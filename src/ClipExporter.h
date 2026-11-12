#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;

// Trich doan ban ghi ra MP4 khong nen lai video:
// rtspsrc (cam/playback, TCP) -> depay -> h264parse/h265parse -> mp4mux -> filesink.
// Am thanh G.711 duoc doi sang AAC (MP4 khong chua G.711). Tai nhanh bang "Rate-Control: no" (~6x).
class ClipExporter : public QObject {
    Q_OBJECT
public:
    struct Shared;
    explicit ClipExporter(QObject *parent = nullptr);
    ~ClipExporter() override;

    bool start(const QString &rtspUrl, const QString &user, const QString &password, qint64 durationMs,
               const QString &outFile, bool audio);
    void stopAndSave();   // dung som, van ghi file MP4 hop le voi phan da co
    bool isRunning() const { return m_pipe != nullptr; }

signals:
    void progress(qint64 doneMs, qint64 totalMs, qint64 bytes);
    void finished(bool ok, const QString &message);

private:
    void poll();
    void sendEos();
    void finish(bool ok, const QString &msg);

    Shared *m_sh = nullptr;
    GstElement *m_pipe = nullptr;
    GstBus *m_bus = nullptr;
    QTimer m_timer;
    QString m_out;
    qint64 m_duration = 0;
    bool m_eosSent = false;
    QElapsedTimer m_eosClock;
    QElapsedTimer m_stallClock;
    QElapsedTimer m_videoClock;
    qint64 m_lastDone = -1;
};
