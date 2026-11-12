#include "VideoTile.h"
#include "AdjustDialog.h"
#include "ImageSettings.h"
#include "LightController.h"
#include "Theme.h"
#include "Users.h"

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

    m_video = new QWidget(body);
    m_video->setAttribute(Qt::WA_NativeWindow);
    m_video->setAttribute(Qt::WA_NoSystemBackground);
    m_video->setAutoFillBackground(true);
    QPalette pal = m_video->palette();
    pal.setColor(QPalette::Window, QColor("#000000"));
    m_video->setPalette(pal);
    m_video->setMinimumSize(80, 45);
    m_video->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_video->installEventFilter(this);
    m_stack->addWidget(m_video);
    lay->addWidget(body, 1);

    connect(&m_player, &GstPlayer::playing, this, [this] {
        m_status.clear();
        m_isError = false;
        refreshHeader();
    });
    connect(&m_player, &GstPlayer::error, this, [this](const QString &msg) {
        m_status = msg;
        m_isError = true;
        m_player.stop();
        refreshHeader();
        if (m_autoReconnect && !m_suspended) {
            const int gen = m_generation;
            m_status += QStringLiteral(" — thử lại sau 3 giây");
            refreshHeader();
            QTimer::singleShot(3000, this, [this, gen] {
                if (gen == m_generation && !m_suspended && !m_player.isOpen()) start();
            });
        }
    });
    connect(&m_player, &GstPlayer::endOfStream, this, [this] {
        m_status = QStringLiteral("Hết dữ liệu");
        refreshHeader();
    });
    connect(&m_player, &GstPlayer::videoClicked, this, [this](bool dbl) {
        emit clicked(this);
        if (dbl) emit doubleClicked(this);
    });

    m_headerTimer.setInterval(1000);
    connect(&m_headerTimer, &QTimer::timeout, this, &VideoTile::refreshHeader);
    m_headerTimer.start();
    setSelected(false);
    refreshHeader();
}

VideoTile::~VideoTile()
{
    m_player.stop();
}

void VideoTile::playLive(const ChannelRef &ref, bool mainStream)
{
    const NvrConfig &n = cfg().nvrs[ref.nvr];
    const bool keepAudio = (ref == m_ref) && m_params.audio;
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
    m_suspended = false;
    start();
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
    ++m_generation;
    m_status = QStringLiteral("Đang kết nối...");
    m_isError = false;
    m_stack->setCurrentWidget(m_video);
    applyImage();
    refreshHeader();
    m_player.open(m_params, quintptr(m_video->winId()));
}

void VideoTile::applyImage()
{
    m_player.setColor(ImageSettings::get(m_imageRef));
}

void VideoTile::setAudio(bool on)
{
    if (!hasStream() || m_params.audio == on) return;
    m_params.audio = on;
    m_audioBtn->setChecked(on);
    if (!m_suspended) start();
}

void VideoTile::stopStream()
{
    ++m_generation;
    m_player.stop();
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
    ++m_generation;
    m_player.stop();
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
    AdjustDialog dlg(&m_player, m_imageRef, m_title, this);
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
        menu.exec(m_lightBtn->mapToGlobal(QPoint(0, m_lightBtn->height())));
    });
}

static QString dotStyle(const QColor &c)
{
    return QStringLiteral("background:%1;border-radius:4px;").arg(c.name());
}

void VideoTile::refreshHeader()
{
    const bool has = hasStream();
    m_audioBtn->setVisible(has && users().can(Users::Audio));
    m_adjustBtn->setVisible(has && users().can(Users::Adjust));
    // chi o xem truc tiep cua dau ghi Dahua (camera le RTSP khong co thong tin camera)
    m_lightBtn->setVisible(has && m_ref.valid() && users().can(Users::Light) && m_ref.nvr < cfg().nvrs.size()
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
        const QString res = m_player.resolution();
        if (!res.isEmpty()) parts << res;
        parts << QStringLiteral("%1 fps").arg(qRound(m_player.fps()));
        if (m_player.droppedFrames() > 0) parts << QStringLiteral("rớt %1").arg(m_player.droppedFrames());
        parts << QStringLiteral("%1 kbps").arg(qRound(m_player.kbps()));
        if (m_params.audio && m_player.isPlaying()) {
            if (m_player.audioActive()) parts << QStringLiteral("♪ %1").arg(m_player.audioCodec());
            else if (!m_player.hasAudioStream()) parts << QStringLiteral("camera không gửi tiếng");
            else parts << QStringLiteral("không phát được tiếng %1").arg(m_player.audioCodec());
        }
    }
    m_dot->setStyleSheet(dotStyle(dot));
    const bool showFlash = !m_flash.isEmpty();
    m_info->setText(showFlash ? m_flash : parts.join(QStringLiteral("  ·  ")));
    const char *col = showFlash ? (m_flashError ? "#f08a84" : "#ffb4a8") : (m_isError ? "#f08a84" : "#a8959a");
    m_info->setStyleSheet(QStringLiteral("color:%1;font-size:11px;background:transparent;").arg(col));
    const QString dec = m_player.decoderName();
    m_headerBar->setToolTip(dec.isEmpty() ? QString() : QStringLiteral("Bộ giải mã: %1").arg(dec));
}

bool VideoTile::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_headerBar || o == m_name || o == m_info || o == m_video || o == m_placeholder) {
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
    m_player.expose();
}
