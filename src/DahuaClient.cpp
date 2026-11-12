#include "DahuaClient.h"
#include "AppInfo.h"

#include <QAuthenticator>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QMap>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <memory>

static QMap<QString, QString> parseKv(const QByteArray &body)
{
    QMap<QString, QString> m;
    for (const QByteArray &raw : body.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        const int eq = line.indexOf('=');
        if (eq > 0) m.insert(line.left(eq), line.mid(eq + 1));
    }
    return m;
}

static QString cgiTime(const QDateTime &t)
{
    return t.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

DahuaClient::DahuaClient(int nvrIndex, QObject *parent)
    : QObject(parent), m_nvr(nvrIndex), m_conf(cfg().nvrs[nvrIndex])
{
}

DahuaClient::DahuaClient(const QString &host, int httpPort, const QString &user, const QString &password,
                         QObject *parent)
    : QObject(parent)
{
    m_conf.host = host;
    m_conf.httpPort = httpPort;
    m_conf.user = user;
    m_conf.password = password;
}

void DahuaClient::get(const QString &pathAndQuery, Done cb)
{
    QUrl url(QStringLiteral("http://%1:%2%3").arg(nvr().host).arg(nvr().httpPort).arg(pathAndQuery));
    QNetworkRequest req(url);
    req.setTransferTimeout(10000);
    // Dau ghi Dahua huy nonce Digest sau moi lan dung; Qt lai gui nonce cu tu cache roi coi cung mat khau
    // la "da that bai" -> 401 o yeu cau thu hai. Moi yeu cau dung 1 QNetworkAccessManager rieng
    // (khong cache, khong lam dut yeu cau khac dang chay song song).
    auto *nam = new QNetworkAccessManager(this);
    connect(nam, &QNetworkAccessManager::authenticationRequired, this,
            [this](QNetworkReply *reply, QAuthenticator *auth) {
                // Neu da thu ma van bi hoi lai -> sai mat khau, de Qt tra loi 401.
                if (reply->property("authTried").toBool()) return;
                reply->setProperty("authTried", true);
                auth->setUser(nvr().user);
                auth->setPassword(nvr().password);
            });
    QNetworkReply *reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, nam, cb] {
        nam->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        // Ghi log HTTP de chan doan khi dat bien LITEPSS_HTTP_LOG=1 (khong ghi mat khau)
        if (qEnvironmentVariableIsSet("LITEPSS_HTTP_LOG")) {
            QFile f(AppInfo::dataFile("http.log"));
            if (f.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream ts(&f);
                ts << QDateTime::currentDateTime().toString(Qt::ISODate) << " " << status << " "
                   << reply->url().toString(QUrl::RemoveUserInfo) << "\n  auth-tried="
                   << reply->property("authTried").toBool() << " www-auth="
                   << reply->rawHeader("WWW-Authenticate") << "\n  body=" << body.left(300).simplified() << "\n";
            }
        }
        QString err;
        if (reply->error() != QNetworkReply::NoError) {
            err = (status == 401) ? QStringLiteral("Sai tài khoản/mật khẩu (HTTP 401)")
                                  : reply->errorString();
        }
        reply->deleteLater();
        cb(status, body, err);
    });
}

// ---------------- Tim ban ghi: mediaFileFind.cgi ----------------

struct DahuaClient::FindJob {
    int channel = 1;
    QDateTime from, to;
    QString object;
    QVector<RecordSegment> segs;
    RecordsCb cb;
    int pages = 0;
};

