#include "PlaybackView.h"
#include "DahuaClient.h"
#include "ExportDialog.h"
#include "RecordCalendar.h"
#include "Users.h"
#include "TimelineWidget.h"
#include "Theme.h"
#include "VideoTile.h"

#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

PlaybackView::PlaybackView(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 6);
    root->setSpacing(8);

    auto *top = new QHBoxLayout;
    top->setSpacing(6);
    top->addWidget(new QLabel(QStringLiteral("Kênh:"), this));
    m_channel = new QComboBox(this);
    m_channel->setMinimumWidth(260);
    top->addWidget(m_channel);
    top->addWidget(new QLabel(QStringLiteral("Ngày:"), this));
    m_date = new QDateEdit(QDate::currentDate(), this);
    m_date->setCalendarPopup(true);
    m_date->setDisplayFormat("dd/MM/yyyy");
    m_calendar = new RecordCalendar(this);
    m_date->setCalendarWidget(m_calendar);
    m_date->setMaximumDate(QDate::currentDate());
    m_date->setToolTip(QStringLiteral("Lịch: ngày tô màu là có bản ghi, chấm cam là có sự kiện"));
    top->addWidget(m_date);
    top->addWidget(new QLabel(QStringLiteral("Luồng:"), this));
    m_stream = new QComboBox(this);
    m_stream->addItem(QStringLiteral("Chính"), true);
    m_stream->addItem(QStringLiteral("Phụ"), false);
    m_stream->setToolTip(QStringLiteral("Luồng phụ chỉ xem lại được nếu đầu ghi có bật ghi luồng phụ"));
    top->addWidget(m_stream);
    m_searchBtn = new QPushButton(Icons::search(), QStringLiteral(" Tìm bản ghi"), this);
    top->addWidget(m_searchBtn);
    m_exportBtn = new QPushButton(Icons::download(), QStringLiteral(" Trích MP4"), this);
    m_exportBtn->setToolTip(QStringLiteral("Trích một đoạn bản ghi của kênh này ra file MP4"));
    top->addWidget(m_exportBtn);
    m_status = new QLabel(this);
    m_status->setStyleSheet("color:#a8959a;padding-left:8px;");
    top->addWidget(m_status, 1);
    root->addLayout(top);

    m_tile = new VideoTile(0, this);
    m_tile->setSelected(false);
    m_tile->setPlaceholderText(QStringLiteral("Chọn kênh và ngày, rồi nhấp vào thanh thời gian bên dưới để xem lại\n"
                                              "(đỏ: ghi thường · cam: có sự kiện)"));
    root->addWidget(m_tile, 1);

    m_timeline = new TimelineWidget(this);
    root->addWidget(m_timeline);

    auto *ctl = new QHBoxLayout;
    ctl->setSpacing(6);
    auto *back = new QPushButton(Icons::back(), QStringLiteral(" 30s"), this);
    m_playBtn = new QPushButton(Icons::pause(), QStringLiteral(" Tạm dừng"), this);
    m_playBtn->setMinimumWidth(110);
    auto *fwd = new QPushButton(Icons::forward(), QStringLiteral(" 30s"), this);
    fwd->setLayoutDirection(Qt::RightToLeft);
    ctl->addWidget(back);
    ctl->addWidget(m_playBtn);
    ctl->addWidget(fwd);
    ctl->addSpacing(12);
    ctl->addWidget(new QLabel(QStringLiteral("Tốc độ:"), this));
    m_speed = new QComboBox(this);
    for (int r : {1, 2, 4, 8}) m_speed->addItem(QStringLiteral("x%1").arg(r), r);
    ctl->addWidget(m_speed);
    m_noSync = new QCheckBox(QStringLiteral("Tắt đồng bộ khi tua nhanh (thử nghiệm)"), this);
    m_noSync->setToolTip(QStringLiteral(
        "Nếu đầu ghi tự gửi khung hình nhanh hơn khi nhận Scale, bật mục này để hiển thị ngay khi nhận"));
    ctl->addWidget(m_noSync);
    ctl->addStretch();
    ctl->addWidget(createVolumeControl(this));
    ctl->addSpacing(8);
    m_timeLbl = new QLabel(QStringLiteral("--:--:--"), this);
    QFont f = m_timeLbl->font();
    f.setPointSize(14);
    f.setBold(true);
    m_timeLbl->setFont(f);
    m_timeLbl->setStyleSheet("color:#ffffff;padding:0 6px;");
    ctl->addWidget(m_timeLbl);
    root->addLayout(ctl);

    reloadChannelNames();

    connect(m_searchBtn, &QPushButton::clicked, this, &PlaybackView::search);
    connect(m_exportBtn, &QPushButton::clicked, this, &PlaybackView::openExport);
    connect(m_calendar, &QCalendarWidget::currentPageChanged, this, [this] { loadMonth(); });
    connect(m_date, &QDateEdit::dateChanged, this, [this](const QDate &d) {
        m_timeline->setDay(d);
        search();
    });
    connect(m_channel, &QComboBox::currentIndexChanged, this, [this] {
        search();
        loadMonth();
    });
    connect(m_timeline, &TimelineWidget::seekRequested, this, &PlaybackView::openAt);
    connect(m_playBtn, &QPushButton::clicked, this, &PlaybackView::togglePause);
    connect(back, &QPushButton::clicked, this, [this] { jump(-30); });
    connect(fwd, &QPushButton::clicked, this, [this] { jump(30); });
    connect(m_speed, &QComboBox::currentIndexChanged, this, &PlaybackView::applySpeed);
    connect(m_noSync, &QCheckBox::toggled, this, &PlaybackView::applySpeed);
    connect(m_tile, &VideoTile::audioToggleRequested, this, &PlaybackView::toggleAudio);
    connect(m_tile->player(), &GstPlayer::playing, this, [this] {
        m_wall.start();
        m_wallBaseMs = 0;
    });

    m_tick.setInterval(250);
    connect(&m_tick, &QTimer::timeout, this, &PlaybackView::tick);
    m_tick.start();
}

