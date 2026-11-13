#include "HeadDetector.h"

#include <QCoreApplication>
#include <QDir>
#include <algorithm>
#include <cmath>
#include <vector>

#include "onnxruntime_c_api.h"

static constexpr int kInW = 640, kInH = 384;
static constexpr float kScoreThr = 0.30f;
static constexpr int kHeadClass = 1; // 0 = than nguoi, 1 = dau, 2 = ban tay

struct HeadDetector::Ort {
    const OrtApi *api = nullptr;
    OrtEnv *env = nullptr;
    OrtSessionOptions *opts = nullptr;
    OrtMemoryInfo *mem = nullptr;
    void *lib = nullptr;
};

static const OrtApi *loadOrt(void **libOut, QString *error)
{
    // onnxruntime.dll duoc lien ket luc build (nam canh file chay)
    *libOut = nullptr;
    const OrtApi *api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!api) {
        *error = QStringLiteral("Phiên bản onnxruntime không khớp (%1)").arg(QString::fromUtf8(OrtGetApiBase()->GetVersionString()));
        return nullptr;
    }
    return api;
}

static bool ok(const OrtApi *api, OrtStatus *st, QString *error)
{
    if (!st) return true;
    if (error) *error = QString::fromUtf8(api->GetErrorMessage(st));
    api->ReleaseStatus(st);
    return false;
}

HeadDetector::HeadDetector() = default;

HeadDetector::~HeadDetector()
{
    if (!m_ort) return;
    const OrtApi *a = m_ort->api;
    if (m_session) a->ReleaseSession(static_cast<OrtSession *>(m_session));
    if (m_ort->mem) a->ReleaseMemoryInfo(m_ort->mem);
    if (m_ort->opts) a->ReleaseSessionOptions(m_ort->opts);
    if (m_ort->env) a->ReleaseEnv(m_ort->env);
    delete m_ort;
}

bool HeadDetector::load(const QString &modelPath, QString *error)
{
    if (m_session) return true;
    m_ort = new Ort;
    m_ort->api = loadOrt(&m_ort->lib, error);
    if (!m_ort->api) return false;
    const OrtApi *a = m_ort->api;
    if (!ok(a, a->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "59eye", &m_ort->env), error)) return false;
    if (!ok(a, a->CreateSessionOptions(&m_ort->opts), error)) return false;
    a->SetSessionGraphOptimizationLevel(m_ort->opts, ORT_ENABLE_ALL);
    a->SetIntraOpNumThreads(m_ort->opts, 0); // 0 = dung moi nhan
    OrtSession *s = nullptr;
#ifdef _WIN32
    const std::wstring wp = QDir::toNativeSeparators(modelPath).toStdWString();
    if (!ok(a, a->CreateSession(m_ort->env, wp.c_str(), m_ort->opts, &s), error)) return false;
#else
    if (!ok(a, a->CreateSession(m_ort->env, modelPath.toUtf8().constData(), m_ort->opts, &s), error)) return false;
#endif
    m_session = s;
    return ok(a, a->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &m_ort->mem), error);
}

QVector<QRect> HeadDetector::regions(const QSize &s)
{
    // Ca anh (bat dau lon o hang dau) + manh chong lan 30% o 2 muc phong to (bat dau nho o day cuoi)
    QVector<QRect> r{QRect(QPoint(0, 0), s)};
    for (double div : {3.2, 4.5}) {
        const int tw = int(s.width() / div), th = int(s.width() / div * kInH / kInW);
        const int stepX = std::max(1, int(tw * 0.7)), stepY = std::max(1, int(th * 0.7));
        for (int y = 0; y < std::max(1, s.height() - th / 3); y += stepY)
            for (int x = 0; x < std::max(1, s.width() - tw / 3); x += stepX) {
                const QRect b(x, y, std::min(tw, s.width() - x), std::min(th, s.height() - y));
                if (b.width() >= tw / 2 && b.height() >= th / 2) r << b;
            }
    }
    return r;
}

int HeadDetector::passesFor(const QSize &s)
{
    return int(regions(s).size());
}

