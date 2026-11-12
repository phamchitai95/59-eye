#pragma once
#include "Config.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QElapsedTimer>
#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

class DahuaClient;
class RecordCalendar;
class TimelineWidget;
class VideoTile;

class PlaybackView : public QWidget {
    Q_OBJECT
public:
    explicit PlaybackView(QWidget *parent = nullptr);

    void setChannel(const ChannelRef &ref);
    void reloadChannelNames();
    void playFirstRecord();   // dung cho chay tu dong (--playback)
    void applyPermissions();
    void setSpeedIndex(int i);   // thu nghiem: 0=x1 1=x2 2=x4 3=x8
    void pressPause() { togglePause(); }
    void pressJump(int sec) { jump(sec); }
    void toggleAudio();
    QString statusText() const { return m_status->text(); }
    QWidget *calendarWidget() const;  // de chup thu lich (--calendar-shot)
    void stopPlayback();
    void openExport();        // hop thoai trich MP4
    // thu nghiem: trich doan [from, from+sec] ra file
    void exportHeadless(const QDateTime &from, int seconds, const QString &file);

private:
    ChannelRef currentChannel() const;
    void search();
    void openAt(const QDateTime &t);
    void togglePause();
    void applySpeed();
    void jump(int seconds);
    void tick();
    QDateTime currentTime() const;
    void loadMonth();

    RecordCalendar *m_calendar;
    QPushButton *m_exportBtn;
    QHash<QString, QHash<QDate, int>> m_monthCache;
    int m_monthGen = 0;
    QComboBox *m_channel;
    QDateEdit *m_date;
    QComboBox *m_stream;
    QPushButton *m_searchBtn;
    QPushButton *m_playBtn;
    QComboBox *m_speed;
    QLabel *m_timeLbl;
    QLabel *m_status;
    TimelineWidget *m_timeline;
    VideoTile *m_tile;
    QTimer m_tick;

    QDateTime m_openTime;      // thoi diem bat dau cua phien RTSP hien tai
    QElapsedTimer m_wall;      // du phong khi khong truy van duoc vi tri
    qint64 m_wallBaseMs = 0;
    double m_rate = 1.0;
    bool m_paused = false;
    bool m_audio = false;
    QDateTime m_pausedAt;
};
