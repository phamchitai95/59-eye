// Che do chan doan CHI DOC: --light-probe <nvr> <file> <kenh1,kenh2,...>
// Lay IP/tai khoan camera tu NVR (giu trong bo nho), hoi camera ho tro kieu den nao. Khong gui lenh doi trang thai.
#include "CameraDirectory.h"
#include "Config.h"
#include "DahuaClient.h"
#include "LightController.h"
#include "OnvifClient.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <functional>
#include <memory>

// Kiem tra bo do cua nut den (LightController::detect) - CHI DOC, khong bat/tat den
int runLightDetect(int nvr, const QString &outPath, const QList<int> &channels)
{
    auto *out = new QFile(outPath, qApp);
    out->open(QIODevice::WriteOnly | QIODevice::Text);
    auto *nvrClient = new DahuaClient(nvr, qApp);
    auto idx = std::make_shared<int>(0);
    auto next = std::make_shared<std::function<void()>>();
    *next = [=] {
        if (*idx >= channels.size()) { out->close(); QCoreApplication::quit(); return; }
        const int ch = channels[(*idx)++];
        LightController::instance().detect({nvr, ch}, [=](const LightController::Caps &c) {
            const CameraInfo cam = CameraDirectory::instance().camera({nvr, ch});
            out->write(QStringLiteral("kenh %1 (%2 %3): method=%4 auto=%5 %6\n")
                           .arg(ch).arg(cam.address, cam.deviceType, c.method.isEmpty() ? "-" : c.method)
                           .arg(c.canAuto).arg(c.reason).toUtf8());
            out->flush();
            (*next)();
        });
    };
    CameraDirectory::instance().load(nvr, nvrClient, [=](const QString &) { (*next)(); });
    return QCoreApplication::exec();
}