void PlaybackView::reloadChannelNames()
{
    const ChannelRef keep = currentChannel();
    QSignalBlocker b(m_channel);
    m_channel->clear();
    // chi dau ghi Dahua co xem lai; bo kenh bi tat / khong co quyen
    for (int n = 0; n < cfg().nvrs.size(); ++n) {
        if (!cfg().nvrs[n].isDahua()) continue;
        for (int ch = 1; ch <= cfg().nvrs[n].channels; ++ch)
            if (users().canSee({n, ch})) m_channel->addItem(cfg().channelTitle({n, ch}), QPoint(n, ch));
    }
    if (keep.valid()) {
        const int i = m_channel->findData(QPoint(keep.nvr, keep.ch));
        if (i >= 0) m_channel->setCurrentIndex(i);
    }
    if (!(currentChannel() == keep)) {
        stopPlayback();
        m_timeline->setSegments({});
        loadMonth();
    }
}

ChannelRef PlaybackView::currentChannel() const
{
    const QPoint p = m_channel->currentData().toPoint();
    if (m_channel->currentIndex() < 0) return {};
    return {p.x(), p.y()};
}

void PlaybackView::setChannel(const ChannelRef &ref)
{
    const int i = m_channel->findData(QPoint(ref.nvr, ref.ch));
    if (i >= 0 && i != m_channel->currentIndex()) m_channel->setCurrentIndex(i); // -> search()
    else search();
}

void PlaybackView::search()
{
    const ChannelRef ref = currentChannel();
    if (!ref.valid()) return;
    const QDate d = m_date->date();
    m_timeline->setDay(d);
    m_status->setText(QStringLiteral("Đang tìm bản ghi..."));
    m_searchBtn->setEnabled(false);
    QElapsedTimer t;
    t.start();
    auto *client = new DahuaClient(ref.nvr, this);
    client->findRecords(
        ref.ch, QDateTime(d, QTime(0, 0)), QDateTime(d, QTime(23, 59, 59)),
        [this, client, ref, d, t](const QVector<RecordSegment> &segs, const QString &err) {
            client->deleteLater();
            m_searchBtn->setEnabled(true);
            if (!(ref == currentChannel()) || d != m_date->date()) return; // ket qua cu
            m_timeline->setSegments(segs);
            int events = 0;
            qint64 totalMs = 0;
            for (const RecordSegment &s : segs) {
                events += s.event;
                totalMs += s.start.msecsTo(s.end);
            }
            QString msg = QStringLiteral("%1 đoạn ghi (%2 sự kiện), tổng %3 giờ · %4 ms")
                              .arg(segs.size()).arg(events).arg(totalMs / 3600000.0, 0, 'f', 1)
                              .arg(t.elapsed());
            if (!err.isEmpty()) msg = QStringLiteral("Lỗi: %1").arg(err);
            else if (segs.isEmpty()) msg = QStringLiteral("Không có bản ghi trong ngày này");
            m_status->setText(msg);
        });
}

