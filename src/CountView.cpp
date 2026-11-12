#include "CountView.h"
#include "AppInfo.h"
#include "CountWorker.h"
#include "Theme.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QFileInfo>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSplitter>
#include <QStandardPaths>
#include <QVBoxLayout>

// Mo hinh YOLOX Body-Head-Hand (PINTO model zoo, Apache 2.0), dang tren GitHub Releases cua 59-eye
static const char kModelFile[] = "head-yolox-m-384x640.onnx";
static const char kModelUrl[] = "https://github.com/phamchitai95/59-eye/releases/download/model-v1/head-yolox-m-384x640.onnx";
static const char kModelSha256[] = "2ffcd663a58057f035979d052edebb5aa51c51fdd72ebce3f3a99cd8d81c4952";
static constexpr qint64 kModelSize = 101704700;

// ---------- Anh ket luan: vua khung, nhap dup de xem kich thuoc that ----------
class ResultImage : public QWidget {
public:
    explicit ResultImage(QWidget *parent) : QWidget(parent) { setMinimumSize(320, 180); }
    void setImage(const QImage &img, const QString &placeholder = {})
    {
        m_img = img;
        m_placeholder = placeholder;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor("#0a0707"));
        if (m_img.isNull()) {
            p.setPen(Theme::textDim);
            p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_placeholder);
            return;
        }
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QSize s = m_img.size().scaled(size(), Qt::KeepAspectRatio);
        p.drawImage(QRect(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s), m_img);
    }
    void mouseDoubleClickEvent(QMouseEvent *) override
    {
        if (m_img.isNull()) return;
        // Xem kich thuoc that de soat tung nguoi
        auto *dlg = new QDialog(window());
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowTitle(QStringLiteral("Ảnh kết luận (kích thước thật)"));
        auto *lay = new QVBoxLayout(dlg);
        lay->setContentsMargins(0, 0, 0, 0);
        auto *area = new QScrollArea(dlg);
        auto *lbl = new QLabel;
        lbl->setPixmap(QPixmap::fromImage(m_img));
        area->setWidget(lbl);
        lay->addWidget(area);
        dlg->resize(window()->size());
        dlg->show();
    }

private:
    QImage m_img;
    QString m_placeholder;
};

QString CountView::modelPath()
{
    return QDir(AppInfo::dataFile("models")).filePath(QString::fromLatin1(kModelFile));
}

