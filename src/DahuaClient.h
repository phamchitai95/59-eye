#pragma once
#include "Config.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <functional>

struct RecordSegment {
    QDateTime start;
    QDateTime end;
    bool event = false; // ghi theo su kien (chuyen dong / bao dong)
};

// Goi HTTP API (CGI) cua dau ghi Dahua, xac thuc Digest. Khong dung SDK.
class DahuaClient : public QObject {
    Q_OBJECT
public:
    DahuaClient(int nvrIndex, QObject *parent = nullptr);
    // Ket noi thang toi 1 thiet bi Dahua bat ky (vd camera sau dau ghi)
    DahuaClient(const QString &host, int httpPort, const QString &user, const QString &password,
                QObject *parent = nullptr);

    using RecordsCb = std::function<void(const QVector<RecordSegment> &, const QString &error)>;
    void findRecords(int channel, const QDateTime &from, const QDateTime &to, RecordsCb cb);

    using TitlesCb = std::function<void(const QStringList &titles, const QString &error)>;
    void fetchChannelTitles(TitlesCb cb);

    // GET bat ky duong dan CGI (co xac thuc Digest)
    using Done = std::function<void(int httpStatus, const QByteArray &body, const QString &err)>;
    void get(const QString &pathAndQuery, Done cb);

private:
    const NvrConfig &nvr() const { return m_conf; }

    struct FindJob;
    void findNext(std::shared_ptr<FindJob> job);
    void finishFind(std::shared_ptr<FindJob> job, const QString &err);

    int m_nvr = -1;
    NvrConfig m_conf;
};
