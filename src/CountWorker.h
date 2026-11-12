#pragma once
#include <QImage>
#include <QObject>
#include <QVector>
#include <atomic>

class HeadDetector;

// Tham so 1 lan dem
struct CountRequest {
    int id = 0;
    QString title;
    QString url, user, password; // RTSP luong chinh
    QString modelPath;
    int frames = 8;              // so khung lay trong khoang captureMs
    int captureMs = 30000;
    int budgetMs = 5 * 60 * 1000; // toi da 5 phut cho ca lan dem
    double ratio = 0.4;          // 1 vi tri duoc tinh neu co dau o >= ratio so khung
};

// Chay tren QThread rieng: lay khung tu camera, nhan dien dau tung khung, dong thuan nhieu khung.
class CountWorker : public QObject {
    Q_OBJECT
public:
    explicit CountWorker(QObject *parent = nullptr);
    ~CountWorker() override;
    void cancel() { m_cancel = true; }

public slots:
    void process(const CountRequest &req);

signals:
    void progress(int id, int percent, const QString &step);
    void finished(int id, bool ok, int count, const QImage &result, const QString &message);

private:
    bool capture(const CountRequest &req, QVector<QImage> *frames, QString *error);
    std::atomic<bool> m_cancel{false};
    HeadDetector *m_detector = nullptr;
    QString m_loadedModel;
};

Q_DECLARE_METATYPE(CountRequest)
