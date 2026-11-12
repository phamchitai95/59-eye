#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

// Mot thiet bi: dau ghi Dahua (nhieu kenh) hoac camera le RTSP (1 kenh)
struct NvrConfig {
    QString id;               // ma co dinh (dung cho phan quyen, chinh hinh), tu sinh
    QString type = "dahua";   // "dahua" | "rtsp"
    QString name;
    QString host;
    int httpPort = 80;
    int rtspPort = 554;
    QString user;
    QString password;
    int channels = 16;
    QString mainUrl;          // chi cho camera RTSP
    QString subUrl;           // chi cho camera RTSP (rong = dung mainUrl)
    QSet<int> hidden;         // kenh bi tat trong app
    QStringList titles;       // ten kenh lay tu dau ghi (co the rong)

    bool isDahua() const { return type != "rtsp"; }
};

struct ChannelRef {
    int nvr = -1;
    int ch = 0; // 1-based nhu tren dau ghi Dahua
    bool valid() const { return nvr >= 0 && ch > 0; }
    bool operator==(const ChannelRef &o) const { return nvr == o.nvr && ch == o.ch; }
};

class AppConfig {
public:
    static AppConfig &instance();

    bool load(const QString &path, QString *error);
    bool save(QString *error = nullptr);
    QString path() const { return m_path; }

    QVector<NvrConfig> nvrs;
    int liveLatencyMs = 200;
    int playbackLatencyMs = 500;
    QString videoSink; // rong = tu chon
    bool liveQueueLeaky = false; // chi de so sanh voi cach cu
    QString updateUrl;           // nguon cap nhat OTA (rong = mac dinh luc build)

    QString channelTitle(const ChannelRef &r) const;
    QString shortTitle(const ChannelRef &r) const; // chi ten kenh, khong co ten dau ghi
    bool channelEnabled(const ChannelRef &r) const;
    QString channelKey(const ChannelRef &r) const; // "<id>/<kenh>" dung cho phan quyen
    int indexOfId(const QString &id) const;
    static QString newId();

    QString liveUrl(const ChannelRef &r, bool mainStream) const;
    QString playbackUrl(const ChannelRef &r, const QDateTime &start, const QDateTime &end,
                        bool mainStream) const;

private:
    QString m_path;
    QJsonObject m_root; // giu nguyen cac khoa khac khi ghi lai
};

inline AppConfig &cfg() { return AppConfig::instance(); }
