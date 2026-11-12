#include "VideoTile.h"
#include "AdjustDialog.h"
#include "AppInfo.h"
#include <QFile>
#include <QTime>
#include "ImageSettings.h"
#include "LightController.h"
#include "Theme.h"
#include "Users.h"

#include <QContextMenuEvent>
#include <QCursor>
#include <QMenu>

#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QStackedLayout>
#include <QToolButton>
#include <QVBoxLayout>

VideoTile::VideoTile(int index, QWidget *parent) : QFrame(parent), m_index(index)
{
    setObjectName("tile");
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(2, 2, 2, 2);
    lay->setSpacing(0);

    // ---- thanh tieu de ----
    m_headerBar = new QWidget(this);
    m_headerBar->setObjectName("tileHeader");
    m_headerBar->setFixedHeight(24);
    m_headerBar->installEventFilter(this);
    auto *hl = new QHBoxLayout(m_headerBar);
    hl->setContentsMargins(8, 0, 4, 0);
    hl->setSpacing(6);
    m_dot = new QLabel(m_headerBar);
    m_dot->setFixedSize(8, 8);
    hl->addWidget(m_dot);
    m_name = new QLabel(m_headerBar);
    m_name->setStyleSheet("color:#f2f4f7;font-weight:600;font-size:12px;background:transparent;");
    m_name->installEventFilter(this);
    // Nhan co the co lai (bi cat chu) de khong ep o rong ra: luoi 4x4 luon vua cua so
    m_name->setMinimumWidth(1);
    hl->addWidget(m_name, 1);
    m_info = new QLabel(m_headerBar);
    m_info->setStyleSheet("color:#a8959a;font-size:11px;background:transparent;");
    m_info->installEventFilter(this);
    m_info->setMinimumWidth(1);
    hl->addWidget(m_info);

    m_audioBtn = new QToolButton(m_headerBar);
    m_audioBtn->setObjectName("tileBtn");
    m_audioBtn->setIcon(Icons::speaker(false));
    m_audioBtn->setIconSize(QSize(16, 16));
    m_audioBtn->setCheckable(true);
    m_audioBtn->setToolTip(QStringLiteral("Bật/tắt âm thanh camera (mỗi lúc 1 ô)"));
    hl->addWidget(m_audioBtn);
    m_adjustBtn = new QToolButton(m_headerBar);
    m_adjustBtn->setObjectName("tileBtn");
    m_adjustBtn->setIcon(Icons::sliders());
    m_adjustBtn->setIconSize(QSize(16, 16));
    m_adjustBtn->setToolTip(QStringLiteral("Chỉnh độ sáng, tương phản, màu"));
    hl->addWidget(m_adjustBtn);
    m_lightBtn = new QToolButton(m_headerBar);
    m_lightBtn->setObjectName("tileBtn");
    m_lightBtn->setIcon(Icons::bulb());
    m_lightBtn->setIconSize(QSize(16, 16));
    m_lightBtn->setToolTip(QStringLiteral("Bật / tắt đèn trên camera"));
    hl->addWidget(m_lightBtn);
    lay->addWidget(m_headerBar);
    connect(m_lightBtn, &QToolButton::clicked, this, [this] {
        emit clicked(this);
        openLightMenu();
    });

    connect(m_audioBtn, &QToolButton::clicked, this, [this] {
        m_audioBtn->setChecked(audioOn()); // trang thai that do LiveView quyet dinh
        emit clicked(this);
        emit audioToggleRequested(this);
    });
    connect(m_adjustBtn, &QToolButton::clicked, this, [this] {
        emit clicked(this);
        openAdjust();
    });

    // ---- vung video / o trong ----
    auto *body = new QWidget(this);
    m_stack = new QStackedLayout(body);
    m_stack->setContentsMargins(0, 0, 0, 0);

    m_placeholder = new QLabel(body);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setStyleSheet("color:#5e4a4d;background:#0a0707;font-size:12px;");
    m_placeholder->installEventFilter(this);
    m_placeholder->setWordWrap(true);
    // Bo qua kich thuoc goi y: moi o trong luoi chia deu, khong bi chu/video lam lech hang cot
    m_placeholder->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_stack->addWidget(m_placeholder);

    // 2 lop video native chong len nhau (StackAll: ca 2 deu hien, lop dang chon nam tren)
    m_videoHost = new QWidget(body);
    m_videoHost->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_videoStack = new QStackedLayout(m_videoHost);
    m_videoStack->setContentsMargins(0, 0, 0, 0);
    m_videoStack->setStackingMode(QStackedLayout::StackAll);
    for (QWidget *&v : m_videos) {
        v = new QWidget(m_videoHost);
        v->setAttribute(Qt::WA_NativeWindow);
        v->setAttribute(Qt::WA_NoSystemBackground);
        v->setAutoFillBackground(true);
        QPalette pal = v->palette();
        pal.setColor(QPalette::Window, QColor("#000000"));
        v->setPalette(pal);
        v->setMinimumSize(80, 45);
        v->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        v->installEventFilter(this);
        m_videoStack->addWidget(v);
    }
    m_videoStack->setCurrentWidget(m_videos[0]);
    body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_stack->addWidget(m_videoHost);
    lay->addWidget(body, 1);

    for (int k = 0; k < 2; ++k) {
        GstPlayer *pl = &m_players[k];
        connect(pl, &GstPlayer::playing, this, [this, k] {
            if (k != m_cur) return; // lop duoi: cho khung dau tien (checkPending)
            m_status.clear();
            m_isError = false;
            refreshHeader();
            emit streamPlaying();
        });
        connect(pl, &GstPlayer::error, this, [this, k](const QString &msg) {
            if (k == m_pend) {
                // luong moi loi: giu nguyen luong dang hien
                cancelPending();
                flashStatus(QStringLiteral("Không mở được luồng %1: %2").arg(m_main ? "HD" : "SD", msg), true);
                return;
            }
            if (k != m_cur) return;
            m_status = msg;
            m_isError = true;
            cur().stop();
            refreshHeader();
            if (m_autoReconnect && !m_suspended) {
                const int gen = m_generation;
                m_status += QStringLiteral(" — thử lại sau 3 giây");
                refreshHeader();
                QTimer::singleShot(3000, this, [this, gen] {
                    if (gen == m_generation && !m_suspended && !cur().isOpen()) start();
                });
            }
        });
        connect(pl, &GstPlayer::endOfStream, this, [this, k] {
            if (k != m_cur) return;
            m_status = QStringLiteral("Hết dữ liệu");
            refreshHeader();
        });
        connect(pl, &GstPlayer::videoClicked, this, [this](bool dbl) {
            emit clicked(this);
            if (dbl) emit doubleClicked(this);
        });
        connect(pl, &GstPlayer::videoContextMenu, this, [this] {
            emit clicked(this);
            showContextMenu(QCursor::pos());
        });
    }
    m_pendTimer.setInterval(40);
    connect(&m_pendTimer, &QTimer::timeout, this, &VideoTile::checkPending);

    m_headerTimer.setInterval(1000);
    connect(&m_headerTimer, &QTimer::timeout, this, &VideoTile::refreshHeader);
    m_headerTimer.start();
    setSelected(false);
    refreshHeader();
}

