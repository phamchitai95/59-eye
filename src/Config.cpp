#include "Config.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>

AppConfig &AppConfig::instance()
{
    static AppConfig c;
    return c;
}

QString AppConfig::newId()
{
    return QUuid::createUuid().toString(QUuid::Id128).left(8);
}

bool AppConfig::load(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Không mở được %1").arg(path);
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    f.close(); // Windows khong cho ghi de (QSaveFile) file dang mo
    if (pe.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("Lỗi JSON: %1 (vị trí %2)").arg(pe.errorString()).arg(pe.offset);
        return false;
    }
    m_root = doc.object();
    nvrs.clear();
    bool needSave = false;
    for (const QJsonValue &v : m_root.value("nvrs").toArray()) {
        const QJsonObject o = v.toObject();
        NvrConfig n;
        n.id = o.value("id").toString();
        n.type = o.value("type").toString("dahua");
        n.name = o.value("name").toString();
        n.host = o.value("host").toString();
        n.httpPort = o.value("httpPort").toInt(80);
        n.rtspPort = o.value("rtspPort").toInt(554);
        n.user = o.value("user").toString();
        n.password = o.value("password").toString();
        n.channels = o.value("channels").toInt(16);
        n.mainUrl = o.value("mainUrl").toString();
        n.subUrl = o.value("subUrl").toString();
        for (const QJsonValue &h : o.value("hiddenChannels").toArray()) n.hidden.insert(h.toInt());
        if (!n.isDahua()) {
            n.channels = 1;
            if (n.mainUrl.isEmpty()) continue;
            if (n.name.isEmpty()) n.name = QStringLiteral("Camera");
        } else {
            if (n.host.isEmpty()) continue;
            if (n.name.isEmpty()) n.name = n.host;
        }
        if (n.id.isEmpty()) {
            n.id = newId();
            needSave = true;
        }
        nvrs.push_back(n);
    }
    // Bo dem jitter xem truc tiep. 200 ms (mac dinh cu) khong du: khung chinh (I) cua nhieu camera den tre
    // 300-460 ms moi ~8 giay -> hinh khung lai. Do tren DH-NVR5232: 500 ms thi gan nhu khong con khung tre.
    // Gia tri 200 luu trong config.json la mac dinh cu (khong phai nguoi dung chon) -> nang len 500.
    liveLatencyMs = m_root.value("liveLatencyMs").toInt(500);
    if (liveLatencyMs == 200) liveLatencyMs = 500;
    playbackLatencyMs = m_root.value("playbackLatencyMs").toInt(500);
    videoSink = m_root.value("videoSink").toString();
    liveQueueLeaky = m_root.value("liveQueueLeaky").toBool(false);
    updateUrl = m_root.value("updateUrl").toString();
    m_path = path;
    // Ma thiet bi phai co dinh giua cac lan chay (phan quyen dua vao no) -> ghi lai ngay
    if (needSave) save();
    return true;
}

bool AppConfig::save(QString *error)
{
    if (m_path.isEmpty()) return false;
    // Ban sao luu lan dau tien ung dung ghi de file nguoi dung tu viet
    const QString bak = m_path + ".bak";
    if (!QFile::exists(bak)) QFile::copy(m_path, bak);

    QJsonArray arr;
    for (const NvrConfig &n : nvrs) {
        QJsonObject o;
        o["id"] = n.id;
        o["type"] = n.type;
        o["name"] = n.name;
        o["user"] = n.user;
        o["password"] = n.password;
        if (n.isDahua()) {
            o["host"] = n.host;
            o["httpPort"] = n.httpPort;
            o["rtspPort"] = n.rtspPort;
            o["channels"] = n.channels;
        } else {
            o["mainUrl"] = n.mainUrl;
            if (!n.subUrl.isEmpty()) o["subUrl"] = n.subUrl;
        }
        if (!n.hidden.isEmpty()) {
            QList<int> h = n.hidden.values();
            std::sort(h.begin(), h.end());
            QJsonArray ha;
            for (int c : h) ha.append(c);
            o["hiddenChannels"] = ha;
        }
        arr.append(o);
    }
    m_root["nvrs"] = arr;
    m_root["liveLatencyMs"] = liveLatencyMs;
    m_root["playbackLatencyMs"] = playbackLatencyMs;
    m_root["videoSink"] = videoSink;

    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(m_root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

int AppConfig::indexOfId(const QString &id) const
{
    for (int i = 0; i < nvrs.size(); ++i)
        if (nvrs[i].id == id) return i;
    return -1;
}

QString AppConfig::channelKey(const ChannelRef &r) const
{
    if (r.nvr < 0 || r.nvr >= nvrs.size()) return {};
    return QStringLiteral("%1/%2").arg(nvrs[r.nvr].id).arg(r.ch);
}

bool AppConfig::channelEnabled(const ChannelRef &r) const
{
    if (r.nvr < 0 || r.nvr >= nvrs.size()) return false;
    const NvrConfig &n = nvrs[r.nvr];
    return r.ch >= 1 && r.ch <= n.channels && !n.hidden.contains(r.ch);
}

QString AppConfig::shortTitle(const ChannelRef &r) const
{
    if (r.nvr < 0 || r.nvr >= nvrs.size()) return {};
    const NvrConfig &n = nvrs[r.nvr];
    if (!n.isDahua()) return n.name;
    QString t = (r.ch - 1 < n.titles.size()) ? n.titles[r.ch - 1] : QString();
    if (t.isEmpty()) t = QStringLiteral("Kênh %1").arg(r.ch);
    return t;
}

QString AppConfig::channelTitle(const ChannelRef &r) const
{
    if (r.nvr < 0 || r.nvr >= nvrs.size()) return {};
    const NvrConfig &n = nvrs[r.nvr];
    if (!n.isDahua()) return n.name;
    return QStringLiteral("%1 · %2").arg(n.name, shortTitle(r));
}

QString AppConfig::liveUrl(const ChannelRef &r, bool mainStream) const
{
    const NvrConfig &n = nvrs[r.nvr];
    if (!n.isDahua()) return (mainStream || n.subUrl.isEmpty()) ? n.mainUrl : n.subUrl;
    return QStringLiteral("rtsp://%1:%2/cam/realmonitor?channel=%3&subtype=%4")
        .arg(n.host).arg(n.rtspPort).arg(r.ch).arg(mainStream ? 0 : 1);
}

static QString dahuaTime(const QDateTime &t)
{
    return t.toString(QStringLiteral("yyyy_MM_dd_HH_mm_ss"));
}

QString AppConfig::playbackUrl(const ChannelRef &r, const QDateTime &start, const QDateTime &end,
                               bool mainStream) const
{
    const NvrConfig &n = nvrs[r.nvr];
    return QStringLiteral("rtsp://%1:%2/cam/playback?channel=%3&subtype=%4&starttime=%5&endtime=%6")
        .arg(n.host).arg(n.rtspPort).arg(r.ch).arg(mainStream ? 0 : 1)
        .arg(dahuaTime(start), dahuaTime(end));
}
