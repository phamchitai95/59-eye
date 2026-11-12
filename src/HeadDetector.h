#pragma once
#include <QImage>
#include <QRectF>
#include <QString>
#include <QVector>
#include <functional>

// Nhan dien dau nguoi bang mo hinh YOLOX Body-Head-Hand (Apache 2.0) qua ONNX Runtime.
// ONNX Runtime duoc nap luc chay (onnxruntime.dll canh file chay) qua C API -> khong can thu vien link.
struct HeadDet {
    float score = 0;
    QRectF box;
};

class HeadDetector {
public:
    HeadDetector();
    ~HeadDetector();
    bool load(const QString &modelPath, QString *error);
    bool isLoaded() const { return m_session != nullptr; }

    // So lan chay mo hinh (ca anh + cac manh) cho 1 anh kich thuoc s
    static int passesFor(const QSize &s);
    // Tim dau tren ca anh (ca anh + manh chong lan 2 muc phong to), gop trung.
    // onPass(): goi sau moi lan chay; tra false de huy.
    QVector<HeadDet> detect(const QImage &img, const std::function<bool()> &onPass, QString *error);

private:
    QVector<HeadDet> run(const QImage &img, const QRect &region, QString *error);
    static QVector<QRect> regions(const QSize &s);

    struct Ort;
    Ort *m_ort = nullptr;
    void *m_session = nullptr;
};

// Gop trung (NMS + loai o nam gon trong o khac)
QVector<HeadDet> mergeHeads(QVector<HeadDet> dets);
