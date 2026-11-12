#include "MainWindow.h"
#include "AppInfo.h"
#include "CameraDirectory.h"
#include "Config.h"
#include "DahuaClient.h"
#include "LightController.h"
#include "LiveView.h"
#include "MetricsBar.h"
#include "PlaybackView.h"
#include "SettingsView.h"
#include "Theme.h"
#include "Updater.h"
#include "Users.h"
#include "VideoTile.h"

#include "GstPlayer.h"

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif
#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QHeaderView>
#include <QMenu>
#include <QProcess>
#include <QScreen>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

MainWindow::MainWindow()
{
    setWindowTitle(QStringLiteral("%1 · %2").arg(AppInfo::displayName(), users().current().name));
    setWindowIcon(Icons::app());
#ifdef _WIN32
    // Thanh tieu de cua so mau toi (Windows 10 1809+ / 11)
    BOOL dark = TRUE;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(winId()), 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
#endif

    auto *split = new QSplitter(this);
    m_tree = new QTreeWidget(split);
    m_tree->setHeaderLabel(QStringLiteral("THIẾT BỊ"));
    m_tree->setToolTip(QStringLiteral("Nhấp đúp vào kênh để mở"));
    m_tree->setIconSize(QSize(16, 16));
    m_tree->setIndentation(14);
    m_tree->setMinimumWidth(210);
    m_tree->setMaximumWidth(360);

    m_tabs = new QTabWidget(split);
    m_live = new LiveView(m_tabs);
    m_playback = new PlaybackView(m_tabs);
    split->setStretchFactor(1, 1);
    setCentralWidget(split);

    // Goc phai thanh tab: tai khoan dang dung
    m_userBtn = new QToolButton(m_tabs);
    m_userBtn->setIcon(Icons::user());
    m_userBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_userBtn->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(m_userBtn);
    menu->addAction(QStringLiteral("Đổi mật khẩu"), this, [this] {
        if (changeOwnPassword(this)) statusBar()->showMessage(QStringLiteral("Đã đổi mật khẩu"), 4000);
    });
    menu->addAction(QStringLiteral("Kiểm tra cập nhật"), this, [this] { Updater::instance().check(this, true); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("Giới thiệu"), this, [this] { AppInfo::showAbout(this); });
    menu->addAction(QStringLiteral("Đăng xuất"), this, &MainWindow::logout);
    m_userBtn->setMenu(menu);
    m_tabs->setCornerWidget(m_userBtn, Qt::TopRightCorner);

    m_metrics = new MetricsBar(this);
    statusBar()->addWidget(m_metrics, 1);
    statusBar()->setSizeGripEnabled(false);

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        const QVariant v = it->data(0, Qt::UserRole);
        if (!v.isValid()) return;
        const QPoint p = v.toPoint();
        const ChannelRef ref{p.x(), p.y()};
        if (m_tabs->currentWidget() == m_live) m_live->assign(ref);
        else if (m_tabs->currentWidget() == m_playback) m_playback->setChannel(ref);
    });

    applyPermissions();
    reloadDevices();
    // Tu kiem tra cap nhat sau khi mo 5 giay (im lang neu khong co ban moi)
    QTimer::singleShot(5000, this, [this] { Updater::instance().check(this, false); });
}

void MainWindow::applyPermissions()
{
    QWidget *cur = m_tabs->currentWidget();
    m_tabs->clear();
    if (users().can(Users::Live)) m_tabs->addTab(m_live, QStringLiteral("Xem trực tiếp"));
    else m_live->closeAll();
    if (users().can(Users::Playback)) m_tabs->addTab(m_playback, QStringLiteral("Xem lại"));
    else m_playback->stopPlayback();
    m_live->setVisible(users().can(Users::Live));
    m_playback->setVisible(users().can(Users::Playback));
    if (users().isAdmin()) {
        if (!m_settings) {
            m_settings = new SettingsView(m_tabs);
            connect(m_settings, &SettingsView::devicesChanged, this, &MainWindow::reloadDevices);
            connect(m_settings, &SettingsView::channelsChanged, this, &MainWindow::refreshChannels);
            connect(m_settings, &SettingsView::usersChanged, this, [this] {
                users().refreshCurrent();
                setWindowTitle(QStringLiteral("%1 · %2").arg(AppInfo::displayName(), users().current().name));
                refreshChannels();
            });
        }
        m_tabs->addTab(m_settings, Icons::gear(), QStringLiteral("Cài đặt"));
    }
    if (m_tabs->count() == 0) {
        auto *none = new QWidget(m_tabs);
        m_tabs->addTab(none, QStringLiteral("Không có quyền"));
    }
    const int i = m_tabs->indexOf(cur);
    if (i >= 0) m_tabs->setCurrentIndex(i);
    m_userBtn->setText(QStringLiteral(" %1 · %2").arg(users().current().name,
                       users().isAdmin() ? QStringLiteral("Quản trị") : QStringLiteral("Nhân viên")));
    m_playback->applyPermissions();
}

