#pragma once
#include "Config.h"

#include <QHash>
#include <QObject>
#include <functional>

class DahuaClient;

// Thong tin camera that sau 1 kenh cua dau ghi, lay tu cau hinh RemoteDevice cua NVR.
// Mat khau chi giu trong bo nho, khong ghi ra dia / log.
struct CameraInfo {
    QString address;
    int httpPort = 80;
    QString user;
    QString password;
    QString protocol;   // "Onvif", "Dahua2", ...
    QString deviceType;
    int remoteChannel = 0; // kenh tren camera/XVR (0-based)
    bool valid() const { return !address.isEmpty(); }
    bool isOnvif() const { return protocol.compare("Onvif", Qt::CaseInsensitive) == 0; }
    bool isDahua() const { return protocol.startsWith("Dahua", Qt::CaseInsensitive) || protocol.compare("Private", Qt::CaseInsensitive) == 0; }
};

class CameraDirectory : public QObject {
    Q_OBJECT
public:
    static CameraDirectory &instance();
    // Tai danh sach camera cua 1 NVR (goi 1 lan khi khoi dong)
    void load(int nvr, DahuaClient *client, std::function<void(const QString &err)> done = {});
    CameraInfo camera(const ChannelRef &r) const;
    void clear() { m_byNvr.clear(); }

signals:
    void loaded(int nvr);

private:
    QHash<int, QVector<CameraInfo>> m_byNvr; // nvr -> kenh (0-based) -> camera
};
