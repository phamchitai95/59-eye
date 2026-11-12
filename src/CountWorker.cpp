#include "CountWorker.h"
#include "HeadDetector.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QThread>
#include <cmath>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>

CountWorker::CountWorker(QObject *parent) : QObject(parent) {}

CountWorker::~CountWorker()
{
    delete m_detector;
}

// Chi lay luong hinh, bo luong tieng
static gboolean selectVideoOnly(GstElement *, guint, GstCaps *caps, gpointer)
{
    if (!caps || gst_caps_get_size(caps) == 0) return TRUE;
    const gchar *media = gst_structure_get_string(gst_caps_get_structure(caps, 0), "media");
    return !media || g_str_equal(media, "video");
}

static QImage sampleToImage(GstSample *s)
{
    GstVideoInfo info;
    if (!gst_video_info_from_caps(&info, gst_sample_get_caps(s))) return {};
    GstBuffer *buf = gst_sample_get_buffer(s);
    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) return {};
    const int w = GST_VIDEO_INFO_WIDTH(&info), h = GST_VIDEO_INFO_HEIGHT(&info);
    const int stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, 0);
    QImage img = QImage(map.data, w, h, stride, QImage::Format_RGB888).copy(); // copy: buffer se duoc tra lai
    gst_buffer_unmap(buf, &map);
    return img;
}

bool CountWorker::capture(const CountRequest &req, QVector<QImage> *frames, QString *error)
{
    GError *perr = nullptr;
    GstElement *pipe = gst_parse_launch(
        "rtspsrc name=src protocols=tcp latency=300 ! decodebin ! videoconvert ! video/x-raw,format=RGB ! "
        "appsink name=sink max-buffers=1 drop=true sync=false", &perr);
    if (!pipe) {
        *error = QStringLiteral("Không tạo được pipeline: %1").arg(QString::fromUtf8(perr ? perr->message : "?"));
        if (perr) g_error_free(perr);
        return false;
    }
    GstElement *src = gst_bin_get_by_name(GST_BIN(pipe), "src");
    GstElement *sink = gst_bin_get_by_name(GST_BIN(pipe), "sink");
    g_object_set(src, "location", req.url.toUtf8().constData(), "user-id", req.user.toUtf8().constData(),
                 "user-pw", req.password.toUtf8().constData(), nullptr);
    g_signal_connect(src, "select-stream", G_CALLBACK(selectVideoOnly), nullptr);
    GstBus *bus = gst_element_get_bus(pipe);
    gst_element_set_state(pipe, GST_STATE_PLAYING);

    auto busError = [&]() -> QString {
        while (GstMessage *m = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR)) {
            GError *e = nullptr;
            gst_message_parse_error(m, &e, nullptr);
            const QString t = QString::fromUtf8(e ? e->message : "lỗi");
            if (e) g_error_free(e);
            gst_message_unref(m);
            return t;
        }
        return {};
    };

    bool good = true;
    QElapsedTimer clock;
    const int interval = req.frames > 1 ? req.captureMs / req.frames : 0;
    // Cho khung dau tien (toi da 15 giay), moc thoi gian tinh tu luc co hinh
    GstSample *first = nullptr;
    QElapsedTimer wait;
    wait.start();
    while (!first && !m_cancel && wait.elapsed() < 15000) {
        first = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 200 * GST_MSECOND);
        const QString e = busError();
        if (!e.isEmpty()) {
            *error = QStringLiteral("Camera lỗi: %1").arg(e);
            good = false;
            break;
        }
    }
    if (good && !first && !m_cancel) {
        *error = QStringLiteral("Không nhận được hình từ camera sau 15 giây");
        good = false;
    }
    if (first) {
        frames->append(sampleToImage(first));
        gst_sample_unref(first);
        clock.start();
        emit progress(req.id, 2, QStringLiteral("Đang chụp khung 1/%1").arg(req.frames));
    }
    for (int i = 1; good && !m_cancel && i < req.frames; ++i) {
        while (!m_cancel && clock.elapsed() < qint64(i) * interval) {
            QThread::msleep(100);
            emit progress(req.id, int(2 + 13.0 * clock.elapsed() / req.captureMs),
                          QStringLiteral("Đang chụp khung %1/%2").arg(i + 1).arg(req.frames));
        }
        GstSample *s = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 5 * GST_SECOND);
        if (!s) {
            const QString e = busError();
            if (frames->size() >= 2) break; // mat hinh giua chung: dem voi so khung da co
            *error = e.isEmpty() ? QStringLiteral("Camera ngừng gửi hình") : QStringLiteral("Camera lỗi: %1").arg(e);
            good = false;
            break;
        }
        frames->append(sampleToImage(s));
        gst_sample_unref(s);
    }
    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(src);
    gst_object_unref(sink);
    gst_object_unref(pipe);
    return good && !m_cancel && !frames->isEmpty();
}