CountView::CountView(QWidget *parent)
    : QWidget(parent), m_worker(new CountWorker), m_net(new QNetworkAccessManager(this))
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 6);
    auto *split = new QSplitter(this);
    root->addWidget(split);

    // Trai: danh sach luot dem
    auto *left = new QWidget(split);
    auto *lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    auto *lt = new QLabel(QStringLiteral("LƯỢT ĐẾM"), left);
    lt->setStyleSheet("color:#a8959a;font-weight:600;");
    lv->addWidget(lt);
    m_list = new QListWidget(left);
    m_list->setIconSize(QSize(18, 18));
    m_list->setMinimumWidth(240);
    lv->addWidget(m_list, 1);
    auto *hint = new QLabel(QStringLiteral("Ở tab Xem trực tiếp: chuột phải vào ô camera của lớp → \"Đếm số người\". "
                                           "Các lượt đếm chạy lần lượt, mỗi lượt tối đa 5 phút. Không lưu lại kết quả."), left);
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#a8959a;");
    lv->addWidget(hint);

    // Phai: tien trinh + ket qua
    auto *right = new QWidget(split);
    auto *rv = new QVBoxLayout(right);
    rv->setContentsMargins(8, 0, 0, 0);
    auto *head = new QHBoxLayout;
    m_title = new QLabel(right);
    QFont tf = m_title->font();
    tf.setPointSize(tf.pointSize() + 3);
    tf.setBold(true);
    m_title->setFont(tf);
    head->addWidget(m_title, 1);
    m_count = new QLabel(right);
    QFont cf = m_count->font();
    cf.setPointSize(cf.pointSize() + 12);
    cf.setBold(true);
    m_count->setFont(cf);
    m_count->setStyleSheet("color:#ffffff;");
    head->addWidget(m_count);
    rv->addLayout(head);
    m_bar = new QProgressBar(right);
    m_bar->setRange(0, 100);
    m_bar->setTextVisible(true);
    m_bar->setFormat("%p%");
    m_bar->setFixedHeight(20);
    m_bar->setStyleSheet("QProgressBar{background:#231718;border:1px solid #3a2528;border-radius:6px;color:#fff;text-align:center;}"
                         "QProgressBar::chunk{background:#e53935;border-radius:5px;}");
    rv->addWidget(m_bar);
    m_step = new QLabel(right);
    m_step->setStyleSheet("color:#dccfd1;");
    rv->addWidget(m_step);
    m_image = new ResultImage(right);
    rv->addWidget(m_image, 1);
    m_msg = new QLabel(right);
    m_msg->setWordWrap(true);
    m_msg->setStyleSheet("color:#a8959a;");
    rv->addWidget(m_msg);
    auto *btns = new QHBoxLayout;
    m_cancelBtn = new QPushButton(QStringLiteral("Huỷ"), right);
    m_retryBtn = new QPushButton(QStringLiteral("Đếm lại"), right);
    m_saveBtn = new QPushButton(Icons::download(), QStringLiteral(" Lưu ảnh..."), right);
    m_removeBtn = new QPushButton(QStringLiteral("Xoá khỏi danh sách"), right);
    btns->addWidget(m_cancelBtn);
    btns->addWidget(m_retryBtn);
    btns->addWidget(m_saveBtn);
    btns->addStretch();
    btns->addWidget(m_removeBtn);
    rv->addLayout(btns);
    split->setStretchFactor(1, 1);
    split->setSizes({280, 900});

    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &CountWorker::progress, this, &CountView::onProgress, Qt::QueuedConnection);
    connect(m_worker, &CountWorker::finished, this, &CountView::onFinished, Qt::QueuedConnection);
    m_thread.start(QThread::LowPriority); // khong lam giat xem truc tiep

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < m_list->count()) showJob(m_list->item(row)->data(Qt::UserRole).toInt());
    });
    connect(m_cancelBtn, &QPushButton::clicked, this, [this] {
        Job *j = job(m_shown);
        if (!j) return;
        if (j->state == Job::Running) {
            m_worker->cancel();
            j->step = QStringLiteral("Đang huỷ...");
            showJob(j->id);
        } else if (j->state == Job::Waiting) {
            j->state = Job::Canceled;
            j->message = QStringLiteral("Đã huỷ");
            refreshItem(j->id);
            showJob(j->id);
        }
    });
    connect(m_retryBtn, &QPushButton::clicked, this, [this] {
        if (Job *j = job(m_shown)) enqueue(j->ref);
    });
    connect(m_removeBtn, &QPushButton::clicked, this, [this] {
        Job *j = job(m_shown);
        if (!j || j->state == Job::Running) return;
        const int id = j->id;
        for (int i = 0; i < m_jobs.size(); ++i)
            if (m_jobs[i].id == id) m_jobs.removeAt(i);
        for (int r = 0; r < m_list->count(); ++r)
            if (m_list->item(r)->data(Qt::UserRole).toInt() == id) delete m_list->takeItem(r);
        m_shown = -1;
        if (m_list->count() > 0) m_list->setCurrentRow(0);
        else showJob(-1);
    });
    connect(m_saveBtn, &QPushButton::clicked, this, [this] {
        Job *j = job(m_shown);
        if (!j || j->result.isNull()) return;
        QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        QString name = QStringLiteral("dem_%1_%2.jpg").arg(j->title, j->created.toString("yyyyMMdd_HHmm"));
        name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|·\\s]+")), "_");
        const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Lưu ảnh kết luận"), QDir(dir).filePath(name), "JPEG (*.jpg)");
        if (!f.isEmpty() && !j->result.save(f, "JPG", 90))
            QMessageBox::warning(this, "59-eye", QStringLiteral("Không lưu được %1").arg(f));
    });
    showJob(-1);
}

CountView::~CountView()
{
    m_worker->cancel();
    m_thread.quit();
    m_thread.wait(15000);
}

CountView::Job *CountView::job(int id)
{
    for (Job &j : m_jobs)
        if (j.id == id) return &j;
    return nullptr;
}