void PlaybackView::openAt(const QDateTime &t)
{
    const ChannelRef ref = currentChannel();
    if (!ref.valid()) return;
    const NvrConfig &n = cfg().nvrs[ref.nvr];
    const QDateTime end(t.date(), QTime(23, 59, 59));
    StreamParams p;
    p.url = cfg().playbackUrl(ref, t, end, m_stream->currentData().toBool());
    p.user = n.user;
    p.password = n.password;
    p.latencyMs = cfg().playbackLatencyMs;
    p.live = false;
    p.sync = true;
    p.audio = m_audio && users().can(Users::Audio);

    m_openTime = t;
    m_wall.invalidate();
    m_wallBaseMs = 0;
    m_paused = false;
    m_playBtn->setIcon(Icons::pause());
    m_playBtn->setText(QStringLiteral(" Tạm dừng"));
    {
        QSignalBlocker b(m_speed);
        m_speed->setCurrentIndex(0);
    }
    m_rate = 1.0;
    m_tile->playCustom(p, QStringLiteral("%1 · từ %2")
                              .arg(cfg().channelTitle(ref), t.toString("dd/MM HH:mm:ss")), false, ref);
    m_timeline->setPlayhead(t);
}

QDateTime PlaybackView::currentTime() const
{
    if (!m_openTime.isValid()) return {};
    if (m_paused) return m_pausedAt;
    const qint64 pos = const_cast<VideoTile *>(m_tile)->player()->positionMs();
    if (pos >= 0) return m_openTime.addMSecs(pos);
    if (m_wall.isValid()) return m_openTime.addMSecs(m_wallBaseMs + qint64(m_wall.elapsed() * m_rate));
    return m_openTime;
}

void PlaybackView::tick()
{
    if (!m_tile->player()->isOpen()) return;
    const QDateTime cur = currentTime();
    if (!cur.isValid()) return;
    m_timeline->setPlayhead(cur);
    m_timeLbl->setText(cur.toString("dd/MM/yyyy HH:mm:ss"));
}

void PlaybackView::togglePause()
{
    GstPlayer *pl = m_tile->player();
    if (!pl->isOpen()) {
        // chua phat: mo tu dau doan ghi dau tien
        const QDateTime first = m_timeline->firstRecordStart();
        if (first.isValid()) openAt(first);
        return;
    }
    if (!m_paused) {
        m_pausedAt = currentTime();
        m_paused = true;
        pl->setPaused(true);
        m_playBtn->setIcon(Icons::play());
        m_playBtn->setText(QStringLiteral(" Phát"));
    } else {
        m_paused = false;
        pl->setPaused(false);
        if (m_wall.isValid()) {
            m_wallBaseMs = m_openTime.msecsTo(m_pausedAt);
            m_wall.restart();
        }
        m_playBtn->setIcon(Icons::pause());
        m_playBtn->setText(QStringLiteral(" Tạm dừng"));
    }
}

void PlaybackView::playFirstRecord()
{
    const QDateTime first = m_timeline->firstRecordStart();
    if (first.isValid()) openAt(first.addSecs(60));
}

void PlaybackView::jump(int seconds)
{
    const QDateTime cur = currentTime();
    if (cur.isValid()) openAt(cur.addSecs(seconds));
}

void PlaybackView::applySpeed()
{
    GstPlayer *pl = m_tile->player();
    const double rate = m_speed->currentData().toDouble();
    if (!pl->isOpen()) return;
    const QDateTime cur = currentTime();
    pl->setSync(!(m_noSync->isChecked() && rate > 1.0));
    const bool ok = pl->setRate(rate);
    if (m_wall.isValid() && cur.isValid()) {
        m_wallBaseMs = m_openTime.msecsTo(cur);
        m_wall.restart();
    }
    m_rate = rate;
    m_status->setText(ok ? QStringLiteral("Đã gửi lệnh tốc độ x%1 (RTSP Scale) — theo dõi băng thông ở thanh dưới")
                               .arg(rate)
                         : QStringLiteral("Đầu ghi/pipeline không nhận lệnh đổi tốc độ x%1").arg(rate));
}