namespace {
struct Cluster {
    QVector<QRectF> boxes;
    QVector<int> frames;
    QRectF mean() const
    {
        double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        for (const QRectF &b : boxes) {
            x1 += b.left();
            y1 += b.top();
            x2 += b.right();
            y2 += b.bottom();
        }
        const double n = boxes.size();
        return QRectF(QPointF(x1 / n, y1 / n), QPointF(x2 / n, y2 / n));
    }
};

double matchScore(const QRectF &a, const QRectF &b)
{
    const QRectF i = a.intersected(b);
    const double inter = i.isValid() ? i.width() * i.height() : 0;
    const double a1 = a.width() * a.height(), a2 = b.width() * b.height();
    return std::max(inter / (a1 + a2 - inter + 1e-6), 0.8 * inter / (std::min(a1, a2) + 1e-6));
}
}

void CountWorker::process(const CountRequest &req)
{
    m_cancel = false;
    QElapsedTimer total;
    total.start();
    emit progress(req.id, 0, QStringLiteral("Đang nạp mô hình nhận diện..."));
    if (!m_detector || m_loadedModel != req.modelPath) {
        delete m_detector;
        m_detector = new HeadDetector;
        QString e;
        if (!m_detector->load(req.modelPath, &e)) {
            delete m_detector;
            m_detector = nullptr;
            emit finished(req.id, false, 0, {}, QStringLiteral("Không nạp được mô hình: %1").arg(e));
            return;
        }
        m_loadedModel = req.modelPath;
    }

    emit progress(req.id, 1, QStringLiteral("Đang kết nối camera..."));
    QVector<QImage> frames;
    QString err;
    if (!capture(req, &frames, &err)) {
        emit finished(req.id, false, 0, {}, m_cancel ? QStringLiteral("Đã huỷ") : err);
        return;
    }

    // Nhan dien tung khung, gom cum vi tri qua cac khung
    const int passesPerFrame = HeadDetector::passesFor(frames.first().size());
    const int totalPasses = passesPerFrame * int(frames.size());
    int donePasses = 0, processed = 0;
    bool timeUp = false;
    QVector<Cluster> clusters;
    QVector<int> perFrame;
    for (int f = 0; f < frames.size() && !m_cancel; ++f) {
        if (total.elapsed() > req.budgetMs && processed >= 2) {
            timeUp = true;
            break;
        }
        auto onPass = [&]() {
            ++donePasses;
            const int pct = 15 + int(83.0 * donePasses / totalPasses);
            emit progress(req.id, pct, QStringLiteral("Đang nhận diện khung %1/%2").arg(f + 1).arg(frames.size()));
            return !m_cancel.load();
        };
        const QVector<HeadDet> heads = m_detector->detect(frames[f], onPass, &err);
        if (!err.isEmpty()) {
            emit finished(req.id, false, 0, {}, QStringLiteral("Lỗi nhận diện: %1").arg(err));
            return;
        }
        if (m_cancel) break;
        perFrame << int(heads.size());
        for (const HeadDet &h : heads) {
            int best = -1;
            double bestV = 0;
            for (int c = 0; c < clusters.size(); ++c) {
                if (clusters[c].frames.contains(f)) continue;
                const double v = matchScore(h.box, clusters[c].mean());
                if (v > bestV) {
                    bestV = v;
                    best = c;
                }
            }
            if (best >= 0 && bestV > 0.35) {
                clusters[best].boxes << h.box;
                clusters[best].frames << f;
            } else {
                Cluster c;
                c.boxes << h.box;
                c.frames << f;
                clusters << c;
            }
        }
        ++processed;
    }
    if (m_cancel) {
        emit finished(req.id, false, 0, {}, QStringLiteral("Đã huỷ"));
        return;
    }

    const int need = std::max(1, int(std::ceil(req.ratio * processed)));
    QVector<Cluster> stable;
    std::sort(clusters.begin(), clusters.end(), [](const Cluster &a, const Cluster &b) { return a.frames.size() > b.frames.size(); });
    for (const Cluster &c : clusters) {
        if (c.frames.size() < need) continue;
        bool dup = false;
        for (const Cluster &k : stable)
            if (matchScore(c.mean(), k.mean()) > 0.5) {
                dup = true;
                break;
            }
        if (!dup) stable << c;
    }
    // Danh so theo hang (tren -> duoi), trong hang tu trai -> phai
    QVector<QRectF> boxes;
    for (const Cluster &c : stable) boxes << c.mean();
    const double rowH = frames.first().height() / 12.0;
    std::sort(boxes.begin(), boxes.end(), [rowH](const QRectF &a, const QRectF &b) {
        const int ra = int(a.center().y() / rowH), rb = int(b.center().y() / rowH);
        return ra != rb ? ra < rb : a.left() < b.left();
    });

    // Anh ket luan: khung giua cac khung da xu ly
    QImage out = frames[processed / 2].convertToFormat(QImage::Format_RGB32);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing);
        const double s = out.width() / 1920.0;
        QFont f("Segoe UI");
        f.setBold(true);
        f.setPixelSize(std::max(12, int(18 * s)));
        p.setFont(f);
        for (int i = 0; i < boxes.size(); ++i) {
            p.setPen(QPen(QColor(255, 45, 45), std::max(2.0, 3 * s)));
            p.setBrush(Qt::NoBrush);
            p.drawRect(boxes[i]);
            const QString n = QString::number(i + 1);
            const QPointF at(boxes[i].left() + 2, boxes[i].top() - 4);
            QPainterPath path;
            path.addText(at, f, n);
            p.setPen(QPen(Qt::black, std::max(2.0, 4 * s)));
            p.drawPath(path);
            p.fillPath(path, QColor(255, 235, 0));
        }
        QFont big = f;
        big.setPixelSize(int(54 * s));
        p.setFont(big);
        const QString label = QStringLiteral("Đếm được: %1").arg(boxes.size());
        const QRectF r(out.width() - 560 * s, 12 * s, 545 * s, 84 * s);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(160, 0, 20, 230));
        p.drawRoundedRect(r, 10 * s, 10 * s);
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter, label);
    }

    QStringList pf;
    for (int v : perFrame) pf << QString::number(v);
    QString msg = QStringLiteral("%1 người · %2 khung (từng khung: %3) · %4 giây")
                      .arg(boxes.size()).arg(processed).arg(pf.join(", ")).arg(total.elapsed() / 1000);
    if (timeUp) msg += QStringLiteral(" · hết 5 phút nên chỉ xử lý %1/%2 khung").arg(processed).arg(frames.size());
    emit progress(req.id, 100, QStringLiteral("Xong"));
    emit finished(req.id, true, int(boxes.size()), out, msg);
}
