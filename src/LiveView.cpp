#include "LiveView.h"
#include "Theme.h"
#include "Users.h"
#include "VideoTile.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

LiveView::LiveView(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 6);
    root->setSpacing(8);

    auto *bar = new QHBoxLayout;
    bar->setSpacing(6);
    m_layoutGroup = new QButtonGroup(this);
    m_layoutGroup->setExclusive(true);
    for (int n : {1, 4, 9, 16, 36}) {
        const int side = int(std::sqrt(double(n)));
        auto *b = new QToolButton(this);
        b->setIcon(Icons::grid(side));
        b->setIconSize(QSize(18, 18));
        b->setCheckable(true);
        b->setToolTip(n == 36 ? QStringLiteral("Lưới 6x6 (đủ 32 kênh một đầu ghi)") : QStringLiteral("Lưới %1x%1").arg(side));
        b->setFixedSize(34, 30);
        m_layoutGroup->addButton(b, n);
        connect(b, &QToolButton::clicked, this, [this, n] { setLayoutCount(n); });
        bar->addWidget(b);
    }
    bar->addSpacing(12);
    auto *fill = new QPushButton(Icons::fill(), QStringLiteral(" Tự điền kênh"), this);
    fill->setToolTip(QStringLiteral("Mở lần lượt các kênh của các đầu ghi vào toàn bộ ô đang hiện (luồng phụ)"));
    connect(fill, &QPushButton::clicked, this, &LiveView::autoFill);
    bar->addWidget(fill);
    auto *closeBtn = new QPushButton(Icons::closeAll(), QStringLiteral(" Đóng tất cả"), this);
    connect(closeBtn, &QPushButton::clicked, this, &LiveView::closeAll);
    bar->addWidget(closeBtn);
    bar->addSpacing(12);
    m_mainOnZoom = new QCheckBox(QStringLiteral("Luồng HD khi phóng to"), this);
    m_mainOnZoom->setToolTip(QStringLiteral("Nhấp đúp 1 ô để phóng to, khi đó chuyển sang luồng chính (nét hơn)"));
    m_mainOnZoom->setChecked(true);
    connect(m_mainOnZoom, &QCheckBox::toggled, this, &LiveView::syncStreams);
    bar->addWidget(m_mainOnZoom);
    bar->addStretch();
    bar->addWidget(createVolumeControl(this));
    root->addLayout(bar);

    auto *host = new QWidget(this);
    m_grid = new QGridLayout(host);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(4);
    root->addWidget(host, 1);

    for (int i = 0; i < 36; ++i) {
        auto *t = new VideoTile(i, host);
        connect(t, &VideoTile::clicked, this, [this](VideoTile *x) { select(x->index()); });
        connect(t, &VideoTile::doubleClicked, this, &LiveView::onDoubleClicked);
        connect(t, &VideoTile::audioToggleRequested, this, &LiveView::onAudioToggle);
        m_tiles.push_back(t);
    }
    relayout();
    select(0);
}

void LiveView::onAudioToggle(VideoTile *t)
{
    if (!t->hasStream()) return;
    if (!users().can(Users::Audio)) return;
    const bool turnOn = !t->audioOn();
    if (turnOn)
        for (VideoTile *o : m_tiles)
            if (o != t && o->audioOn()) o->setAudio(false); // moi luc chi 1 o phat tieng
    t->setAudio(turnOn);
}

void LiveView::select(int i)
{
    m_selected = i;
    for (VideoTile *t : m_tiles) t->setSelected(t->index() == i);
}

bool LiveView::wantMain(int i) const
{
    if (!m_mainOnZoom->isChecked()) return false;
    return m_maximized == i || (m_maximized < 0 && m_count == 1 && i == 0);
}

void LiveView::relayout()
{
    if (QAbstractButton *b = m_layoutGroup->button(m_count)) b->setChecked(true);
    for (VideoTile *t : m_tiles) m_grid->removeWidget(t);
    for (int r = 0; r < 6; ++r) {
        m_grid->setRowStretch(r, 0);
        m_grid->setColumnStretch(r, 0);
    }

    QVector<int> visible;
    if (m_maximized >= 0) {
        visible << m_maximized;
        m_grid->addWidget(m_tiles[m_maximized], 0, 0);
        m_grid->setRowStretch(0, 1);
        m_grid->setColumnStretch(0, 1);
    } else {
        const int side = int(std::sqrt(double(m_count)));
        for (int i = 0; i < m_count; ++i) {
            visible << i;
            m_grid->addWidget(m_tiles[i], i / side, i % side);
        }
        for (int r = 0; r < side; ++r) {
            m_grid->setRowStretch(r, 1);
            m_grid->setColumnStretch(r, 1);
        }
    }

    for (VideoTile *t : m_tiles) {
        const bool vis = visible.contains(t->index());
        t->setVisible(vis);
        // O nam ngoai bo cuc hien tai (vd chuyen 4x4 -> 2x2) thi dung han de khong ton tai nguyen.
        // Khi phong to 1 o, cac o khac van chay ngam de quay lai luoi ngay lap tuc.
        if (m_maximized < 0 && !vis) {
            if (t->audioOn()) t->setAudio(false);
            t->suspend();
        }
    }
    syncStreams();
}

void LiveView::syncStreams()
{
    const int limit = m_maximized >= 0 ? int(m_tiles.size()) : m_count;
    for (int i = 0; i < limit; ++i) {
        VideoTile *t = m_tiles[i];
        if (!t->channel().valid()) continue;
        const bool main = wantMain(i);
        if (t->isSuspended() || t->isMainStream() != main) t->playLive(t->channel(), main);
    }
}

void LiveView::setLayoutCount(int n)
{
    m_count = n;
    m_maximized = -1;
    if (m_selected >= n) select(0);
    relayout();
}

void LiveView::assign(const ChannelRef &ref)
{
    if (!users().canSee(ref)) return;
    const int i = m_maximized >= 0 ? m_maximized : m_selected;
    m_tiles[i]->playLive(ref, wantMain(i));
    if (m_maximized < 0) select((i + 1) % m_count);
}

void LiveView::autoFill()
{
    m_maximized = -1;
    int i = 0;
    for (int n = 0; n < cfg().nvrs.size() && i < m_count; ++n)
        for (int ch = 1; ch <= cfg().nvrs[n].channels && i < m_count; ++ch) {
            if (!users().canSee({n, ch})) continue;
            m_tiles[i]->playLive({n, ch}, wantMain(i));
            ++i;
        }
    relayout();
}

void LiveView::closeAll()
{
    for (VideoTile *t : m_tiles) t->stopStream();
    m_maximized = -1;
    relayout();
}

void LiveView::closeDisallowed()
{
    for (VideoTile *t : m_tiles)
        if (t->channel().valid() && !users().canSee(t->channel())) t->stopStream();
    relayout();
}

void LiveView::onDoubleClicked(VideoTile *t)
{
    m_maximized = (m_maximized == t->index()) ? -1 : t->index();
    select(t->index());
    relayout();
}

VideoTile *LiveView::tile(int i) const
{
    return (i >= 0 && i < m_tiles.size()) ? m_tiles[i] : nullptr;
}