void MainWindow::logout()
{
    // Khoi dong lai ung dung de hien man hinh dang nhap, tranh sot trang thai cua phien cu
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
    QApplication::quit();
}

static QString argValue(const QStringList &a, const QString &name)
{
    const int i = a.indexOf(name);
    return (i >= 0 && i + 1 < a.size()) ? a[i + 1] : QString();
}

void MainWindow::runAutomation(const QStringList &a)
{
    if (a.contains("--playback")) {
        m_tabs->setCurrentWidget(m_playback);
        m_playback->setChannel({0, 1});
        QTimer::singleShot(3000, m_playback, &PlaybackView::playFirstRecord);
        // --steps "9:s2,12:p,15:p,18:j30" (giay:lenh) thu tua/tam dung (chan doan)
        //   sN = chon toc do N (0=x1..3=x8), p = bam Tam dung/Phat, jN = nhay N giay
        for (const QString &step : argValue(a, "--steps").split(',', Qt::SkipEmptyParts)) {
            const int at = step.section(':', 0, 0).toInt();
            const QString cmd = step.section(':', 1);
            QTimer::singleShot(at * 1000, m_playback, [this, cmd] {
                if (cmd.startsWith('s')) m_playback->setSpeedIndex(cmd.mid(1).toInt());
                else if (cmd == "p") m_playback->pressPause();
                else if (cmd.startsWith('j')) m_playback->pressJump(cmd.mid(1).toInt());
                else if (cmd == "a") m_playback->toggleAudio();
            });
        }
    } else if (a.contains("--settings") && m_settings) {
        m_tabs->setCurrentWidget(m_settings);
    } else {
        const int layout = argValue(a, "--layout").toInt();
        if (layout == 1 || layout == 4 || layout == 9 || layout == 16) m_live->setLayoutCount(layout);
        if (a.contains("--autofill")) m_live->autoFill();
        if (a.contains("--audio"))
            QTimer::singleShot(4000, this, [this] {
                if (VideoTile *t = m_live->tile(0)) m_live->onAudioToggle(t);
            });
    }
    // --export <kenh> <yyyy-MM-ddTHH:mm:ss> <giay> <file>: thu trich MP4 (kenh cua thiet bi 0)
    const int ei = a.indexOf("--export");
    if (ei >= 0 && ei + 4 < a.size()) {
        const int ch = a[ei + 1].toInt();
        const QDateTime from = QDateTime::fromString(a[ei + 2], Qt::ISODate);
        const int sec = a[ei + 3].toInt();
        const QString file = a[ei + 4];
        m_tabs->setCurrentWidget(m_playback);
        m_playback->setChannel({0, ch});
        QTimer::singleShot(1000, m_playback, [this, from, sec, file] { m_playback->exportHeadless(from, sec, file); });
    }
    // --calendar-shot <file>: chup lich ngay co ban ghi cua kenh 1 (thu nghiem)
    const QString calShot = argValue(a, "--calendar-shot");
    if (!calShot.isEmpty()) {
        m_tabs->setCurrentWidget(m_playback);
        m_playback->setChannel({0, 1});
        QTimer::singleShot(12000, this, [this, calShot] {
            QWidget *c = m_playback->calendarWidget();
            c->resize(360, 280);
            c->grab().save(calShot);
        });
    }
    const QString cmp = argValue(a, "--compare");
    if (!cmp.isEmpty()) m_metrics->setCompare(cmp);
    if (a.contains("--record")) QTimer::singleShot(5000, m_metrics, &MetricsBar::toggleRecord);

    const QString shot = argValue(a, "--screenshot");
    const int quitAfter = argValue(a, "--quit-after").toInt();
    if (!shot.isEmpty()) {
        const int at = quitAfter > 2 ? (quitAfter - 2) * 1000 : 10000;
        QTimer::singleShot(at, this, [this, shot] {
            // chi chup noi dung cua so ung dung (khong chup man hinh khac)
            grab().save(shot);
        });
    }
    const QString statsFile = argValue(a, "--stats");
    if (quitAfter > 0)
        QTimer::singleShot(quitAfter * 1000, qApp, [statsFile] {
            if (!statsFile.isEmpty()) {
                QFile f(statsFile);
                if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                    f.write((QStringLiteral("latency=%1 leaky=%2\n").arg(cfg().liveLatencyMs).arg(cfg().liveQueueLeaky) +
                             GstPlayer::statsSummary() + "\n").toUtf8());
            }
            QApplication::quit();
        });
}