void CountView::enqueue(const ChannelRef &ref, const QDateTime &from)
{
    if (!ref.valid()) return;
    Job j;
    j.id = m_nextId++;
    j.ref = ref;
    j.title = cfg().channelTitle(ref);
    j.created = QDateTime::currentDateTime();
    j.from = from;
    j.step = QStringLiteral("Đang chờ lượt");
    m_jobs.append(j);
    auto *it = new QListWidgetItem(Icons::camera(), QString());
    it->setData(Qt::UserRole, j.id);
    m_list->insertItem(0, it);
    refreshItem(j.id);
    m_list->setCurrentItem(it);
    startNext();
}

void CountView::refreshItem(int id)
{
    const Job *j = job(id);
    if (!j) return;
    QString status;
    switch (j->state) {
    case Job::Waiting: status = QStringLiteral("đang chờ"); break;
    case Job::Running: status = QStringLiteral("đang đếm %1%").arg(j->percent); break;
    case Job::Done: status = QStringLiteral("→ %1 người").arg(j->count); break;
    case Job::Failed: status = QStringLiteral("lỗi"); break;
    case Job::Canceled: status = QStringLiteral("đã huỷ"); break;
    }
    for (int r = 0; r < m_list->count(); ++r) {
        QListWidgetItem *it = m_list->item(r);
        if (it->data(Qt::UserRole).toInt() != id) continue;
        it->setText(QStringLiteral("%1\n%2 · %3").arg(cfg().shortTitle(j->ref), j->created.toString("HH:mm"), status));
        it->setForeground(j->state == Job::Failed ? Theme::bad : j->state == Job::Done ? Theme::text : Theme::textDim);
    }
}

void CountView::showJob(int id)
{
    m_shown = id;
    const Job *j = job(id);
    if (!j) {
        m_title->setText(QStringLiteral("Đếm số người"));
        m_count->clear();
        m_bar->setValue(0);
        m_bar->setVisible(false);
        m_step->clear();
        m_msg->clear();
        m_image->setImage({}, QStringLiteral("Chưa có lượt đếm nào.\n\nỞ tab Xem trực tiếp, chuột phải vào ô camera của lớp cần đếm → \"Đếm số người\"."));
        for (QPushButton *b : {m_cancelBtn, m_retryBtn, m_saveBtn, m_removeBtn}) b->setEnabled(false);
        return;
    }
    // ten kenh co the tai ve sau khi them luot -> lay lai moi lan hien
    m_title->setText(QStringLiteral("%1 · %2").arg(cfg().channelTitle(j->ref), j->created.toString("dd/MM HH:mm")));
    m_count->setText(j->state == Job::Done ? QStringLiteral("%1 người").arg(j->count) : QString());
    m_bar->setVisible(j->state == Job::Running || j->state == Job::Waiting);
    m_bar->setValue(j->percent);
    m_step->setText(j->state == Job::Done ? QStringLiteral("Đã xong. Nhấp đúp vào ảnh để xem kích thước thật và soát lại từng người.")
                                          : j->step);
    QString placeholder;
    if (j->state == Job::Waiting) placeholder = QStringLiteral("Đang chờ lượt đếm trước xong...");
    else if (j->state == Job::Running) placeholder = QStringLiteral("Đang chụp và nhận diện, ảnh kết luận sẽ hiện ở đây khi xong.");
    else if (j->state != Job::Done) placeholder = j->message;
    m_image->setImage(j->result, placeholder);
    QString msg = j->message;
    if (j->state == Job::Done)
        msg += QStringLiteral("\nMáy có thể đếm sót người bị che hoặc đếm nhầm đồ vật: hãy soát ảnh trước khi ghi số.");
    m_msg->setText(msg);
    m_msg->setStyleSheet(j->state == Job::Failed ? "color:#f08a84;" : "color:#a8959a;");
    m_cancelBtn->setEnabled(j->state == Job::Running || j->state == Job::Waiting);
    m_retryBtn->setEnabled(j->state != Job::Running && j->state != Job::Waiting);
    m_saveBtn->setEnabled(!j->result.isNull());
    m_removeBtn->setEnabled(j->state != Job::Running);
}