VideoTile::~VideoTile()
{
    m_pendTimer.stop();
    m_players[0].stop();
    m_players[1].stop();
}

void VideoTile::playLive(const ChannelRef &ref, bool mainStream)
{
    const NvrConfig &n = cfg().nvrs[ref.nvr];
    const bool sameChannel = (ref == m_ref);
    const bool keepAudio = sameChannel && m_params.audio;
    StreamParams p;
    p.url = cfg().liveUrl(ref, mainStream);
    p.user = n.user;
    p.password = n.password;
    p.latencyMs = cfg().liveLatencyMs;
    p.live = true;
    p.leaky = cfg().liveQueueLeaky;
    p.audio = keepAudio;
    m_ref = ref;
    m_imageRef = ref;
    m_main = mainStream;
    m_params = p;
    m_title = cfg().channelTitle(ref);
    m_autoReconnect = true;
    if (sameChannel && m_pend >= 0 && !m_suspended && p.url == m_curParams.url && p.audio == m_curParams.audio) {
        cancelPending(); // vd. phong to roi thu nho ngay khi HD chua kip co hinh: SD van dang hien
        refreshHeader();
        return;
    }
    const bool seamless = sameChannel && !m_suspended && cur().isOpen() && cur().hasRendered();
    m_suspended = false;
    if (seamless) startSeamless();
    else start();
}

