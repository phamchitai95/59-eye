#include "OnvifClient.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QUrl>
#include <QXmlStreamReader>
#include <memory>

OnvifClient::OnvifClient(const QString &host, int port, const QString &user, const QString &password,
                         QObject *parent)
    : QObject(parent), m_host(host), m_port(port), m_user(user), m_password(password),
      m_nam(new QNetworkAccessManager(this))
{
}

QString OnvifClient::deviceUrl() const
{
    return QStringLiteral("http://%1:%2/onvif/device_service").arg(m_host).arg(m_port);
}

QString OnvifClient::envelope(const QString &body) const
{
    // WS-Security: PasswordDigest = Base64(SHA1(nonce + created + password))
    QByteArray nonce(16, 0);
    for (char &c : nonce) c = char(QRandomGenerator::global()->bounded(256));
    const QByteArray created = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ss.zzzZ").toUtf8();
    const QByteArray digest =
        QCryptographicHash::hash(nonce + created + m_password.toUtf8(), QCryptographicHash::Sha1).toBase64();
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
        "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" "
        "xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" "
        "xmlns:timg=\"http://www.onvif.org/ver20/imaging/wsdl\" "
        "xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
        "<s:Header><wsse:Security s:mustUnderstand=\"1\" "
        "xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\" "
        "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\">"
        "<wsse:UsernameToken><wsse:Username>%1</wsse:Username>"
        "<wsse:Password Type=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">%2</wsse:Password>"
        "<wsse:Nonce EncodingType=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary\">%3</wsse:Nonce>"
        "<wsu:Created>%4</wsu:Created></wsse:UsernameToken></wsse:Security></s:Header>"
        "<s:Body>%5</s:Body></s:Envelope>")
        .arg(m_user.toHtmlEscaped(), QString::fromLatin1(digest), QString::fromLatin1(nonce.toBase64()),
             QString::fromLatin1(created), body);
}

void OnvifClient::call(const QString &url, const QString &bodyXml, Done cb)
{
    QUrl u(url.isEmpty() ? deviceUrl() : url);
    // Mot so camera tra XAddr bang IP noi bo khac; luon dung host ma ta ket noi duoc
    u.setHost(m_host);
    if (u.port() <= 0) u.setPort(m_port);
    QNetworkRequest req(u);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/soap+xml; charset=utf-8");
    req.setTransferTimeout(8000);
    QNetworkReply *r = m_nam->post(req, envelope(bodyXml).toUtf8());
    connect(r, &QNetworkReply::finished, this, [r, cb] {
        const QByteArray xml = r->readAll();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString err;
        if (r->error() != QNetworkReply::NoError || xml.contains(":Fault>")) {
            err = faultText(xml);
            if (err.isEmpty()) err = status ? QStringLiteral("HTTP %1").arg(status) : r->errorString();
        }
        r->deleteLater();
        cb(err.isEmpty(), xml, err);
    });
}

QStringList OnvifClient::values(const QByteArray &xml, const QString &localName)
{
    QStringList out;
    QXmlStreamReader x(xml);
    while (!x.atEnd()) {
        if (x.readNext() == QXmlStreamReader::StartElement && x.name() == localName)
            out << x.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
    }
    return out;
}

QString OnvifClient::faultText(const QByteArray &xml)
{
    QStringList t = values(xml, "Text");
    if (t.isEmpty()) t = values(xml, "Reason");
    QStringList codes = values(xml, "Value");
    QString s = t.join(' ');
    if (!codes.isEmpty()) s += QStringLiteral(" [%1]").arg(codes.join(", "));
    return s.trimmed();
}

void OnvifClient::discover(std::function<void(const QString &)> cb)
{
    call({}, QStringLiteral("<tds:GetCapabilities><tds:Category>All</tds:Category></tds:GetCapabilities>"),
         [this, cb](bool ok, const QByteArray &xml, const QString &err) {
        if (!ok) return cb(QStringLiteral("GetCapabilities: %1").arg(err));
        // Moi khoi <Media>, <Imaging>, <DeviceIO>, <PTZ> co <XAddr>
        QXmlStreamReader x(xml);
        QString section;
        while (!x.atEnd()) {
            if (x.readNext() != QXmlStreamReader::StartElement) continue;
            const QString n = x.name().toString();
            if (n == "Media" || n == "Imaging" || n == "PTZ" || n == "DeviceIO" || n == "Device" || n == "Events")
                section = n;
            else if (n == "XAddr" && !section.isEmpty() && !m_services.contains(section))
                m_services[section] = x.readElementText().trimmed();
        }
        const QString media = m_services.value("Media");
        if (media.isEmpty()) return cb(QStringLiteral("Camera không có dịch vụ Media"));
        call(media, QStringLiteral("<trt:GetVideoSources/>"), [this, cb](bool ok2, const QByteArray &x2, const QString &e2) {
            if (!ok2) return cb(QStringLiteral("GetVideoSources: %1").arg(e2));
            QXmlStreamReader r(x2);
            while (!r.atEnd()) {
                if (r.readNext() == QXmlStreamReader::StartElement && r.name() == QLatin1String("VideoSources")) {
                    m_vsToken = r.attributes().value("token").toString();
                    break;
                }
            }
            cb(m_vsToken.isEmpty() ? QStringLiteral("Không có token nguồn video") : QString());
        });
    });
}

void OnvifClient::getImagingOptions(Done cb)
{
    call(service("Imaging"), QStringLiteral("<timg:GetOptions><timg:VideoSourceToken>%1</timg:VideoSourceToken></timg:GetOptions>")
                                 .arg(m_vsToken), cb);
}

void OnvifClient::getImagingSettings(Done cb)
{
    call(service("Imaging"), QStringLiteral("<timg:GetImagingSettings><timg:VideoSourceToken>%1</timg:VideoSourceToken></timg:GetImagingSettings>")
                                 .arg(m_vsToken), cb);
}

void OnvifClient::setIrCutFilter(const QString &mode, Done cb)
{
    call(service("Imaging"),
         QStringLiteral("<timg:SetImagingSettings><timg:VideoSourceToken>%1</timg:VideoSourceToken>"
                        "<timg:ImagingSettings><tt:IrCutFilter>%2</tt:IrCutFilter></timg:ImagingSettings>"
                        "<timg:ForcePersistence>false</timg:ForcePersistence></timg:SetImagingSettings>")
             .arg(m_vsToken, mode), cb);
}

void OnvifClient::sendAuxiliary(const QString &command, Done cb)
{
    call({}, QStringLiteral("<tds:SendAuxiliaryCommand><tds:AuxiliaryCommand>%1</tds:AuxiliaryCommand></tds:SendAuxiliaryCommand>")
                 .arg(command.toHtmlEscaped()), cb);
}