void DahuaClient::findRecords(int channel, const QDateTime &from, const QDateTime &to, RecordsCb cb)
{
    auto job = std::make_shared<FindJob>();
    job->channel = channel;
    job->from = from;
    job->to = to;
    job->cb = std::move(cb);

    get(QStringLiteral("/cgi-bin/mediaFileFind.cgi?action=factory.create"),
        [this, job](int, const QByteArray &body, const QString &err) {
            if (!err.isEmpty()) return job->cb({}, err);
            job->object = parseKv(body).value("result");
            if (job->object.isEmpty())
                return job->cb({}, QStringLiteral("Đầu ghi không trả objectId: %1")
                                       .arg(QString::fromUtf8(body.left(200))));

            QUrl q(QStringLiteral("/cgi-bin/mediaFileFind.cgi"));
            const QString query =
                QStringLiteral("action=findFile&object=%1&condition.Channel=%2"
                               "&condition.StartTime=%3&condition.EndTime=%4&condition.Types[0]=dav")
                    .arg(job->object).arg(job->channel)
                    .arg(QString::fromUtf8(QUrl::toPercentEncoding(cgiTime(job->from), ":")),
                         QString::fromUtf8(QUrl::toPercentEncoding(cgiTime(job->to), ":")));
            get(q.path() + "?" + query, [this, job](int, const QByteArray &b, const QString &e) {
                if (!e.isEmpty() || !b.trimmed().startsWith("OK")) {
                    // Nhieu firmware tra "Error" khi khong co ban ghi nao trong khoang thoi gian.
                    finishFind(job, e);
                    return;
                }
                findNext(job);
            });
        });
}

void DahuaClient::findNext(std::shared_ptr<FindJob> job)
{
    const int count = 100;
    get(QStringLiteral("/cgi-bin/mediaFileFind.cgi?action=findNextFile&object=%1&count=%2")
            .arg(job->object).arg(count),
        [this, job, count](int, const QByteArray &body, const QString &err) {
            if (!err.isEmpty()) return finishFind(job, err);
            const auto kv = parseKv(body);
            const int found = kv.value("found").toInt();
            for (int i = 0; i < found; ++i) {
                const QString p = QStringLiteral("items[%1].").arg(i);
                RecordSegment s;
                s.start = QDateTime::fromString(kv.value(p + "StartTime"), "yyyy-MM-dd HH:mm:ss");
                s.end = QDateTime::fromString(kv.value(p + "EndTime"), "yyyy-MM-dd HH:mm:ss");
                for (auto it = kv.begin(); it != kv.end(); ++it) {
                    if (it.key().startsWith(p + "Flags[") || it.key().startsWith(p + "Events[")) {
                        const QString v = it.value();
                        if (v.compare("Timing", Qt::CaseInsensitive) && !v.isEmpty()) s.event = true;
                    }
                }
                if (s.start.isValid() && s.end.isValid() && s.end > s.start) job->segs.push_back(s);
            }
            ++job->pages;
            if (found >= count && job->pages < 50) findNext(job);
            else finishFind(job, {});
        });
}

void DahuaClient::finishFind(std::shared_ptr<FindJob> job, const QString &err)
{
    if (!job->object.isEmpty()) {
        const QString obj = job->object;
        get(QStringLiteral("/cgi-bin/mediaFileFind.cgi?action=close&object=%1").arg(obj),
            [this, obj](int, const QByteArray &, const QString &) {
                get(QStringLiteral("/cgi-bin/mediaFileFind.cgi?action=destroy&object=%1").arg(obj),
                    [](int, const QByteArray &, const QString &) {});
            });
    }
    std::sort(job->segs.begin(), job->segs.end(),
              [](const RecordSegment &a, const RecordSegment &b) { return a.start < b.start; });
    job->cb(job->segs, err);
}

// ---------------- Ten kenh: configManager.cgi ----------------

void DahuaClient::fetchChannelTitles(TitlesCb cb)
{
    get(QStringLiteral("/cgi-bin/configManager.cgi?action=getConfig&name=ChannelTitle"),
        [cb](int, const QByteArray &body, const QString &err) {
            if (!err.isEmpty()) return cb({}, err);
            static const QRegularExpression re(QStringLiteral("ChannelTitle\\[(\\d+)\\]\\.Name$"));
            QStringList titles;
            const auto kv = parseKv(body);
            for (auto it = kv.begin(); it != kv.end(); ++it) {
                const auto m = re.match(it.key());
                if (!m.hasMatch()) continue;
                const int idx = m.captured(1).toInt();
                while (titles.size() <= idx) titles << QString();
                titles[idx] = it.value();
            }
            cb(titles, {});
        });
}
