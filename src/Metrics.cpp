#include "Metrics.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#endif

static qint64 nowNs()
{
    static QElapsedTimer t = [] { QElapsedTimer x; x.start(); return x; }();
    return t.nsecsElapsed();
}

static int cpuCount()
{
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return std::max<int>(1, si.dwNumberOfProcessors);
#else
    return std::max<long>(1, sysconf(_SC_NPROCESSORS_ONLN));
#endif
}

// =====================================================================
// SystemMetrics
// =====================================================================
struct GpuItem {
    quint32 pid;
    QString name;
    double value;
};

struct SystemMetrics::Impl {
#ifdef _WIN32
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER engine = nullptr, dedicated = nullptr, shared = nullptr;
    quint64 prevIdle = 0, prevTotal = 0;
    int age = 0;
#else
    quint64 prevIdle = 0, prevTotal = 0;
    QString busyPath;
#endif
    QVector<GpuItem> engines;
    QVector<GpuItem> mem;
};

#ifdef _WIN32
static quint64 ft2u(const FILETIME &f)
{
    return (quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime;
}

static quint32 pidFromInstance(const QString &n)
{
    static const QRegularExpression re(QStringLiteral("pid_(\\d+)_"));
    const auto m = re.match(n);
    return m.hasMatch() ? m.captured(1).toUInt() : 0;
}

static void readArray(PDH_HCOUNTER c, QVector<GpuItem> &out)
{
    out.clear();
    if (!c) return;
    DWORD size = 0, count = 0;
    PDH_STATUS st = PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, nullptr);
    if (st != PDH_STATUS(PDH_MORE_DATA) || size == 0) return;
    QByteArray buf(int(size), 0);
    auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buf.data());
    if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, items) != ERROR_SUCCESS)
        return;
    out.reserve(int(count));
    for (DWORD i = 0; i < count; ++i) {
        if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
            items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA)
            continue;
        const QString name = QString::fromWCharArray(items[i].szName);
        out.push_back({pidFromInstance(name), name, items[i].FmtValue.doubleValue});
    }
}
#endif

SystemMetrics &SystemMetrics::instance()
{
    static SystemMetrics s;
    return s;
}

SystemMetrics::SystemMetrics() : d(new Impl)
{
#ifdef _WIN32
    if (PdhOpenQueryW(nullptr, 0, &d->query) == ERROR_SUCCESS) {
        const bool a = PdhAddEnglishCounterW(d->query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &d->engine) == ERROR_SUCCESS;
        PdhAddEnglishCounterW(d->query, L"\\GPU Process Memory(*)\\Dedicated Usage", 0, &d->dedicated);
        PdhAddEnglishCounterW(d->query, L"\\GPU Process Memory(*)\\Shared Usage", 0, &d->shared);
        PdhCollectQueryData(d->query);
        m_gpuOk = a;
    }
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
            m_gpuName = QString::fromWCharArray(dd.DeviceString);
            break;
        }
    }
#else
    for (const QString &card : QDir("/sys/class/drm").entryList({"card?"}, QDir::Dirs)) {
        const QString p = "/sys/class/drm/" + card + "/device/gpu_busy_percent";
        if (QFile::exists(p)) {
            d->busyPath = p;
            m_gpuOk = true;
            break;
        }
    }
#endif
}

void SystemMetrics::collect()
{
#ifdef _WIN32
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const quint64 i = ft2u(idle), total = ft2u(kernel) + ft2u(user);
        if (d->prevTotal && total > d->prevTotal)
            m_sysCpu = 100.0 * (1.0 - double(i - d->prevIdle) / double(total - d->prevTotal));
        d->prevIdle = i;
        d->prevTotal = total;
    }
    if (d->query && PdhCollectQueryData(d->query) == ERROR_SUCCESS) {
        readArray(d->engine, d->engines);
        QVector<GpuItem> ded, sh;
        readArray(d->dedicated, ded);
        readArray(d->shared, sh);
        d->mem = ded + sh;
    }
#else
    QFile f("/proc/stat");
    if (f.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> v = f.readLine().simplified().split(' ');
        if (v.size() > 5) {
            quint64 total = 0;
            for (int k = 1; k < v.size(); ++k) total += v[k].toULongLong();
            const quint64 idle = v[4].toULongLong() + (v.size() > 5 ? v[5].toULongLong() : 0);
            if (d->prevTotal && total > d->prevTotal)
                m_sysCpu = 100.0 * (1.0 - double(idle - d->prevIdle) / double(total - d->prevTotal));
            d->prevIdle = idle;
            d->prevTotal = total;
        }
    }
    d->engines.clear();
    if (!d->busyPath.isEmpty()) {
        QFile b(d->busyPath);
        if (b.open(QIODevice::ReadOnly)) d->engines.push_back({0, "engtype_3D", b.readAll().trimmed().toDouble()});
    }
#endif
}

