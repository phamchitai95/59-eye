#include "MetricsBar.h"
#include "AppInfo.h"
#include "GstPlayer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QTextStream>
#include <QVBoxLayout>

static QString num(double v, const char *unit, int prec = 0)
{
    return v < 0 ? QStringLiteral("–") : QString::number(v, 'f', prec) + QString::fromUtf8(unit);
}

static QString csvNum(double v)
{
    return v < 0 ? QString() : QString::number(v, 'f', 1);
}

MetricsBar::MetricsBar(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 2, 4, 2);
    root->setSpacing(1);

    auto *row1 = new QHBoxLayout;
    m_selfLbl = new QLabel(this);
    m_selfLbl->setTextFormat(Qt::RichText);
    row1->addWidget(m_selfLbl, 1);
    m_sysLbl = new QLabel(this);
    row1->addWidget(m_sysLbl);
    root->addLayout(row1);

    auto *row2 = new QHBoxLayout;
    row2->addWidget(new QLabel(QStringLiteral("So sánh với tiến trình:"), this));
    m_cmpName = new QLineEdit(this);
#ifdef _WIN32
    m_cmpName->setPlaceholderText("SmartPSS.exe");
#else
    m_cmpName->setPlaceholderText(QStringLiteral("tên tiến trình"));
#endif
    m_cmpName->setMaximumWidth(160);
    row2->addWidget(m_cmpName);
    m_cmpLbl = new QLabel(this);
    m_cmpLbl->setTextFormat(Qt::RichText);
    row2->addWidget(m_cmpLbl, 1);
    m_recBtn = new QPushButton(QStringLiteral("● Ghi CSV"), this);
    m_recBtn->setToolTip(QStringLiteral("Ghi số liệu mỗi giây ra file metrics_*.csv cạnh file chạy"));
    row2->addWidget(m_recBtn);
    root->addLayout(row2);

    connect(m_cmpName, &QLineEdit::editingFinished, this, [this] {
        if (m_cmpName->text().trimmed().isEmpty()) m_cmp.setExeName({});
        else m_cmp.setExeName(m_cmpName->text());
    });
    connect(m_recBtn, &QPushButton::clicked, this, &MetricsBar::toggleRecord);

    m_self.setSelf();
    m_cmp.setExeName({});
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &MetricsBar::tick);
    m_timer.start();
    tick();
}

QString MetricsBar::fmt(const ProcSample &s)
{
    return QStringLiteral("CPU <b>%1</b> · RAM <b>%2</b> (WS %3) · GPU giải mã <b>%4</b> · GPU 3D %5 · RAM GPU %6")
        .arg(num(s.cpu, "%", 1), num(s.privateMB, " MB"), num(s.workingMB, " MB"),
             num(s.gpuDecode, "%"), num(s.gpu3d, "%"), num(s.gpuMemMB, " MB"));
}

void MetricsBar::tick()
{
    SystemMetrics &sm = SystemMetrics::instance();
    sm.collect();
    const ProcSample me = m_self.sample();
    const double mbps = GstPlayer::totalKbps() / 1000.0;
    const int streams = GstPlayer::activeCount();
    m_selfLbl->setText(QStringLiteral("<b>59-eye:</b> ") + fmt(me) +
                       QStringLiteral(" · Mạng <b>%1 Mbps</b> · %2 luồng").arg(mbps, 0, 'f', 2).arg(streams));
    m_sysLbl->setText(QStringLiteral("CPU hệ thống %1 · %2").arg(num(sm.systemCpu(), "%"), sm.gpuName()));

    ProcSample other;
    if (!m_cmp.exeName().isEmpty()) {
        other = m_cmp.sample();
        m_cmpLbl->setText(other.processCount == 0
                              ? QStringLiteral("<i>không thấy tiến trình đang chạy</i>")
                              : QStringLiteral("(%1 tiến trình) ").arg(other.processCount) + fmt(other));
    } else {
        m_cmpLbl->setText(QStringLiteral("<i>nhập tên file chạy rồi Enter để đo song song</i>"));
    }

    if (m_csv.isOpen()) {
        QTextStream out(&m_csv);
        out << QDateTime::currentDateTime().toString(Qt::ISODate) << ',' << streams << ','
            << QString::number(mbps, 'f', 2) << ',' << csvNum(sm.systemCpu()) << ',' << csvNum(me.cpu) << ','
            << csvNum(me.privateMB) << ',' << csvNum(me.workingMB) << ',' << csvNum(me.gpuDecode) << ','
            << csvNum(me.gpu3d) << ',' << csvNum(me.gpuMemMB) << ',' << csvNum(other.cpu) << ','
            << csvNum(other.privateMB) << ',' << csvNum(other.workingMB) << ',' << csvNum(other.gpuDecode)
            << ',' << csvNum(other.gpu3d) << ',' << csvNum(other.gpuMemMB) << '\n';
        out.flush();
    }
}

void MetricsBar::setCompare(const QString &exe)
{
    m_cmpName->setText(exe);
    m_cmp.setExeName(exe);
}

void MetricsBar::toggleRecord()
{
    if (m_csv.isOpen()) {
        m_csv.close();
        m_recBtn->setText(QStringLiteral("● Ghi CSV"));
        return;
    }
    const QString path = QDir(AppInfo::dataDir())
                             .filePath(QStringLiteral("metrics_%1.csv")
                                           .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
    m_csv.setFileName(path);
    if (!m_csv.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream(&m_csv) << "time,streams,net_mbps,sys_cpu,cpu,private_mb,ws_mb,gpu_decode,gpu_3d,gpu_mem_mb,"
                           "cmp_cpu,cmp_private_mb,cmp_ws_mb,cmp_gpu_decode,cmp_gpu_3d,cmp_gpu_mem_mb\n";
    m_recBtn->setText(QStringLiteral("■ Dừng ghi (%1)").arg(QFileInfo(path).fileName()));
}
