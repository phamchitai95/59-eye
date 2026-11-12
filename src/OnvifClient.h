#pragma once
#include <QMap>
#include <QObject>
#include <QStringList>
#include <functional>

class QNetworkAccessManager;

// Client ONVIF toi gian (SOAP 1.2 + WS-Security UsernameToken digest).
// Dung de doc/doi che do hong ngoai (IrCutFilter) va gui lenh phu tro (IRLamp / Light).
class OnvifClient : public QObject {
    Q_OBJECT
public:
    OnvifClient(const QString &host, int port, const QString &user, const QString &password,
                QObject *parent = nullptr);

    using Done = std::function<void(bool ok, const QByteArray &xml, const QString &err)>;

    // Goi 1 lenh SOAP toi 1 dich vu (url rong = device_service)
    void call(const QString &url, const QString &bodyXml, Done cb);

    // Tim dia chi cac dich vu (Media, Imaging, DeviceIO, PTZ) va token nguon video
    void discover(std::function<void(const QString &err)> cb);
    QString service(const QString &name) const { return m_services.value(name); }
    QString videoSourceToken() const { return m_vsToken; }

    void getImagingOptions(Done cb);
    void getImagingSettings(Done cb);
    void setIrCutFilter(const QString &mode, Done cb);         // ON (ngay) / OFF (dem) / AUTO
    void sendAuxiliary(const QString &command, Done cb);       // vd "tt:IRLamp|On"

    // Lay noi dung cac the co ten local (bo namespace) trong XML
    static QStringList values(const QByteArray &xml, const QString &localName);
    static QString faultText(const QByteArray &xml);

private:
    QString envelope(const QString &body) const;
    QString deviceUrl() const;

    QString m_host;
    int m_port;
    QString m_user, m_password;
    QMap<QString, QString> m_services;
    QString m_vsToken;
    QNetworkAccessManager *m_nam;
};