int runLightProbe(int nvr, const QString &outPath, const QList<int> &channels)
{
    auto *out = new QFile(outPath, qApp);
    out->open(QIODevice::WriteOnly | QIODevice::Text);
    auto w = [out](const QString &s) { out->write(s.toUtf8()); out->write("\n"); out->flush(); };
    auto *nvrClient = new DahuaClient(nvr, qApp);
    auto idx = std::make_shared<int>(0);
    auto next = std::make_shared<std::function<void()>>();

    *next = [=] {
        if (*idx >= channels.size()) { out->close(); QCoreApplication::quit(); return; }
        const int ch = channels[(*idx)++];
        const CameraInfo cam = CameraDirectory::instance().camera({nvr, ch});
        w(QStringLiteral("\n======== Kenh %1: %2:%3 proto=%4 type=%5 user=%6")
              .arg(ch).arg(cam.address).arg(cam.httpPort).arg(cam.protocol, cam.deviceType, cam.user));
        if (!cam.valid()) { w("  (khong co camera)"); (*next)(); return; }

        if (cam.isOnvif()) {
            auto *o = new OnvifClient(cam.address, cam.httpPort, cam.user, cam.password, qApp);
            o->call({}, "<tds:GetDeviceInformation/>", [=](bool ok, const QByteArray &x, const QString &e) {
                if (ok) w(QStringLiteral("  Hang/Model/FW: %1 / %2 / %3")
                              .arg(OnvifClient::values(x, "Manufacturer").value(0), OnvifClient::values(x, "Model").value(0),
                                   OnvifClient::values(x, "FirmwareVersion").value(0)));
                else w("  GetDeviceInformation loi: " + e);
                o->discover([=](const QString &err) {
                    if (!err.isEmpty()) { w("  discover loi: " + err); (*next)(); return; }
                    w("  Dich vu: " + QStringList(o->service("Imaging").isEmpty() ? "Imaging=khong" : "Imaging=co").join(' ')
                      + (o->service("DeviceIO").isEmpty() ? " DeviceIO=khong" : " DeviceIO=co")
                      + (o->service("PTZ").isEmpty() ? " PTZ=khong" : " PTZ=co") + " vs=" + o->videoSourceToken());
                    o->getImagingOptions([=](bool ok2, const QByteArray &x2, const QString &e2) {
                        if (ok2) w("  IrCutFilterModes ho tro: " + OnvifClient::values(x2, "IrCutFilterModes").join(", "));
                        else w("  GetOptions loi: " + e2);
                        o->getImagingSettings([=](bool ok3, const QByteArray &x3, const QString &e3) {
                            if (ok3) w("  IrCutFilter hien tai: " + OnvifClient::values(x3, "IrCutFilter").value(0));
                            else w("  GetImagingSettings loi: " + e3);
                            // Lenh phu tro ho tro (khong gui lenh nao, chi doc danh sach)
                            // PTZ GetNodes: nhieu camera Trung Quoc khai bao lenh den o day (tt:Light|On, tt:IRLamp|On...)
                            auto ptzThenIo = [=] {
                                auto io = [=] {
                                    if (o->service("DeviceIO").isEmpty()) { (*next)(); return; }
                                    o->call(o->service("DeviceIO"),
                                            "<tmd:GetRelayOutputs xmlns:tmd=\"http://www.onvif.org/ver10/deviceIO/wsdl\"/>",
                                            [=](bool ok6, const QByteArray &x6, const QString &e6) {
                                        if (ok6) w("  RelayOutputs: " + QString::number(OnvifClient::values(x6, "RelayOutputs").size())
                                                   + " cong, mode=" + OnvifClient::values(x6, "Mode").join(","));
                                        else w("  GetRelayOutputs loi: " + e6);
                                        (*next)();
                                    });
                                };
                                if (o->service("PTZ").isEmpty()) { io(); return; }
                                o->call(o->service("PTZ"), "<tptz:GetNodes xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\"/>",
                                        [=](bool ok5, const QByteArray &x5, const QString &e5) {
                                    if (ok5) {
                                        const QStringList aux = OnvifClient::values(x5, "AuxiliaryCommands");
                                        w("  PTZ AuxiliaryCommands: " + (aux.isEmpty() ? QStringLiteral("(khong co)") : aux.join(", ")));
                                    } else w("  PTZ GetNodes loi: " + e5);
                                    io();
                                });
                            };
                            o->call({}, "<tds:GetCapabilities><tds:Category>Device</tds:Category></tds:GetCapabilities>",
                                    [=](bool ok4, const QByteArray &x4, const QString &) {
                                if (ok4) {
                                    const QStringList aux = OnvifClient::values(x4, "AuxiliaryCommands");
                                    w("  AuxiliaryCommands (Device): " + (aux.isEmpty() ? QStringLiteral("(khong khai bao)") : aux.join(", ")));
                                }
                                ptzThenIo();
                            });
                        });
                    });
                });
            });
        } else {
            auto *d = new DahuaClient(cam.address, cam.httpPort, cam.user, cam.password, qApp);
            const QStringList paths{"/cgi-bin/magicBox.cgi?action=getDeviceType",
                                    "/cgi-bin/configManager.cgi?action=getConfig&name=Lighting_V2",
                                    "/cgi-bin/configManager.cgi?action=getConfig&name=Lighting",
                                    "/cgi-bin/configManager.cgi?action=getConfig&name=LightingScheme"};
            auto pi = std::make_shared<int>(0);
            auto step = std::make_shared<std::function<void()>>();
            *step = [=] {
                if (*pi >= paths.size()) { (*next)(); return; }
                const QString p = paths[(*pi)++];
                d->get(p, [=](int st, const QByteArray &body, const QString &e) {
                    w(QStringLiteral("  %1 -> %2 %3").arg(p.section("name=", 1).isEmpty() ? p : p.section("name=", 1)).arg(st).arg(e));
                    int n = 0;
                    for (const QByteArray &l : body.split('\n'))
                        if (!l.trimmed().isEmpty() && n++ < 25) w("     " + QString::fromUtf8(l.trimmed()));
                    (*step)();
                });
            };
            (*step)();
        }
    };

    CameraDirectory::instance().load(nvr, nvrClient, [=](const QString &err) {
        if (!err.isEmpty()) { w("Khong lay duoc danh sach camera tu NVR: " + err); out->close(); QCoreApplication::quit(); return; }
        (*next)();
    });
    return QCoreApplication::exec();
}
