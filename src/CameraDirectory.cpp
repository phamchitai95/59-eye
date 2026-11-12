#include "CameraDirectory.h"
#include "DahuaClient.h"

#include <QMap>
#include <QRegularExpression>

CameraDirectory &CameraDirectory::instance()
{
    static CameraDirectory d;
    return d;
}

void CameraDirectory::load(int nvr, DahuaClient *client, std::function<void(const QString &)> done)
{
    client->get(QStringLiteral("/cgi-bin/configManager.cgi?action=getConfig&name=RemoteDevice"),
                [this, nvr, done](int, const QByteArray &body, const QString &err) {
        if (!err.isEmpty()) {
            if (done) done(err);
            return;
        }
        // table.RemoteDevice.uuid:System_CONFIG_NETCAMERA_INFO_<n>.<Field>=<value>
        static const QRegularExpression re(QStringLiteral("NETCAMERA_INFO_(\\d+)\\.([A-Za-z\\[\\]0-9\\.]+)=(.*)$"));
        QMap<int, CameraInfo> map;
        for (const QByteArray &raw : body.split('\n')) {
            const auto m = re.match(QString::fromUtf8(raw).trimmed());
            if (!m.hasMatch()) continue;
            CameraInfo &c = map[m.captured(1).toInt()];
            const QString k = m.captured(2), v = m.captured(3);
            if (k == "Address") c.address = v;
            else if (k == "HttpPort") c.httpPort = v.toInt();
            else if (k == "UserName") c.user = v;
            else if (k == "Password") c.password = v;
            else if (k == "ProtocolType") c.protocol = v;
            else if (k == "DeviceType") c.deviceType = v;
            else if (k == "Enable" && v != "true") c.address.clear();
        }
        QVector<CameraInfo> list;
        for (auto it = map.begin(); it != map.end(); ++it) {
            while (list.size() < it.key()) list.push_back({});
            CameraInfo c = it.value();
            if (c.address == "192.168.0.0" || c.address == "0.0.0.0") c.address.clear();
            if (c.httpPort <= 0) c.httpPort = 80;
            list.push_back(c);
        }
        m_byNvr[nvr] = list;
        emit loaded(nvr);
        if (done) done({});
    });
}

CameraInfo CameraDirectory::camera(const ChannelRef &r) const
{
    const auto it = m_byNvr.constFind(r.nvr);
    if (it == m_byNvr.constEnd() || r.ch - 1 < 0 || r.ch - 1 >= it->size()) return {};
    return it->at(r.ch - 1);
}
