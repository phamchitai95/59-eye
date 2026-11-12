#include "LightController.h"
#include "CameraDirectory.h"
#include "DahuaClient.h"
#include "OnvifClient.h"

#include <QXmlStreamReader>

static QString friendly(const QString &err)
{
    if (err.contains("canceled", Qt::CaseInsensitive) || err.contains("timed out", Qt::CaseInsensitive))
        return QStringLiteral("Camera không phản hồi (có thể đang tắt hoặc mất mạng)");
    if (err.contains("Not Found", Qt::CaseInsensitive) || err.contains("Bad Request", Qt::CaseInsensitive))
        return QStringLiteral("Thiết bị này không hỗ trợ điều khiển đèn");
    if (err.contains("401") || err.contains("NotAuthorized", Qt::CaseInsensitive))
        return QStringLiteral("Sai tài khoản camera");
    return err;
}

static QString cacheKey(const ChannelRef &r)
{
    return QStringLiteral("%1/%2").arg(r.nvr).arg(r.ch);
}

LightController &LightController::instance()
{
    static LightController c;
    return c;
}

void LightController::detect(const ChannelRef &r, std::function<void(const Caps &)> cb)
{
    const QString key = cacheKey(r);
    if (m_cache.contains(key)) return cb(m_cache.value(key));

    const CameraInfo cam = CameraDirectory::instance().camera(r);
    auto finish = [this, key, cb](Caps c) {
        c.checked = true;
        m_cache.insert(key, c);
        cb(c);
    };
    if (!cam.valid()) {
        Caps c;
        c.reason = QStringLiteral("Chưa lấy được thông tin camera từ đầu ghi");
        // khong cache: co the danh sach camera chua tai xong
        return cb(c);
    }

    if (cam.isDahua()) {
        auto *d = new DahuaClient(cam.address, cam.httpPort, cam.user, cam.password, this);
        d->get(QStringLiteral("/cgi-bin/configManager.cgi?action=getConfig&name=Lighting_V2"),
               [d, finish](int, const QByteArray &body, const QString &err) {
            d->deleteLater();
            Caps c;
            if (err.isEmpty() && body.contains("Lighting_V2")) {
                c.method = "dahua";
                c.canAuto = true;
            } else {
                c.reason = err.isEmpty() ? QStringLiteral("Camera không có cấu hình đèn") : friendly(err);
            }
            finish(c);
        });
        return;
    }

    if (!cam.isOnvif()) {
        Caps c;
        c.reason = QStringLiteral("Giao thức %1 chưa hỗ trợ điều khiển đèn").arg(cam.protocol);
        return finish(c);
    }

    auto *o = new OnvifClient(cam.address, cam.httpPort, cam.user, cam.password, this);
    o->discover([this, o, finish](const QString &err) {
        if (!err.isEmpty()) {
            o->deleteLater();
            Caps c;
            c.reason = friendly(err);
            return finish(c);
        }
        auto caps = std::make_shared<Caps>();
        caps->vsToken = o->videoSourceToken();
        caps->ptzUrl = o->service("PTZ");
        caps->imagingUrl = o->service("Imaging");

        // 1) che do ngay/dem (IrCutFilter) co ho tro AUTO khong
        auto afterIrCut = [this, o, caps, finish] {
            if (caps->ptzUrl.isEmpty()) {
                o->deleteLater();
                if (caps->method.isEmpty()) caps->reason = QStringLiteral("Camera không khai báo lệnh đèn");
                return finish(*caps);
            }
            // 2) lenh phu PTZ tt:Lamp
            o->call(caps->ptzUrl, "<tptz:GetNodes xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\"/>",
                    [this, o, caps, finish](bool ok, const QByteArray &x, const QString &) {
                const QStringList aux = ok ? OnvifClient::values(x, "AuxiliaryCommands") : QStringList();
                const bool lamp = aux.contains("tt:Lamp|On", Qt::CaseInsensitive);
                if (!lamp) {
                    o->deleteLater();
                    if (caps->method.isEmpty()) caps->reason = QStringLiteral("Camera không khai báo lệnh đèn");
                    return finish(*caps);
                }
                // can token cua media profile de gui lenh PTZ
                o->call(o->service("Media"), "<trt:GetProfiles/>", [o, caps, finish](bool ok2, const QByteArray &x2, const QString &) {
                    o->deleteLater();
                    if (ok2) {
                        QXmlStreamReader r(x2);
                        while (!r.atEnd()) {
                            if (r.readNext() == QXmlStreamReader::StartElement && r.name() == QLatin1String("Profiles")) {
                                caps->profileToken = r.attributes().value("token").toString();
                                break;
                            }
                        }
                    }
                    if (!caps->profileToken.isEmpty()) caps->method = "onvif-lamp";
                    else if (caps->method.isEmpty()) caps->reason = QStringLiteral("Không lấy được profile ONVIF");
                    finish(*caps);
                });
            });
        };
        if (caps->imagingUrl.isEmpty()) return afterIrCut();
        o->getImagingOptions([caps, afterIrCut](bool ok, const QByteArray &x, const QString &) {
            const QStringList modes = ok ? OnvifClient::values(x, "IrCutFilterModes") : QStringList();
            if (modes.contains("OFF") && modes.contains("ON")) {
                caps->method = "onvif-ircut";
                caps->canAuto = modes.contains("AUTO");
            }
            afterIrCut();
        });
    });
}

