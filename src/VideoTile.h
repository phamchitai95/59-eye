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
    GstPlayer *player() { return &m_player; }
    bool hasStream() const { return !m_params.url.isEmpty(); }

    void setSelected(bool s);
    void openAdjust();
    void setPlaceholderText(const QString &t) { m_placeholderText = t; refreshHeader(); }

signals:
    void clicked(VideoTile *t);
    void doubleClicked(VideoTile *t);
    void audioToggleRequested(VideoTile *t);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    void start();
    void refreshHeader();
    void applyImage();

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
    QStackedLayout *m_stack;
    QWidget *m_video;
    QLabel *m_placeholder;
    GstPlayer m_player;
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
