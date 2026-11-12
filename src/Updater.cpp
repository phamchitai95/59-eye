#include "Updater.h"
#include "AppInfo.h"
#include "Config.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>

#ifndef UPDATE_URL
#define UPDATE_URL ""
#endif

Updater &Updater::instance()
{
    static Updater u;
    return u;
}

QString Updater::manifestUrl() const
{
    return cfg().updateUrl.isEmpty() ? QStringLiteral(UPDATE_URL) : cfg().updateUrl;
}

static bool isRemote(const QString &s)
{
    return s.startsWith("http://", Qt::CaseInsensitive) || s.startsWith("https://", Qt::CaseInsensitive);
}

// Ghep duong dan file bo cai tuong doi voi vi tri latest.json
static QString resolve(const QString &manifest, const QString &file)
{
    if (isRemote(file) || QDir::isAbsolutePath(file) || file.startsWith("\\\\")) return file;
    if (isRemote(manifest)) return QUrl(manifest).resolved(QUrl(file)).toString();
    return QFileInfo(QDir::fromNativeSeparators(manifest)).dir().filePath(file);
}

void Updater::fetch(const QString &url, QWidget *, std::function<void(const QByteArray &, const QString &)> cb,
                    std::function<void(qint64, qint64)> onProgress)
{
    if (!isRemote(url)) {
        // thu muc mang / o dia: doc truc tiep (khong chan giao dien qua lau voi file nho)
        QTimer::singleShot(0, this, [url, cb] {
            QFile f(QDir::fromNativeSeparators(url));
            if (!f.open(QIODevice::ReadOnly)) return cb({}, QStringLiteral("Không mở được %1").arg(url));
            cb(f.readAll(), {});
        });
        return;
    }
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    QNetworkReply *r = nam->get(req);
    if (onProgress) connect(r, &QNetworkReply::downloadProgress, this, onProgress);
    connect(r, &QNetworkReply::finished, this, [r, nam, cb] {
        nam->deleteLater();
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) return cb({}, r->errorString());
        cb(r->readAll(), {});
    });
}

void Updater::check(QWidget *parent, bool interactive)
{
    const QString url = manifestUrl();
    if (url.isEmpty()) {
        if (interactive)
            QMessageBox::information(parent, AppInfo::displayName(),
                QStringLiteral("Chưa cấu hình nguồn cập nhật (mục \"updateUrl\" trong config.json)."));
        return;
    }
    if (m_busy) return;
    m_busy = true;
    QPointer<QWidget> guard(parent);
    fetch(url, parent, [this, guard, interactive, url](const QByteArray &data, const QString &err) {
        m_busy = false;
        if (!guard) return;
        if (!err.isEmpty()) {
            if (interactive) QMessageBox::warning(guard, AppInfo::displayName(), QStringLiteral("Không kiểm tra được cập nhật:\n%1").arg(err));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(data).object();
        const QString ver = o.value("version").toString();
        const QVersionNumber remote = QVersionNumber::fromString(ver);
        const QVersionNumber local = QVersionNumber::fromString(AppInfo::version());
        if (remote.isNull() || o.value("file").toString().isEmpty()) {
            if (interactive) QMessageBox::warning(guard, AppInfo::displayName(), QStringLiteral("File latest.json không hợp lệ."));
            return;
        }
        if (QVersionNumber::compare(remote, local) <= 0) {
            if (interactive)
                QMessageBox::information(guard, AppInfo::displayName(),
                    QStringLiteral("Đang dùng bản mới nhất (%1).").arg(AppInfo::version()));
            return;
        }
        QMessageBox box(guard);
        box.setWindowTitle(QStringLiteral("Có bản cập nhật"));
        box.setIcon(QMessageBox::Information);
        box.setText(QStringLiteral("<b>%1 %2</b> đã có (đang dùng %3).").arg(AppInfo::displayName(), ver, AppInfo::version()));
        const QString notes = o.value("notes").toString();
        box.setInformativeText((notes.isEmpty() ? QString() : notes + "\n\n") +
                               QStringLiteral("Cập nhật sẽ đóng ứng dụng, cài bản mới (Windows có thể hỏi quyền quản trị) rồi tự mở lại."));
        QPushButton *now = box.addButton(QStringLiteral("Cập nhật ngay"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Để sau"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != static_cast<QAbstractButton *>(now)) return;
        download(guard, resolve(url, o.value("file").toString()), o.value("sha256").toString().toLower(), ver);
    });
}

void Updater::download(QWidget *parent, const QString &url, const QString &sha256, const QString &version)
{
    if (sha256.size() != 64) {
        QMessageBox::warning(parent, AppInfo::displayName(), QStringLiteral("latest.json thiếu mã SHA-256, không cài để an toàn."));
        return;
    }
    m_busy = true;
    auto *dlg = new QProgressDialog(QStringLiteral("Đang tải bản %1...").arg(version), QStringLiteral("Huỷ"), 0, 100, parent);
    dlg->setWindowTitle(AppInfo::displayName());
    dlg->setWindowModality(Qt::WindowModal);
    dlg->setMinimumDuration(0);
    dlg->setValue(0);
    QPointer<QProgressDialog> pd(dlg);
    fetch(url, parent, [this, pd, sha256, version, parent](const QByteArray &data, const QString &err) {
        m_busy = false;
        const bool canceled = !pd || pd->wasCanceled();
        if (pd) pd->deleteLater();
        if (canceled) return;
        if (!err.isEmpty()) {
            QMessageBox::warning(parent, AppInfo::displayName(), QStringLiteral("Tải bản cập nhật lỗi:\n%1").arg(err));
            return;
        }
        const QString got = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
        if (got != sha256) {
            QMessageBox::warning(parent, AppInfo::displayName(), QStringLiteral("File tải về không khớp mã SHA-256, đã huỷ cài đặt."));
            return;
        }
        const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("59-eye-update");
        QDir().mkpath(dir);
        const QString exe = QDir(dir).filePath(QStringLiteral("59-eye-setup-%1.exe").arg(version));
        QFile f(exe);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
            QMessageBox::warning(parent, AppInfo::displayName(), QStringLiteral("Không ghi được %1").arg(exe));
            return;
        }
        f.close();
        // Inno Setup: cai im lang, tu dong dong ung dung dang chay, mo lai sau khi cai
        if (!QProcess::startDetached(exe, {"/SILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CLOSEAPPLICATIONS"})) {
            QMessageBox::warning(parent, AppInfo::displayName(), QStringLiteral("Không chạy được bộ cài:\n%1").arg(exe));
            return;
        }
        QApplication::quit();
    }, [pd](qint64 got, qint64 total) {
        if (pd && total > 0) pd->setValue(int(got * 100 / total));
    });
}
