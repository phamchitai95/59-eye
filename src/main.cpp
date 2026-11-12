#include "Config.h"
#include "DahuaClient.h"
#include "MainWindow.h"
#include "Theme.h"
#include "AppInfo.h"
#include "Users.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>
#include <functional>
#include <memory>

int runLightProbe(int nvr, const QString &outPath, const QList<int> &channels);
int runLightDetect(int nvr, const QString &outPath, const QList<int> &channels);
#include <gst/gst.h>
#ifdef _WIN32
#include <windows.h>
#include <string>
#endif

int main(int argc, char *argv[])
{
#ifndef _WIN32
    // GstVideoOverlay can cua so X11; tren Wayland chay qua XWayland.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "xcb");
#endif
#ifdef _WIN32
    // Ban dong goi: GStreamer di kem trong thu muc cai dat (lib\gstreamer-1.0, libexec\...).
    // Chi dung plugin di kem, khong lan voi ban GStreamer khac co tren may.
    {
        wchar_t buf[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::wstring dir(buf, n);
        dir = dir.substr(0, dir.find_last_of(L"\\/"));
        const std::wstring plugins = dir + L"\\lib\\gstreamer-1.0";
        if (GetFileAttributesW(plugins.c_str()) != INVALID_FILE_ATTRIBUTES) {
            SetEnvironmentVariableW(L"GST_PLUGIN_SYSTEM_PATH_1_0", plugins.c_str());
            SetEnvironmentVariableW(L"GST_PLUGIN_PATH_1_0", L"");
            SetEnvironmentVariableW(L"GST_PLUGIN_PATH", L"");
            SetEnvironmentVariableW(L"GST_PLUGIN_SCANNER_1_0", (dir + L"\\libexec\\gstreamer-1.0\\gst-plugin-scanner.exe").c_str());
            // gst-plugin-scanner (tien trinh con) can tim DLL trong thu muc cai dat
            std::wstring path = dir;
            const DWORD len = GetEnvironmentVariableW(L"PATH", nullptr, 0);
            if (len > 0) {
                std::wstring old(len, L'\0');
                GetEnvironmentVariableW(L"PATH", old.data(), len);
                old.resize(len - 1);
                path += L";" + old;
            }
            SetEnvironmentVariableW(L"PATH", path.c_str());
            wchar_t local[MAX_PATH];
            if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) {
                const std::wstring reg = std::wstring(local) + L"\\59-eye";
                CreateDirectoryW(reg.c_str(), nullptr);
                SetEnvironmentVariableW(L"GST_REGISTRY_1_0", (reg + L"\\gst-registry.bin").c_str());
            }
        }
    }
#endif
    gst_init(&argc, &argv);
    QApplication app(argc, argv);
    // Tao icon tu logo: --make-icons <logo.png> <icon.png> <icon.ico>
    // Tu tim o vuong logo tren nen sang, cat vuong, bo goc trong suot.
    {
        const QStringList a = app.arguments();
        const int mi = a.indexOf("--make-icons");
        if (mi >= 0 && mi + 3 < a.size()) {
            QImage src(a[mi + 1]);
            if (src.isNull()) return 2;
            src = src.convertToFormat(QImage::Format_ARGB32);
            auto dark = [&](int x, int y) {
                const QColor c = src.pixelColor(x, y);
                return c.lightness() < 200;
            };
            int l = src.width(), r = -1, t = src.height(), b = -1;
            for (int y = 0; y < src.height(); y += 2)
                for (int x = 0; x < src.width(); x += 2)
                    if (dark(x, y)) {
                        l = qMin(l, x); r = qMax(r, x);
                        t = qMin(t, y); b = qMax(b, y);
                    }
            if (r < l || b < t) return 3;
            const int side = qMax(r - l, b - t);
            const QRect sq(l + (r - l - side) / 2, t + (b - t - side) / 2, side, side);
            const QImage crop = src.copy(sq).scaled(512, 512, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            QImage out(512, 512, QImage::Format_ARGB32_Premultiplied);
            out.fill(Qt::transparent);
            {
                QPainter p(&out);
                p.setRenderHint(QPainter::Antialiasing);
                QPainterPath path;
                path.addRoundedRect(QRectF(0, 0, 512, 512), 80, 80);
                p.setClipPath(path);
                p.drawImage(0, 0, crop);
            }
            const bool ok = out.save(a[mi + 2], "PNG") &&
                            out.scaled(256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).save(a[mi + 3], "ICO");
            return ok ? 0 : 4;
        }
    }

    QApplication::setApplicationName("59-eye");
    QApplication::setApplicationVersion(AppInfo::version());
    QApplication::setWindowIcon(Icons::app());
    Theme::apply(app);

    // config.json trong thu muc du lieu (ban cai dat: %ProgramData%/59-eye; ban dev: canh file chay).
    // Chua co thi tao file rong: quan tri vien them dau ghi trong tab Cai dat.
    const QString path = AppInfo::dataFile("config.json");
    if (!QFile::exists(path)) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) ||
            f.write("{\n  \"nvrs\": [],\n  \"liveLatencyMs\": 200,\n  \"playbackLatencyMs\": 500,\n  \"videoSink\": \"\"\n}\n") < 0) {
            QMessageBox::critical(nullptr, "59-eye", QStringLiteral("Không tạo được %1").arg(QDir::toNativeSeparators(path)));
            return 1;
        }
    }
    QString err;
    if (!cfg().load(path, &err)) {
        QMessageBox::critical(nullptr, "59-eye", QStringLiteral("Lỗi đọc %1:\n%2").arg(QDir::toNativeSeparators(path), err));
        return 1;
    }

    // Ghi de tam thoi tu dong lenh (de do so sanh): --latency 500  --leaky
    {
        const QStringList a = app.arguments();
        const int li = a.indexOf("--latency");
        if (li >= 0 && li + 1 < a.size()) cfg().liveLatencyMs = a[li + 1].toInt();
        if (a.contains("--leaky")) cfg().liveQueueLeaky = true;
    }

    // Che do do den (chi doc): --light-probe <nvr> <file> <kenh1,kenh2,...>
    {
        const QStringList a = app.arguments();
        int li = a.indexOf("--light-probe");
        const bool detectMode = li < 0 && (li = a.indexOf("--light-detect")) >= 0;
        if (li >= 0 && li + 3 < a.size()) {
            QList<int> chs;
            for (const QString &s : a[li + 3].split(',')) chs << s.toInt();
            const int rc = detectMode ? runLightDetect(a[li + 1].toInt(), a[li + 2], chs)
                                      : runLightProbe(a[li + 1].toInt(), a[li + 2], chs);
            gst_deinit();
            return rc;
        }
    }

    // Che do chan doan khong mo cua so: --cgi-dump <nvr> <file> <duong_dan_cgi>...
    // Goi lan luot cac duong dan CGI (GET) va ghi ket qua vao file. Khong ghi mat khau.
    {
        const QStringList a = app.arguments();
        const int ci = a.indexOf("--cgi-dump");
        if (ci >= 0 && ci + 3 < a.size()) {
            const int nvr = a[ci + 1].toInt();
            const QString outPath = a[ci + 2];
            const QStringList paths = a.mid(ci + 3);
            auto *client = new DahuaClient(nvr, &app);
            auto *out = new QFile(outPath, &app);
            out->open(QIODevice::WriteOnly | QIODevice::Text);
            auto idx = std::make_shared<int>(0);
            auto next = std::make_shared<std::function<void()>>();
            *next = [client, out, paths, idx, next] {
                if (*idx >= paths.size()) { out->close(); QCoreApplication::quit(); return; }
                const QString p = paths[(*idx)++];
                client->get(p, [out, p, next](int status, const QByteArray &body, const QString &err) {
                    out->write(QStringLiteral("### %1  ->  HTTP %2 %3\n").arg(p).arg(status).arg(err).toUtf8());
                    // che moi truong mat khau / khoa truoc khi ghi ra file
                    for (const QByteArray &line : body.split('\n')) {
                        const int eq = line.indexOf('=');
                        const QByteArray k = eq > 0 ? line.left(eq).toLower() : QByteArray();
                        if (k.contains("password") || k.contains("passwd") || k.contains("pwd") || k.contains("key"))
                            out->write(line.left(eq + 1) + "***\n");
                        else
                            out->write(line + "\n");
                    }
                    out->write("\n\n");
                    (*next)();
                });
            };
            QTimer::singleShot(0, &app, [next] { (*next)(); });
            const int rc = app.exec();
            gst_deinit();
            return rc;
        }
    }

    // Dang nhap (tai khoan trong users.json). --login ten:matkhau dung cho chay thu tu dong.
    users().load(AppInfo::dataFile("users.json"));
    {
        const QStringList a = app.arguments();
        const int li = a.indexOf("--login");
        bool ok = false;
        if (li >= 0 && li + 1 < a.size()) {
            const QString cred = a[li + 1];
            ok = users().login(cred.section(':', 0, 0), cred.section(':', 1));
        }
        if (!ok) {
            LoginDialog dlg;
            dlg.setWindowIcon(Icons::app());
            if (dlg.exec() != QDialog::Accepted) {
                gst_deinit();
                return 0;
            }
        }
    }

    int rc;
    {
        MainWindow w;
        // Vua man hinh: toi da 1440x900, khong vuot 92% vung lam viec
        const QRect avail = w.screen()->availableGeometry();
        const QSize sz(qMin(1440, int(avail.width() * 0.92)), qMin(900, int(avail.height() * 0.92)));
        w.resize(sz);
        w.move(avail.center() - QPoint(sz.width() / 2, sz.height() / 2));
        w.show();
        w.runAutomation(app.arguments());
        rc = app.exec();
    }
    gst_deinit();
    return rc;
}