QVector<HeadDet> HeadDetector::run(const QImage &img, const QRect &region, QString *error)
{
    const OrtApi *a = m_ort->api;
    const QImage crop = img.copy(region).scaled(kInW, kInH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                            .convertToFormat(QImage::Format_RGB888);
    // BGR, CHW, gia tri 0..255 (dau vao cua YOLOX)
    std::vector<float> input(size_t(3) * kInW * kInH);
    float *b = input.data(), *g = b + kInW * kInH, *r = g + kInW * kInH;
    for (int y = 0; y < kInH; ++y) {
        const uchar *p = crop.constScanLine(y);
        for (int x = 0; x < kInW; ++x, p += 3) {
            const int i = y * kInW + x;
            r[i] = p[0];
            g[i] = p[1];
            b[i] = p[2];
        }
    }
    const int64_t shape[4] = {1, 3, kInH, kInW};
    OrtValue *in = nullptr, *out = nullptr;
    if (!ok(a, a->CreateTensorWithDataAsOrtValue(m_ort->mem, input.data(), input.size() * sizeof(float), shape, 4,
                                                 ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in), error))
        return {};
    const char *inNames[] = {"input"};
    const char *outNames[] = {"batchno_classid_score_x1y1x2y2"};
    const bool ran = ok(a, a->Run(static_cast<OrtSession *>(m_session), nullptr, inNames, &in, 1, outNames, 1, &out), error);
    a->ReleaseValue(in);
    if (!ran) return {};

    QVector<HeadDet> res;
    OrtTensorTypeAndShapeInfo *info = nullptr;
    if (ok(a, a->GetTensorTypeAndShape(out, &info), error)) {
        size_t dims = 0;
        a->GetDimensionsCount(info, &dims);
        std::vector<int64_t> d(dims);
        a->GetDimensions(info, d.data(), dims);
        a->ReleaseTensorTypeAndShapeInfo(info);
        float *data = nullptr;
        if (dims == 2 && d[1] == 7 && ok(a, a->GetTensorMutableData(out, reinterpret_cast<void **>(&data)), error)) {
            const double sx = double(region.width()) / kInW, sy = double(region.height()) / kInH;
            for (int64_t i = 0; i < d[0]; ++i) {
                const float *row = data + i * 7; // batch, lop, diem, x1, y1, x2, y2
                if (int(row[1]) != kHeadClass || row[2] < kScoreThr) continue;
                HeadDet h;
                h.score = row[2];
                h.box = QRectF(QPointF(region.x() + row[3] * sx, region.y() + row[4] * sy),
                               QPointF(region.x() + row[5] * sx, region.y() + row[6] * sy));
                res << h;
            }
        }
    }
    a->ReleaseValue(out);
    return res;
}

QVector<HeadDet> HeadDetector::detect(const QImage &img, const std::function<bool()> &onPass, QString *error)
{
    QVector<HeadDet> all;
    for (const QRect &r : regions(img.size())) {
        QString e;
        all += run(img, r, &e);
        if (!e.isEmpty()) {
            if (error) *error = e;
            return {};
        }
        if (onPass && !onPass()) return {};
    }
    return mergeHeads(all);
}

static void overlap(const QRectF &a, const QRectF &b, double *iou, double *inMin)
{
    const QRectF i = a.intersected(b);
    const double inter = i.isValid() ? i.width() * i.height() : 0;
    const double a1 = a.width() * a.height(), a2 = b.width() * b.height();
    *iou = inter / (a1 + a2 - inter + 1e-6);
    *inMin = inter / (std::min(a1, a2) + 1e-6);
}

QVector<HeadDet> mergeHeads(QVector<HeadDet> dets)
{
    std::sort(dets.begin(), dets.end(), [](const HeadDet &x, const HeadDet &y) { return x.score > y.score; });
    QVector<HeadDet> keep;
    for (const HeadDet &d : dets) {
        bool dup = false;
        for (const HeadDet &k : keep) {
            double iou, inMin;
            overlap(d.box, k.box, &iou, &inMin);
            if (iou > 0.45 || inMin > 0.7) {
                dup = true;
                break;
            }
        }
        if (!dup) keep << d;
    }
    return keep;
}