void MainWindow::buildTree()
{
    m_tree->clear();
    for (int n = 0; n < cfg().nvrs.size(); ++n) {
        const NvrConfig &nvr = cfg().nvrs[n];
        QList<int> visible;
        for (int ch = 1; ch <= nvr.channels; ++ch)
            if (users().canSee({n, ch})) visible << ch;
        if (visible.isEmpty()) continue;

        if (!nvr.isDahua()) {
            // camera le: 1 dong, nhap dup de mo
            auto *it = new QTreeWidgetItem(m_tree, {nvr.name});
            it->setIcon(0, Icons::camera());
            it->setData(0, Qt::UserRole, QPoint(n, 1));
            it->setToolTip(0, QStringLiteral("Camera RTSP (chỉ xem trực tiếp)"));
            continue;
        }
        auto *top = new QTreeWidgetItem(m_tree, {nvr.name});
        top->setIcon(0, Icons::recorder());
        top->setToolTip(0, nvr.host);
        top->setData(0, Qt::UserRole + 1, n);
        QFont bf = top->font(0);
        bf.setBold(true);
        top->setFont(0, bf);
        for (int ch : visible) {
            auto *it = new QTreeWidgetItem(top, {QStringLiteral("%1  %2").arg(ch, 2, 10, QChar('0')).arg(cfg().shortTitle({n, ch}))});
            it->setIcon(0, Icons::camera());
            it->setData(0, Qt::UserRole, QPoint(n, ch));
        }
        top->setExpanded(true);
    }
}

void MainWindow::reloadDevices()
{
    ++m_reloadGen;
    m_live->closeAll();
    m_playback->stopPlayback();
    qDeleteAll(m_clients);
    m_clients.clear();
    CameraDirectory::instance().clear();
    LightController::instance().clear();
    for (int i = 0; i < cfg().nvrs.size(); ++i)
        m_clients.push_back(cfg().nvrs[i].isDahua() ? new DahuaClient(i, this) : nullptr);
    buildTree();
    m_playback->reloadChannelNames();
    if (m_settings) m_settings->reload();
    fetchTitles();
}

void MainWindow::refreshChannels()
{
    applyPermissions();
    buildTree();
    m_live->closeDisallowed();
    m_playback->reloadChannelNames();
}

void MainWindow::fetchTitles()
{
    const int gen = m_reloadGen;
    for (int n = 0; n < m_clients.size(); ++n) {
        if (!m_clients[n]) continue;
        // Danh sach camera that sau dau ghi (dung cho nut den)
        CameraDirectory::instance().load(n, m_clients[n]);
        const QString id = cfg().nvrs[n].id;
        m_clients[n]->fetchChannelTitles([this, n, id, gen](const QStringList &titles, const QString &err) {
            if (gen != m_reloadGen || n >= cfg().nvrs.size() || cfg().nvrs[n].id != id) return; // ket qua cu
            if (!err.isEmpty()) {
                for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
                    QTreeWidgetItem *top = m_tree->topLevelItem(i);
                    if (top->data(0, Qt::UserRole + 1).toInt() != n || !top->data(0, Qt::UserRole + 1).isValid()) continue;
                    top->setToolTip(0, QStringLiteral("Không kết nối được: %1").arg(err));
                    top->setForeground(0, Theme::bad);
                }
                return;
            }
            cfg().nvrs[n].titles = titles;
            buildTree();
            m_playback->reloadChannelNames();
            if (m_settings) m_settings->reload();
        });
    }
}