void VideoTile::playCustom(const StreamParams &p, const QString &title, bool autoReconnect,
                           const ChannelRef &imageRef)
{
    m_ref = {};
    m_imageRef = imageRef;
    m_params = p;
    m_title = title;
    m_autoReconnect = autoReconnect;
    m_suspended = false;
    start();
}

void VideoTile::start()
{
    cancelPending();
    ++m_generation;
    m_status = QStringLiteral("Đang kết nối...");
    m_isError = false;
    m_stack->setCurrentWidget(m_videoHost);
    m_videoStack->setCurrentWidget(m_videos[m_cur]);
    applyImage(&cur());
    refreshHeader();
    m_curParams = m_params;
    cur().open(m_params, quintptr(m_videos[m_cur]->winId()));
}

void VideoTile::startSeamless()
{
    cancelPending();
    ++m_generation;
    m_pend = 1 - m_cur;
    GstPlayer &next = m_players[m_pend];
    applyImage(&next);
    // lop moi nam duoi lop dang hien (StackAll) nen van ve duoc nhung chua lo ra
    m_videoStack->setCurrentWidget(m_videos[m_cur]);
    m_videos[m_cur]->raise();
    m_pendClock.start();
    m_pendTimer.start();
    next.open(m_params, quintptr(m_videos[m_pend]->winId()));
    refreshHeader();
}

void VideoTile::checkPending()
{
    if (m_pend < 0) {
        m_pendTimer.stop();
        return;
    }
    GstPlayer &next = m_players[m_pend];
    if (next.isOpen() && next.hasRendered()) {
        // luong moi da co hinh: dua len tren roi dung luong cu
        if (qEnvironmentVariableIsSet("LITEPSS_HTTP_LOG")) {
            QFile f(AppInfo::dataFile("switch.log"));
            if (f.open(QIODevice::Append | QIODevice::Text))
                f.write(QStringLiteral("%1 o%2 %3 co hinh sau %4 ms (hinh cu van hien trong luc cho)\n")
                            .arg(QTime::currentTime().toString("HH:mm:ss.zzz")).arg(m_index + 1)
                            .arg(m_main ? "HD" : "SD").arg(m_pendClock.elapsed()).toUtf8());
        }
        const int old = m_cur;
        m_curParams = m_params;
        m_cur = m_pend;
        m_pend = -1;
        m_pendTimer.stop();
        m_videoStack->setCurrentWidget(m_videos[m_cur]);
        m_videos[m_cur]->raise();
        m_players[old].stop();
        m_status.clear();
        m_isError = false;
        refreshHeader();
        emit streamPlaying();
        return;
    }
    if (m_pendClock.elapsed() > 10000) {
        cancelPending();
        flashStatus(QStringLiteral("Luồng %1 không có hình sau 10 giây, giữ luồng cũ").arg(m_main ? "HD" : "SD"), true);
    }
}

void VideoTile::cancelPending()
{
    m_pendTimer.stop();
    if (m_pend < 0) return;
    m_players[m_pend].stop();
    m_pend = -1;
    m_videoStack->setCurrentWidget(m_videos[m_cur]);
    m_videos[m_cur]->raise();
}

void VideoTile::applyImage(GstPlayer *p)
{
    p->setColor(ImageSettings::get(m_imageRef));
}

void VideoTile::setAudio(bool on)
{
    if (!hasStream() || m_params.audio == on) return;
    m_params.audio = on;
    m_audioBtn->setChecked(on);
    if (m_suspended) return;
    if (m_params.live && cur().isOpen() && cur().hasRendered()) startSeamless();
    else start();
}

void VideoTile::stopStream()
{
    cancelPending();
    ++m_generation;
    cur().stop();
    m_ref = {};
    m_imageRef = {};
    m_params = {};
    m_title.clear();
    m_status.clear();
    m_isError = false;
    m_suspended = false;
    m_audioBtn->setChecked(false);
    m_stack->setCurrentWidget(m_placeholder);
    refreshHeader();
}

void VideoTile::suspend()
{
    if (!hasStream() || m_suspended) return;
    cancelPending();
    ++m_generation;
    cur().stop();
    m_suspended = true;
    refreshHeader();
}

void VideoTile::resume()
{
    if (!m_suspended) return;
    m_suspended = false;
    start();
}

