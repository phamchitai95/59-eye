#pragma once
#include <QMainWindow>
#include <QVector>

class DahuaClient;
class LiveView;
class PlaybackView;
class SettingsView;
class QTabWidget;
class QTreeWidget;
class QToolButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void runAutomation(const QStringList &args); // tham so dong lenh de do tu dong

private:
    class MetricsBar *m_metrics = nullptr;
    void buildTree();
    void fetchTitles();
    void reloadDevices();    // them / sua / xoa thiet bi
    void refreshChannels();  // bat / tat kenh, doi quyen
    void applyPermissions();
    void logout();

    QVector<DahuaClient *> m_clients; // nullptr voi camera RTSP
    QTreeWidget *m_tree;
    QTabWidget *m_tabs;
    LiveView *m_live;
    PlaybackView *m_playback;
    SettingsView *m_settings = nullptr;
    QToolButton *m_userBtn;
    int m_reloadGen = 0;
};
