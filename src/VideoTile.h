#pragma once
#include "Config.h"
#include "GstPlayer.h"

#include <QFrame>
#include <QLabel>
#include <QTimer>

class QStackedLayout;
class QToolButton;

// Mot o video: thanh tieu de (cham trang thai, ten kenh, thong so, nut loa, nut chinh hinh)
// + vung video native de GStreamer ve thang len.
// Co 2 bo phat chong len nhau: khi doi luong cung kenh (phong to -> HD, thu nho -> SD) luong moi mo
// o lop duoi, luong cu van hien cho toi khi luong moi ve khung dau tien roi moi doi lop -> khong den man hinh.
class VideoTile : public QFrame {
    Q_OBJECT
public:
    explicit VideoTile(int index, QWidget *parent = nullptr);
    ~VideoTile() override;

    int index() const { return m_index; }

    void playLive(const ChannelRef &ref, bool mainStream);
    // imageRef: kenh dung de lay chinh mau (xem lai)
    void playCustom(const StreamParams &p, const QString &title, bool autoReconnect,
                    const ChannelRef &imageRef = {});
    void stopStream();               // dung va quen kenh
    void suspend();                  // dung nhung nho kenh de mo lai
    void resume();
    bool isSuspended() const { return m_suspended; }

    void setAudio(bool on);          // mo lai luong co/khong am thanh
    bool audioOn() const { return m_params.audio; }

    ChannelRef channel() const { return m_ref; }
    bool isMainStream() const { return m_main; }
    GstPlayer *player() { return &m_players[m_cur]; }  // bo phat dang hien
    bool hasStream() const { return !m_params.url.isEmpty(); }
    bool isSwitching() const { return m_pend >= 0; }

    void setSelected(bool s);
    void openAdjust();
    void setPlaceholderText(const QString &t) { m_placeholderText = t; refreshHeader(); }

signals:
    void clicked(VideoTile *t);
    void doubleClicked(VideoTile *t);
    void audioToggleRequested(VideoTile *t);
    void countRequested(const ChannelRef &ref); // chuot phai -> Dem so nguoi
    void streamPlaying(); // bo phat dang hien bat dau chay (thay cho player()->playing khi doi lop)

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    void start();
    void startSeamless();            // mo luong moi o lop duoi, doi lop khi co hinh
    void checkPending();
    void cancelPending();
    void refreshHeader();
    void applyImage(GstPlayer *p);
    void showContextMenu(const QPoint &globalPos);
    bool compact() const { return width() < 300; } // o rat nho (luoi 6x6)
    GstPlayer &cur() { return m_players[m_cur]; }

    int m_index;
    QWidget *m_headerBar;
    QLabel *m_dot;
    QLabel *m_name;
    QLabel *m_info;
    QToolButton *m_audioBtn;
    QToolButton *m_adjustBtn;
    QToolButton *m_lightBtn;
    void openLightMenu();
    void flashStatus(const QString &msg, bool error);
    QString m_flash;
    bool m_flashError = false;
    int m_flashGen = 0;
    QStackedLayout *m_stack;     // o trong / vung video
    QStackedLayout *m_videoStack; // 2 lop video (StackAll, lop dang hien o tren)
    QWidget *m_videoHost;
    QWidget *m_videos[2];
    QLabel *m_placeholder;
    GstPlayer m_players[2];
    int m_cur = 0;               // lop dang hien
    int m_pend = -1;             // lop dang mo luong moi (-1 = khong co)
    StreamParams m_curParams;    // luong cua lop dang hien (khac m_params khi dang doi luong)
    QTimer m_pendTimer;
    QElapsedTimer m_pendClock;
    QTimer m_headerTimer;

    ChannelRef m_ref;
    ChannelRef m_imageRef;
    bool m_main = false;
    StreamParams m_params;
    QString m_title;
    bool m_autoReconnect = false;
    bool m_suspended = false;
    bool m_selected = false;
    QString m_status;
    bool m_isError = false;
    int m_generation = 0;
    QString m_placeholderText = QStringLiteral("Nhấp đúp vào một kênh bên trái để mở\n(hoặc bấm \"Tự điền kênh\")");
};