void CountView::startNext()
{
    if (m_running >= 0 || m_downloading) return;
    Job *next = nullptr;
    for (Job &j : m_jobs) // ds them moi o cuoi -> lay luot cu nhat dang cho
        if (j.state == Job::Waiting) {
            next = &j;
            break;
        }
    if (!next) return;
    const int id = next->id;
    next->state = Job::Running;
    next->percent = 0;
    m_running = id;
    refreshItem(id);
    if (m_shown == id) showJob(id);
    ensureModel(id, [this, id](bool ok, const QString &err) {
        Job *j = job(id);
        if (!j) {
            m_running = -1;
            startNext();
            return;
        }
        if (!ok) {
            onFinished(id, false, 0, {}, err);
            return;
        }
        if (j->state != Job::Running) { // huy trong luc tai mo hinh
            m_running = -1;
            startNext();
            return;
        }
        const NvrConfig &n = cfg().nvrs[j->ref.nvr];
        CountRequest req;
        req.id = id;
        req.title = j->title;
        // luong chinh: net hon de bat duoc dau o day cuoi
        req.url = j->from.isValid() ? cfg().playbackUrl(j->ref, j->from, j->from.addSecs(120), true)
                                    : cfg().liveUrl(j->ref, true);
        req.user = n.user;
        req.password = n.password;
        req.modelPath = modelPath();
        QMetaObject::invokeMethod(m_worker, [w = m_worker, req] { w->process(req); }, Qt::QueuedConnection);
    });
}

void CountView::ensureModel(int jobId, std::function<void(bool, const QString &)> done)
{
    const QString path = modelPath();
    if (QFileInfo(path).size() == kModelSize) return done(true, {});
    // Lan dau: tai mo hinh (~100 MB) tu GitHub Releases, kiem tra SHA-256 roi moi dung
    QDir().mkpath(QFileInfo(path).absolutePath());
    auto *file = new QFile(path + ".part", this);
    if (!file->open(QIODevice::WriteOnly)) {
        delete file;
        return done(false, QStringLiteral("Không ghi được %1").arg(QDir::toNativeSeparators(path)));
    }
    m_downloading = true;
    QNetworkRequest rq{QUrl(QString::fromLatin1(kModelUrl))};
    rq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *r = m_net->get(rq);
    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    connect(r, &QNetworkReply::readyRead, this, [r, file, hash] {
        const QByteArray b = r->readAll();
        hash->addData(b);
        file->write(b);
    });
    connect(r, &QNetworkReply::downloadProgress, this, [this, jobId](qint64 got, qint64) {
        onProgress(jobId, 0, QStringLiteral("Đang tải mô hình nhận diện (chỉ lần đầu): %1 / %2 MB")
                                 .arg(got / 1048576).arg(kModelSize / 1048576));
    });
    connect(r, &QNetworkReply::finished, this, [this, r, file, hash, path, done] {
        m_downloading = false;
        const QByteArray rest = r->readAll();
        hash->addData(rest);
        file->write(rest);
        file->close();
        const bool netOk = r->error() == QNetworkReply::NoError;
        const QString netErr = r->errorString();
        r->deleteLater();
        const bool shaOk = QString::fromLatin1(hash->result().toHex()) == QLatin1String(kModelSha256);
        if (!netOk || !shaOk) {
            file->remove();
            file->deleteLater();
            return done(false, netOk ? QStringLiteral("Mô hình tải về bị sai (SHA-256 không khớp), đã xoá")
                                     : QStringLiteral("Không tải được mô hình nhận diện: %1").arg(netErr));
        }
        QFile::remove(path);
        const bool renamed = file->rename(path);
        file->deleteLater();
        done(renamed, renamed ? QString() : QStringLiteral("Không đổi tên được file mô hình"));
    });
}

void CountView::onProgress(int id, int percent, const QString &step)
{
    Job *j = job(id);
    if (!j || j->state != Job::Running) return;
    j->percent = percent;
    j->step = step;
    refreshItem(id);
    if (m_shown == id) {
        m_bar->setValue(percent);
        m_step->setText(step);
    }
}

void CountView::onFinished(int id, bool ok, int count, const QImage &img, const QString &msg)
{
    if (Job *j = job(id)) {
        if (ok) {
            j->state = Job::Done;
            j->count = count;
            j->result = img;
            j->percent = 100;
        } else {
            j->state = msg == QStringLiteral("Đã huỷ") ? Job::Canceled : Job::Failed;
        }
        j->message = msg;
        refreshItem(id);
        if (m_shown == id) showJob(id);
        emit jobFinished(ok, count, img, msg);
    }
    if (m_running == id) m_running = -1;
    startNext();
}
