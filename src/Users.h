#pragma once
#include "Config.h"

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

// Tai khoan dang nhap LitePSS (luu users.json canh file chay, mat khau bam PBKDF2-SHA256).
// Day la phan quyen trong ung dung: ai sua duoc file tren may thi van vuot qua duoc.
struct UserAccount {
    QString name;
    bool admin = false;
    QByteArray salt;      // hex
    QByteArray hash;      // hex
    // Quyen cua nhan vien (quan tri luon co du)
    bool live = true;
    bool playback = true;
    bool exportClip = false;
    bool audio = true;
    bool light = false;
    bool adjust = true;
    bool allChannels = true;
    QStringList channels; // "<id thiet bi>/<kenh>" khi allChannels = false
};

class Users {
public:
    enum Perm { Live, Playback, Export, Audio, Light, Adjust };

    static Users &instance();
    bool load(const QString &path);
    bool save(QString *error = nullptr);
    bool isEmpty() const { return m_list.isEmpty(); }

    QVector<UserAccount> &list() { return m_list; }
    int indexOf(const QString &name) const;
    static void setPassword(UserAccount &u, const QString &password);
    static bool checkPassword(const UserAccount &u, const QString &password);
    int adminCount() const;

    bool login(const QString &name, const QString &password);
    const UserAccount &current() const { return m_current; }
    bool isAdmin() const { return m_current.admin; }
    bool can(Perm p) const;
    bool canSee(const ChannelRef &r) const; // quyen + kenh dang bat
    void refreshCurrent();                  // sau khi sua tai khoan dang dang nhap

private:
    QString m_path;
    QVector<UserAccount> m_list;
    UserAccount m_current;
};

inline Users &users() { return Users::instance(); }

// Hop thoai dang nhap / tao tai khoan quan tri lan dau
class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(QWidget *parent = nullptr);
};

// Hop thoai doi mat khau cua chinh minh
bool changeOwnPassword(QWidget *parent);

// Dang nhap tu dong bang mat khau da luu (Windows: DPAPI theo nguoi dung Windows, luu trong
// HKCU, tach rieng theo thu muc du lieu). That bai (tai khoan bi xoa / doi mat khau) -> tra false
// im lang va xoa mat khau da luu, de hien hop thoai dang nhap.
bool tryAutoLogin();