void VideoTile::setSelected(bool s)
{
    m_selected = s;
    setStyleSheet(QStringLiteral(
        "QFrame#tile{background:#0a0707;border:1px solid %1;border-radius:6px;}"
        "QWidget#tileHeader{background:%2;border-top-left-radius:5px;border-top-right-radius:5px;}")
        .arg(s ? "#e53935" : "#2a1b1d", s ? "#3a1418" : "#171011"));
}

void VideoTile::openAdjust()
{
    if (!hasStream() || !users().can(Users::Adjust)) return;
    AdjustDialog dlg(&cur(), m_imageRef, m_title, this);
    dlg.exec();
}

void VideoTile::flashStatus(const QString &msg, bool error)
{
    m_flash = msg;
    m_flashError = error;
    const int gen = ++m_flashGen;
    refreshHeader();
    QTimer::singleShot(5000, this, [this, gen] {
        if (gen != m_flashGen) return;
        m_flash.clear();
        refreshHeader();
    });
}

void VideoTile::openLightMenu()
{
    const ChannelRef ref = m_ref;
    if (!ref.valid() || !users().can(Users::Light)) return;
    flashStatus(QStringLiteral("Đang kiểm tra đèn của camera..."), false);
    LightController::instance().detect(ref, [this, ref](const LightController::Caps &c) {
        if (!(ref == m_ref)) return;
        if (c.method.isEmpty()) {
            flashStatus(QStringLiteral("Đèn: %1").arg(c.reason.isEmpty() ? QStringLiteral("không hỗ trợ") : c.reason), true);
            return;
        }
        m_flash.clear();
        refreshHeader();
        QMenu menu(this);
        auto add = [&](const QString &text, LightController::Action a) {
            menu.addAction(text, this, [this, ref, a] {
                flashStatus(QStringLiteral("Đang gửi lệnh..."), false);
                LightController::instance().apply(ref, a, [this, ref](bool ok, const QString &msg) {
                    if (ref == m_ref) flashStatus(msg, !ok);
                });
            });
        };
        add(QStringLiteral("Bật đèn"), LightController::On);
        add(QStringLiteral("Tắt đèn"), LightController::Off);
        if (c.canAuto) add(QStringLiteral("Tự động (theo cảm biến)"), LightController::Auto);
        menu.exec(m_lightBtn->isVisible() ? m_lightBtn->mapToGlobal(QPoint(0, m_lightBtn->height())) : QCursor::pos());
    });
}

static QString dotStyle(const QColor &c)
{
    return QStringLiteral("background:%1;border-radius:4px;").arg(c.name());
}