void PlaybackView::stopPlayback()
{
    m_tile->stopStream();
    m_openTime = {};
    m_timeLbl->setText(QStringLiteral("--:--:--"));
}

QWidget *PlaybackView::calendarWidget() const
{
    return m_calendar;
}

void PlaybackView::applyPermissions()
{
    m_exportBtn->setVisible(users().can(Users::Export));
}

// Danh dau tren lich nhung ngay co ban ghi: 1 lan tim mediaFileFind cho ca thang dang hien
void PlaybackView::loadMonth()
{
    const ChannelRef ref = currentChannel();
    const int gen = ++m_monthGen;
    if (!ref.valid()) {
        m_calendar->setMarks({});
        return;
    }
    const QDate first(m_calendar->yearShown(), m_calendar->monthShown(), 1);
    if (first > QDate::currentDate()) {
        m_calendar->setMarks({});
        return;
    }
    const QString key = QStringLiteral("%1/%2/%3").arg(cfg().channelKey(ref)).arg(first.year()).arg(first.month());
    const bool currentMonth = first.year() == QDate::currentDate().year() && first.month() == QDate::currentDate().month();
    if (m_monthCache.contains(key) && !currentMonth) {
        m_calendar->setMarks(m_monthCache.value(key));
        return;
    }
    m_calendar->setLoading(true);
    QDate last = first.addMonths(1).addDays(-1);
    if (last > QDate::currentDate()) last = QDate::currentDate();
    auto *client = new DahuaClient(ref.nvr, this);
    client->findRecords(ref.ch, QDateTime(first, QTime(0, 0)), QDateTime(last, QTime(23, 59, 59)),
                        [this, client, gen, key, first](const QVector<RecordSegment> &segs, const QString &) {
        client->deleteLater();
        QHash<QDate, int> marks;
        for (const RecordSegment &s : segs) {
            for (QDate d = s.start.date(); d <= s.end.date(); d = d.addDays(1)) {
                if (d.month() != first.month() || d.year() != first.year()) continue;
                // doan ket thuc dung 00:00:00 khong tinh cho ngay sau
                if (d == s.end.date() && d != s.start.date() && s.end.time() == QTime(0, 0)) continue;
                marks[d] |= RecordCalendar::HasRecord | (s.event ? RecordCalendar::HasEvent : 0);
            }
        }
        m_monthCache.insert(key, marks);
        if (gen == m_monthGen) m_calendar->setMarks(marks);
    });
}

void PlaybackView::openExport()
{
    const ChannelRef ref = currentChannel();
    if (!ref.valid() || !users().can(Users::Export)) return;
    QDateTime from = currentTime();
    if (!from.isValid()) from = m_timeline->firstRecordStart();
    if (!from.isValid()) from = QDateTime(m_date->date(), QTime(8, 0));
    from = from.addMSecs(-(from.time().msec()));
    QDateTime to = from.addSecs(60);
    const QDateTime now = QDateTime::currentDateTime().addSecs(-10);
    if (to > now) {
        to = now;
        if (from > to.addSecs(-10)) from = to.addSecs(-60);
    }
    ExportDialog dlg(ref, from, to, m_stream->currentData().toBool(), this);
    dlg.exec();
}

void PlaybackView::exportHeadless(const QDateTime &from, int seconds, const QString &file)
{
    const ChannelRef ref = currentChannel();
    if (!ref.valid()) return;
    auto *dlg = new ExportDialog(ref, from, from.addSecs(seconds), true, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    dlg->startNow(file);
}

void PlaybackView::setSpeedIndex(int i)
{
    m_speed->setCurrentIndex(i); // -> applySpeed()
}

void PlaybackView::toggleAudio()
{
    m_audio = !m_audio;
    const QDateTime cur = currentTime();
    if (cur.isValid()) openAt(cur); // mo lai tai thoi diem dang xem, co/khong tieng
}
