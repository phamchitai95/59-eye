#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;

struct StreamParams {
    QString url;
    QString user;
    QString password;
    int latencyMs = 200;
    bool live = true;   // live: queue leaky de luon bam sat thoi gian thuc
    bool sync = true;   // sink co dong bo theo timestamp hay khong
    bool leaky = false; // queue vut khung khi day (cach cu, de so sanh)
    bool audio = false; // phat am thanh cua camera
    double speed = 1.0; // xem lai tua nhanh: dau ghi tu gui nhanh (Speed/Scale), sink khong dong bo
};

// Chinh mau tren GPU (GstColorBalance cua d3d12videosink)
struct ColorAdjust {
    double brightness = 0.0; // -1..1
    double contrast = 1.0;   // 0..2
    double saturation = 1.0; // 0..2
    double hue = 0.0;        // -1..1
};

// Mot pipeline: rtspsrc (TCP) -> decodebin (tu chon giai ma GPU) -> queue -> video sink
// ve thang len cua so native (GstVideoOverlay).
class GstPlayer : public QObject {
    Q_OBJECT
public:
    explicit GstPlayer(QObject *parent = nullptr);
    ~GstPlayer() override;

    bool open(const StreamParams &p, quintptr windowHandle);
    void stop();
    bool isOpen() const { return m_pipe != nullptr; }
    bool isPlaying() const { return m_playing; }

    void setPaused(bool paused);
    bool setRate(double rate);       // gui Scale qua RTSP (seek voi rate)
    void setSync(bool sync);
    void setColor(const ColorAdjust &c);
    ColorAdjust color() const { return m_color; }
    bool colorSupported() const;
    bool hasAudioStream() const;  // camera co gui tieng
    bool audioActive() const;     // dang phat tieng
    QString audioCodec() const;
    qint64 positionMs() const;       // -1 neu khong truy van duoc
    qint64 mediaPositionMs() const;  // thoi gian ban ghi da nhan (theo moc RTP video), -1 neu chua co
    void expose();

    QString decoderName() const;
    QString sinkName() const { return m_sinkName; }
    QString resolution() const;
    double kbps() const { return m_kbps; }
    double fps() const { return m_fps; }
    quint64 droppedFrames() const { return m_dropped; }
    bool hasRendered() const;     // da ve it nhat 1 khung (dung de doi luong khong bi den man hinh)

    static double totalKbps();
    static int activeCount();
    static QString statsSummary();
    static void setGlobalVolume(double v); // 0..1.5, ap dung ngay cho o dang phat tieng
    static double globalVolume() { return s_volume; }

    struct Shared; // du lieu dung chung voi callback cua GStreamer

signals:
    void playing();
    void error(const QString &msg);
    void endOfStream();
    void videoClicked(bool doubleClick);
    void videoContextMenu();      // chuot phai tren vung video

private:
    void pollBus();
    void updateRate();
    void applyColor();
    ColorAdjust m_color;

    Shared *m_sh = nullptr;
    GstElement *m_pipe = nullptr;
    GstElement *m_sink = nullptr;
    GstBus *m_bus = nullptr;
    QString m_sinkName;
    bool m_playing = false;
    QTimer m_busTimer;
    QTimer m_rateTimer;
    quint64 m_lastBytes = 0;
    QElapsedTimer m_rateClock;
    double m_kbps = 0;
    double m_fps = 0;
    quint64 m_rendered = 0, m_dropped = 0, m_lastRendered = 0;
    QElapsedTimer m_sessionClock;
    QElapsedTimer m_lastClick;

    static QSet<GstPlayer *> s_all;
    static double s_volume;
};
