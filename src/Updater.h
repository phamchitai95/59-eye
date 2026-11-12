#pragma once
#include <QObject>
#include <QString>
#include <functional>

class QWidget;

// Cap nhat OTA: doc file latest.json tu updateUrl (http/https hoac thu muc mang \\may\chia-se\...),
// neu co phien ban moi hon thi tai bo cai, kiem tra SHA-256, chay bo cai im lang roi thoat.
//   latest.json = { "version": "1.1.0", "file": "59-eye-setup-1.1.0.exe", "sha256": "...", "notes": "..." }
// "file" la duong dan tuong doi voi latest.json (hoac URL day du).
class Updater : public QObject {
    Q_OBJECT
public:
    static Updater &instance();
    QString manifestUrl() const;
    void check(QWidget *parent, bool interactive);

private:
    void download(QWidget *parent, const QString &url, const QString &sha256, const QString &version);
    void fetch(const QString &url, QWidget *progressParent, std::function<void(const QByteArray &, const QString &err)> cb,
               std::function<void(qint64, qint64)> onProgress = {});
    bool m_busy = false;
};