void VideoTile::refreshHeader()
{
    const bool has = hasStream();
    const bool small = compact();
    m_audioBtn->setVisible(has && !small && users().can(Users::Audio));
    m_adjustBtn->setVisible(has && !small && users().can(Users::Adjust));
    // chi o xem truc tiep cua dau ghi Dahua (camera le RTSP khong co thong tin camera)
    m_lightBtn->setVisible(has && !small && m_ref.valid() && users().can(Users::Light) && m_ref.nvr < cfg().nvrs.size()
                           && cfg().nvrs[m_ref.nvr].isDahua());
    if (!has) {
        m_dot->setStyleSheet(dotStyle(QColor("#4a3639")));
        m_name->setText(QStringLiteral("Ô %1").arg(m_index + 1));
        m_info->clear();
        m_placeholder->setText(m_placeholderText);
        m_stack->setCurrentWidget(m_placeholder);
        return;
    }
    if (m_ref.valid()) m_title = cfg().channelTitle(m_ref); // ten kenh co the tai ve sau khi mo luong
    // o nho (luoi 3x3/4x4): bo ten dau ghi, chi giu ten kenh
    m_name->setText(width() < 520 && m_ref.valid() ? cfg().shortTitle(m_ref) : m_title);
    m_name->setToolTip(m_title); // ten day du khi o nho bi cat chu
    m_audioBtn->setChecked(m_params.audio);
    m_audioBtn->setIcon(Icons::speaker(m_params.audio));

    QStringList parts;
    QColor dot = Theme::ok;
    if (m_suspended) {
        dot = QColor("#4a3639");
        parts << QStringLiteral("tạm dừng");
    } else if (!m_status.isEmpty()) {
        dot = m_isError ? Theme::bad : Theme::warn;
        parts << m_status;
    } else {
        if (!m_params.live) parts << QStringLiteral("XEM LẠI");
        else parts << (m_main ? QStringLiteral("HD") : QStringLiteral("SD"));
        if (isSwitching()) parts << (m_main ? QStringLiteral("đang mở HD…") : QStringLiteral("đang mở SD…"));
        const QString res = cur().resolution();
        if (!res.isEmpty()) parts << res;
        parts << QStringLiteral("%1 fps").arg(qRound(cur().fps()));
        if (cur().droppedFrames() > 0) parts << QStringLiteral("rớt %1").arg(cur().droppedFrames());
        parts << QStringLiteral("%1 kbps").arg(qRound(cur().kbps()));
        if (m_params.audio && cur().isPlaying()) {
            if (cur().audioActive()) parts << QStringLiteral("♪ %1").arg(cur().audioCodec());
            else if (!cur().hasAudioStream()) parts << QStringLiteral("camera không gửi tiếng");
            else parts << QStringLiteral("không phát được tiếng %1").arg(cur().audioCodec());
        }
    }
    m_dot->setStyleSheet(dotStyle(dot));
    const bool showFlash = !m_flash.isEmpty();
    m_info->setText(showFlash ? m_flash : parts.join(QStringLiteral("  ·  ")));
    // o rat nho: chi giu cham trang thai + ten kenh (thong so xem o tooltip / khi phong to)
    m_info->setVisible(!small || showFlash || m_isError);
    if (small) m_name->setToolTip(QStringLiteral("%1\n%2").arg(m_title, parts.join(QStringLiteral("  ·  "))));
    const char *col = showFlash ? (m_flashError ? "#f08a84" : "#ffb4a8") : (m_isError ? "#f08a84" : "#a8959a");
    m_info->setStyleSheet(QStringLiteral("color:%1;font-size:11px;background:transparent;").arg(col));
    const QString dec = cur().decoderName();
    m_headerBar->setToolTip(dec.isEmpty() ? QString() : QStringLiteral("Bộ giải mã: %1").arg(dec));
}

bool VideoTile::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_headerBar || o == m_name || o == m_info || o == m_videos[0] || o == m_videos[1] || o == m_placeholder) {
        if (e->type() == QEvent::MouseButtonPress) {
            emit clicked(this);
        } else if (e->type() == QEvent::MouseButtonDblClick) {
            emit doubleClicked(this);
            return true;
        }
    }
    return QFrame::eventFilter(o, e);
}

void VideoTile::resizeEvent(QResizeEvent *e)
{
    QFrame::resizeEvent(e);
    // Uu tien ten kenh: thong so chi chiem toi da ~45% thanh tieu de (o nho trong luoi 4x4)
    m_info->setMaximumWidth(qMax(40, int(width() * 0.40) - 40));
    m_players[0].expose();
    m_players[1].expose();
}

void VideoTile::contextMenuEvent(QContextMenuEvent *e)
{
    emit clicked(this);
    showContextMenu(e->globalPos());
}

// Menu chuot phai: dung duoc o moi co o, can nhat o luoi 6x6 khi thanh tieu de an het nut
void VideoTile::showContextMenu(const QPoint &globalPos)
{
    if (!hasStream()) return;
    QMenu menu(this);
    menu.addAction(m_title)->setEnabled(false);
    menu.addSeparator();
    if (m_params.live) menu.addAction(QStringLiteral("Phóng to / thu nhỏ"), this, [this] { emit doubleClicked(this); });
    if (users().can(Users::Audio))
        menu.addAction(audioOn() ? QStringLiteral("Tắt âm thanh") : QStringLiteral("Bật âm thanh"), this,
                       [this] { emit audioToggleRequested(this); });
    if (users().can(Users::Adjust)) menu.addAction(QStringLiteral("Chỉnh hình ảnh..."), this, &VideoTile::openAdjust);
    if (m_params.live && m_ref.valid() && users().can(Users::Count)) {
        const ChannelRef ref = m_ref;
        menu.addAction(QStringLiteral("Đếm số người"), this, [this, ref] { emit countRequested(ref); });
    }
    if (m_ref.valid() && users().can(Users::Light) && m_ref.nvr < cfg().nvrs.size() && cfg().nvrs[m_ref.nvr].isDahua())
        menu.addAction(QStringLiteral("Đèn camera..."), this, &VideoTile::openLightMenu);
    menu.exec(globalPos);
}
