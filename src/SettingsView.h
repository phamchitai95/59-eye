#pragma once
#include <QWidget>

class QListWidget;
class QTableWidget;
class QPushButton;
class QLabel;

// Tab Cai dat (chi quan tri): thiet bi + kenh, tai khoan + phan quyen.
// Moi thay doi chi luu trong LitePSS (config.json / users.json), khong ghi vao dau ghi.
class SettingsView : public QWidget {
    Q_OBJECT
public:
    explicit SettingsView(QWidget *parent = nullptr);
    void reload();

signals:
    void devicesChanged();   // them / sua / xoa thiet bi -> nap lai toan bo
    void channelsChanged();  // bat / tat kenh
    void usersChanged();

private:
    QWidget *buildDevicesPage();
    QWidget *buildUsersPage();
    void reloadDevices();
    void reloadChannels();
    void reloadUsers();
    void addDevice(bool rtsp);
    void editDevice();
    void deleteDevice();
    void setAllChannels(bool on);
    void editUser(int index); // -1 = them moi
    void deleteUser();
    bool saveConfig();

    QListWidget *m_devList;
    QListWidget *m_chList;
    QLabel *m_chTitle;
    QPushButton *m_editDev, *m_delDev, *m_allOn, *m_allOff;
    QTableWidget *m_userTable;
    QPushButton *m_editUser, *m_delUser;
    bool m_filling = false;
};