double SystemMetrics::gpuEngine(const QVector<quint32> &pids, const QString &engType) const
{
    if (!m_gpuOk) return -1;
    double sum = 0;
    for (const GpuItem &g : d->engines) {
#ifdef _WIN32
        if (!pids.contains(g.pid)) continue;
#else
        Q_UNUSED(pids);
#endif
        if (g.name.endsWith("engtype_" + engType, Qt::CaseInsensitive)) sum += g.value;
    }
    return std::min(100.0, sum);
}

double SystemMetrics::gpuMemMB(const QVector<quint32> &pids) const
{
    double sum = 0;
    bool any = false;
    for (const GpuItem &g : d->mem)
        if (pids.contains(g.pid)) {
            sum += g.value;
            any = true;
        }
    return any ? sum / (1024.0 * 1024.0) : -1;
}

// =====================================================================
// ProcessMonitor
// =====================================================================
ProcessMonitor::ProcessMonitor() = default;
ProcessMonitor::~ProcessMonitor() = default;

void ProcessMonitor::setSelf()
{
    m_self = true;
    m_exe.clear();
    m_prevCpu.clear();
    m_resolveCounter = 0;
}

void ProcessMonitor::setExeName(const QString &exe)
{
    m_self = false;
    m_exe = exe.trimmed();
    m_prevCpu.clear();
    m_resolveCounter = 0;
}

QVector<quint32> ProcessMonitor::resolvePids()
{
    if (m_self) return {quint32(QCoreApplication::applicationPid())};
    if (m_exe.isEmpty()) return {};
    if (m_resolveCounter++ % 5 != 0) return m_pids; // tim lai danh sach moi 5 giay
    QVector<quint32> pids;
#ifdef _WIN32
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (QString::fromWCharArray(pe.szExeFile).compare(m_exe, Qt::CaseInsensitive) == 0)
                    pids.push_back(pe.th32ProcessID);
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
#else
    for (const QString &e : QDir("/proc").entryList(QDir::Dirs)) {
        bool ok = false;
        const quint32 pid = e.toUInt(&ok);
        if (!ok) continue;
        QFile f("/proc/" + e + "/comm");
        if (f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll().trimmed()) == m_exe) pids.push_back(pid);
    }
#endif
    m_pids = pids;
    return pids;
}

ProcSample ProcessMonitor::sample()
{
    ProcSample s;
    const QVector<quint32> pids = resolvePids();
    s.processCount = pids.size();
    if (pids.isEmpty()) return s;

    const qint64 wall = nowNs();
    const double wallDelta = m_prevWallNs ? double(wall - m_prevWallNs) : 0;
    double cpuDeltaNs = 0;
    bool haveCpu = false;
    double priv = 0, ws = 0;
    QHash<quint32, quint64> cur;

    for (quint32 pid : pids) {
#ifdef _WIN32
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) continue;
        FILETIME c, e, k, u;
        if (GetProcessTimes(h, &c, &e, &k, &u)) {
            const quint64 t = ft2u(k) + ft2u(u); // don vi 100ns
            cur.insert(pid, t);
            if (m_prevCpu.contains(pid)) {
                cpuDeltaNs += double(t - m_prevCpu.value(pid)) * 100.0;
                haveCpu = true;
            }
        }
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        if (GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc))) {
            priv += pmc.PrivateUsage;
            ws += pmc.WorkingSetSize;
        }
        CloseHandle(h);
#else
        QFile f(QStringLiteral("/proc/%1/stat").arg(pid));
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray all = f.readAll();
            const int rp = all.lastIndexOf(')');
            const QList<QByteArray> v = all.mid(rp + 2).split(' ');
            if (v.size() > 22) {
                const quint64 t = v[11].toULongLong() + v[12].toULongLong(); // utime + stime (tick)
                cur.insert(pid, t);
                if (m_prevCpu.contains(pid)) {
                    cpuDeltaNs += double(t - m_prevCpu.value(pid)) * 1e9 / sysconf(_SC_CLK_TCK);
                    haveCpu = true;
                }
                ws += v[21].toDouble() * sysconf(_SC_PAGESIZE); // rss (trang)
            }
        }
        QFile st(QStringLiteral("/proc/%1/status").arg(pid));
        if (st.open(QIODevice::ReadOnly)) {
            for (const QByteArray &line : st.readAll().split('\n'))
                if (line.startsWith("RssAnon:")) priv += line.mid(8).trimmed().split(' ').value(0).toDouble() * 1024;
        }
#endif
    }
    if (haveCpu && wallDelta > 0) s.cpu = 100.0 * cpuDeltaNs / (wallDelta * cpuCount());
    m_prevCpu = cur;
    m_prevWallNs = wall;
    s.privateMB = priv / (1024.0 * 1024.0);
    s.workingMB = ws / (1024.0 * 1024.0);

    const SystemMetrics &sm = SystemMetrics::instance();
#ifdef _WIN32
    s.gpuDecode = sm.gpuEngine(pids, "VideoDecode");
    s.gpu3d = sm.gpuEngine(pids, "3D");
    s.gpuMemMB = sm.gpuMemMB(pids);
#else
    s.gpu3d = sm.gpuEngine(pids, "3D"); // AMD: % ban toan he thong
#endif
    return s;
}