void LightController::apply(const ChannelRef &r, Action a, std::function<void(bool, const QString &)> cb)
{
    detect(r, [this, r, a, cb](const Caps &c) {
        const CameraInfo cam = CameraDirectory::instance().camera(r);
        const QString what = a == On ? QStringLiteral("Bật đèn") : a == Off ? QStringLiteral("Tắt đèn") : QStringLiteral("Đèn tự động");
        if (c.method.isEmpty()) return cb(false, c.reason.isEmpty() ? QStringLiteral("Không hỗ trợ") : c.reason);

        if (c.method == "dahua") {
            // Ap dung cho ca 3 profile (ngay / dem / thuong) de chac chan profile dang dung bi doi
            const QString mode = a == On ? "Manual" : a == Off ? "Off" : "Auto";
            QString q = QStringLiteral("/cgi-bin/configManager.cgi?action=setConfig");
            for (int p = 0; p < 3; ++p) {
                q += QStringLiteral("&Lighting_V2[0][%1][0].Mode=%2").arg(p).arg(mode);
                if (a == On) q += QStringLiteral("&Lighting_V2[0][%1][0].MiddleLight[0].Light=100").arg(p);
            }
            auto *d = new DahuaClient(cam.address, cam.httpPort, cam.user, cam.password, this);
            d->get(q, [d, cb, what](int, const QByteArray &body, const QString &err) {
                d->deleteLater();
                const bool ok = err.isEmpty() && body.trimmed().startsWith("OK");
                cb(ok, ok ? what : QStringLiteral("%1 thất bại: %2").arg(what, err.isEmpty() ? QString::fromUtf8(body.left(80)) : friendly(err)));
            });
            return;
        }

        auto *o = new OnvifClient(cam.address, cam.httpPort, cam.user, cam.password, this);
        auto done = [o, cb, what](bool ok, const QByteArray &, const QString &err) {
            o->deleteLater();
            cb(ok, ok ? what : QStringLiteral("%1 thất bại: %2").arg(what, friendly(err)));
        };
        if (c.method == "onvif-lamp" && a != Auto) {
            o->call(c.ptzUrl,
                    QStringLiteral("<tptz:SendAuxiliaryCommand xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\">"
                                   "<tptz:ProfileToken>%1</tptz:ProfileToken><tptz:AuxiliaryData>%2</tptz:AuxiliaryData>"
                                   "</tptz:SendAuxiliaryCommand>")
                        .arg(c.profileToken, a == On ? "tt:Lamp|On" : "tt:Lamp|Off"),
                    done);
            return;
        }
        // IrCutFilter: OFF = bo loc hong ngoai mo = che do dem (den bat); ON = ngay; AUTO
        if (c.imagingUrl.isEmpty() || (a == Auto && !c.canAuto)) {
            o->deleteLater();
            return cb(false, QStringLiteral("Camera không hỗ trợ chế độ này"));
        }
        const QString mode = a == On ? "OFF" : a == Off ? "ON" : "AUTO";
        o->call(c.imagingUrl,
                QStringLiteral("<timg:SetImagingSettings><timg:VideoSourceToken>%1</timg:VideoSourceToken>"
                               "<timg:ImagingSettings><tt:IrCutFilter>%2</tt:IrCutFilter></timg:ImagingSettings>"
                               "<timg:ForcePersistence>false</timg:ForcePersistence></timg:SetImagingSettings>")
                    .arg(c.vsToken, mode),
                done);
    });
}
