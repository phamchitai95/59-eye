#include "ExportDialog.h"
#include "ClipExporter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

static constexpr qint64 kMaxMs = 2LL * 3600 * 1000; // toi da 2 gio / lan

ExportDialog::ExportDialog(const ChannelRef &ref, const QDateTime &from, const QDateTime &to, bool mainStream,
                           QWidget *parent)
    : QDialog(parent), m_ref(ref), m_exp(new ClipExporter(this))
{
    setWindowTitle(QStringLiteral("Trích đoạn ra MP4"));
    setMinimumWidth(520);
    auto *root = new QVBoxLayout(this);
    auto *title = new QLabel(cfg().channelTitle(ref), this);
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSize(tf.pointSize() + 2);
    title->setFont(tf);
    root->addWidget(title);

    auto *form = new QFormLayout;
    form->setSpacing(8);
    m_from = new QDateTimeEdit(from, this);
    m_to = new QDateTimeEdit(to, this);
    for (QDateTimeEdit *e : {m_from, m_to}) {
        e->setDisplayFormat("dd/MM/yyyy HH:mm:ss");
        e->setCalendarPopup(true);
    }
    m_stream = new QComboBox(this);
    m_stream->addItem(QStringLiteral("Chính (nét, file lớn)"), true);
    m_stream->addItem(QStringLiteral("Phụ (nhẹ)"), false);
    m_stream->setCurrentIndex(mainStream ? 0 : 1);
    m_audio = new QCheckBox(QStringLiteral("Kèm âm thanh (đổi sang AAC)"), this);
    m_audio->setChecked(true);
    auto *pathRow = new QHBoxLayout;
    m_path = new QLineEdit(this);
    m_browse = new QPushButton(QStringLiteral("Chọn..."), this);
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(m_browse);
    form->addRow(QStringLiteral("Từ"), m_from);
    form->addRow(QStringLiteral("Đến"), m_to);
    form->addRow(QStringLiteral("Luồng"), m_stream);
    form->addRow(QString(), m_audio);
    form->addRow(QStringLiteral("Lưu vào"), pathRow);
    root->addLayout(form);

    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 1000);
    m_bar->setValue(0);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(8);
    root->addWidget(m_bar);
    m_status = new QLabel(QStringLiteral("Trích theo tốc độ thực: đoạn 1 phút mất khoảng 1 phút. Tối đa 2 giờ mỗi lần."), this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet("color:#a8959a;");
    root->addWidget(m_status);

    auto *btns = new QHBoxLayout;
    m_openBtn = new QPushButton(QStringLiteral("Mở thư mục"), this);
    m_openBtn->setEnabled(false);
    m_startBtn = new QPushButton(QStringLiteral("Bắt đầu trích"), this);
    m_startBtn->setDefault(true);
    m_stopBtn = new QPushButton(QStringLiteral("Dừng và lưu"), this);
    m_stopBtn->setEnabled(false);
    m_closeBtn = new QPushButton(QStringLiteral("Đóng"), this);
    btns->addWidget(m_openBtn);
    btns->addStretch();
    btns->addWidget(m_startBtn);
    btns->addWidget(m_stopBtn);
    btns->addWidget(m_closeBtn);
    root->addLayout(btns);

    updateDefaultName();
    connect(m_from, &QDateTimeEdit::dateTimeChanged, this, &ExportDialog::updateDefaultName);
    connect(m_path, &QLineEdit::textEdited, this, [this] { m_pathEdited = true; });
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Lưu MP4"), m_path->text(), "MP4 (*.mp4)");
        if (!f.isEmpty()) {
            m_path->setText(f.endsWith(".mp4", Qt::CaseInsensitive) ? f : f + ".mp4");
            m_pathEdited = true;
        }
    });
    connect(m_startBtn, &QPushButton::clicked, this, &ExportDialog::start);
    connect(m_stopBtn, &QPushButton::clicked, this, [this] {
        m_stopBtn->setEnabled(false);
        m_status->setText(QStringLiteral("Đang đóng file..."));
        m_exp->stopAndSave();
    });
    connect(m_closeBtn, &QPushButton::clicked, this, &ExportDialog::reject);
    connect(m_openBtn, &QPushButton::clicked, this, [this] {
#ifdef _WIN32
        QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(m_lastFile)});
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_lastFile).absolutePath()));
#endif
    });
    connect(m_exp, &ClipExporter::progress, this, [this](qint64 done, qint64 total, qint64 bytes) {
        m_bar->setValue(total > 0 ? int(done * 1000 / total) : 0);
        m_status->setStyleSheet("color:#a8959a;");
        m_status->setText(QStringLiteral("Đang trích %1 / %2 giây · %3 MB")
                              .arg(done / 1000).arg(total / 1000).arg(bytes / 1048576.0, 0, 'f', 1));
    });
    connect(m_exp, &ClipExporter::finished, this, [this](bool ok, const QString &msg) {
        setRunning(false);
        if (ok) m_bar->setValue(1000);
        m_status->setStyleSheet(ok ? "color:#7ee2a8;" : "color:#f08a84;");
        m_status->setText(ok ? QStringLiteral("%1 → %2").arg(msg, QDir::toNativeSeparators(m_lastFile)) : msg);
        m_openBtn->setEnabled(ok);
    });
}

