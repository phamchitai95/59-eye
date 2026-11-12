#pragma once
#include "Config.h"

#include <QHash>
#include <QObject>
#include <functional>

// Bat / tat / tu dong den tren camera (den trang, den "Lamp"), noi thang toi camera:
//  - Camera Dahua: configManager setConfig Lighting_V2 (WhiteLight) cho ca 3 profile ngay/dem/thuong
//  - Camera ONVIF co lenh phu PTZ "tt:Lamp|On/Off" (TR38Y51, K7T1S4...)
//  - Camera ONVIF chi co IrCutFilter: ep che do dem (OFF) / ngay (ON) / AUTO
class LightController : public QObject {
    Q_OBJECT
public:
    enum Action { On, Off, Auto };
    struct Caps {
        bool checked = false;
        QString method;          // "dahua", "onvif-lamp", "onvif-ircut", "" = khong ho tro
        bool canAuto = false;
        QString reason;          // ly do khong ho tro
        QString profileToken;    // ONVIF media profile (cho PTZ)
        QString vsToken;         // ONVIF video source (cho Imaging)
        QString ptzUrl, imagingUrl;
    };

    static LightController &instance();

    // Do kha nang (co cache). cb chay tren luong GUI.
    void detect(const ChannelRef &r, std::function<void(const Caps &)> cb);
    void apply(const ChannelRef &r, Action a, std::function<void(bool ok, const QString &msg)> cb);

    void clear() { m_cache.clear(); }

private:
    QHash<QString, Caps> m_cache;
};