void ExportDialog::updateDefaultName()
{
    if (m_pathEdited) return;
    QString dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (dir.isEmpty()) dir = QDir::homePath();
    dir = QDir(dir).filePath("59-eye");
    QString name = cfg().channelTitle(m_ref);
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|·]+")), "_");
    name = name.simplified().replace(' ', '_');
    m_path->setText(QDir::toNativeSeparators(
        QDir(dir).filePath(QStringLiteral("%1_%2.mp4").arg(name, m_from->dateTime().toString("yyyyMMdd_HHmmss")))));
}

void ExportDialog::setRunning(bool on)
{
    m_startBtn->setEnabled(!on);
    m_stopBtn->setEnabled(on);
    m_from->setEnabled(!on);
    m_to->setEnabled(!on);
    m_stream->setEnabled(!on);
    m_audio->setEnabled(!on);
    m_path->setEnabled(!on);
    m_browse->setEnabled(!on);
}

void ExportDialog::start()
{
    const QDateTime a = m_from->dateTime(), b = m_to->dateTime();
    const qint64 ms = a.msecsTo(b);
    m_status->setStyleSheet("color:#f08a84;");
    if (ms < 2000) return m_status->setText(QStringLiteral("Thời điểm kết thúc phải sau thời điểm bắt đầu"));
    if (ms > kMaxMs) return m_status->setText(QStringLiteral("Đoạn trích tối đa 2 giờ"));
    if (b > QDateTime::currentDateTime()) return m_status->setText(QStringLiteral("Thời điểm kết thúc đang ở tương lai"));
    QString out = QDir::fromNativeSeparators(m_path->text().trimmed());
    if (out.isEmpty()) return m_status->setText(QStringLiteral("Chọn nơi lưu file"));
    if (!out.endsWith(".mp4", Qt::CaseInsensitive)) out += ".mp4";
    if (!QDir().mkpath(QFileInfo(out).absolutePath()))
        return m_status->setText(QStringLiteral("Không tạo được thư mục %1").arg(QFileInfo(out).absolutePath()));
    if (QFileInfo::exists(out) &&
        QMessageBox::question(this, QStringLiteral("Ghi đè"), QStringLiteral("File đã có, ghi đè?")) != QMessageBox::Yes)
        return;
    startNow(out);
}

void ExportDialog::startNow(const QString &outFile)
{
    const NvrConfig &n = cfg().nvrs[m_ref.nvr];
    const QDateTime a = m_from->dateTime(), b = m_to->dateTime();
    // xin them 5 giay de chac chan du thoi luong (dung bang dem PTS)
    const QString url = cfg().playbackUrl(m_ref, a, b.addSecs(5), m_stream->currentData().toBool());
    m_lastFile = outFile;
    m_openBtn->setEnabled(false);
    m_bar->setValue(0);
    setRunning(true);
    m_status->setStyleSheet("color:#a8959a;");
    m_status->setText(QStringLiteral("Đang kết nối đầu ghi..."));
    if (!m_exp->start(url, n.user, n.password, a.msecsTo(b), outFile, m_audio->isChecked())) setRunning(false);
}

void ExportDialog::reject()
{
    if (m_exp->isRunning()) {
        if (QMessageBox::question(this, QStringLiteral("Đang trích"),
                                  QStringLiteral("Đang trích đoạn. Dừng lại và lưu phần đã có?")) != QMessageBox::Yes)
            return;
        m_exp->stopAndSave();
        // cho file dong xong roi moi dong hop thoai
        connect(m_exp, &ClipExporter::finished, this, [this] { QDialog::reject(); });
        return;
    }
    QDialog::reject();
}
