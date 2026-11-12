#include "TimelineWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>

static constexpr qint64 kDay = 24LL * 3600 * 1000;
static constexpr qint64 kMinSpan = 5LL * 60 * 1000;

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(72);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Nhấp để tua tới thời điểm · Lăn chuột để phóng to/thu nhỏ · Kéo để di chuyển"));
}

void TimelineWidget::setDay(const QDate &d)
{
    m_day = d;
    m_segs.clear();
    m_play = -1;
    update();
}

void TimelineWidget::setSegments(const QVector<RecordSegment> &s)
{
    m_segs = s;
    update();
}

void TimelineWidget::setPlayhead(const QDateTime &t)
{
    m_play = t.isValid() ? toDayMs(t) : -1;
    // tu cuon theo vach dang phat khi dang phong to
    if (m_play >= 0 && !m_dragging && (m_play < m_viewStart || m_play > m_viewStart + m_viewSpan)) {
        m_viewStart = m_play - m_viewSpan / 4;
        clampView();
    }
    update();
}

QDateTime TimelineWidget::firstRecordStart() const
{
    const QDateTime dayStart(m_day, QTime(0, 0));
    for (const RecordSegment &s : m_segs)
        if (s.end > dayStart) return std::max(s.start, dayStart);
    return {};
}

qint64 TimelineWidget::toDayMs(const QDateTime &t) const
{
    return QDateTime(m_day, QTime(0, 0)).msecsTo(t);
}

qint64 TimelineWidget::xToMs(double x) const
{
    return m_viewStart + qint64(x / std::max(1, width()) * m_viewSpan);
}

double TimelineWidget::msToX(qint64 ms) const
{
    return double(ms - m_viewStart) / m_viewSpan * width();
}

void TimelineWidget::clampView()
{
    m_viewSpan = std::clamp(m_viewSpan, kMinSpan, kDay);
    m_viewStart = std::clamp(m_viewStart, qint64(0), kDay - m_viewSpan);
}

void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int w = width(), h = height();
    p.fillRect(rect(), QColor("#110b0c"));

    const int barTop = 26, barH = h - barTop - 8;
    p.fillRect(QRect(0, barTop, w, barH), QColor("#1a1e25"));

    // doan da ghi
    const QDateTime dayStart(m_day, QTime(0, 0));
    for (const RecordSegment &s : m_segs) {
        const double x1 = msToX(dayStart.msecsTo(s.start));
        const double x2 = msToX(dayStart.msecsTo(s.end));
        if (x2 < 0 || x1 > w) continue;
        const QRectF r(std::max(0.0, x1), barTop, std::max(1.0, std::min<double>(w, x2) - std::max(0.0, x1)), barH);
        p.fillRect(r, s.event ? QColor("#f0a020") : QColor("#e53935"));
    }

    // vach chia
    static const qint64 steps[] = {60000, 5 * 60000, 10 * 60000, 15 * 60000, 30 * 60000,
                                   3600000, 2 * 3600000, 3 * 3600000, 6 * 3600000};
    qint64 step = steps[8];
    for (qint64 s : steps)
        if (double(s) / m_viewSpan * w >= 70) { step = s; break; }
    p.setPen(QColor("#a8959a"));
    QFont f = font();
    f.setPointSizeF(8);
    p.setFont(f);
    for (qint64 t = (m_viewStart / step) * step; t <= m_viewStart + m_viewSpan; t += step) {
        const double x = msToX(t);
        if (x < 0) continue;
        p.drawLine(QPointF(x, barTop - 6), QPointF(x, barTop));
        const QTime tm = QTime(0, 0).addMSecs(int(t % kDay));
        const QString label = (t == kDay) ? QStringLiteral("24:00") : tm.toString("HH:mm");
        p.drawText(QPointF(x + 2, 14), label);
    }

    // vach dang phat
    if (m_play >= 0) {
        const double x = msToX(m_play);
        p.setPen(QPen(QColor("#ff3b3b"), 2));
        p.drawLine(QPointF(x, 18), QPointF(x, h));
    }
    // vach chuot
    if (m_hoverX >= 0) {
        p.setPen(QPen(QColor(255, 255, 255, 120), 1, Qt::DashLine));
        p.drawLine(m_hoverX, barTop, m_hoverX, h);
        const QTime tm = QTime(0, 0).addMSecs(int(std::clamp(xToMs(m_hoverX), qint64(0), kDay - 1)));
        const QString txt = tm.toString("HH:mm:ss");
        const int tw = p.fontMetrics().horizontalAdvance(txt) + 8;
        const int tx = std::min(m_hoverX + 4, w - tw);
        p.fillRect(QRect(tx, barTop + 2, tw, 16), QColor(0, 0, 0, 180));
        p.setPen(Qt::white);
        p.drawText(QRect(tx, barTop + 2, tw, 16), Qt::AlignCenter, txt);
    }
}

void TimelineWidget::mousePressEvent(QMouseEvent *e)
{
    m_dragging = true;
    m_moved = false;
    m_dragStartX = int(e->position().x());
    m_dragStartView = m_viewStart;
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *e)
{
    m_hoverX = int(e->position().x());
    if (m_dragging) {
        const int dx = m_hoverX - m_dragStartX;
        if (std::abs(dx) > 3) m_moved = true;
        if (m_moved) {
            m_viewStart = m_dragStartView - qint64(double(dx) / std::max(1, width()) * m_viewSpan);
            clampView();
        }
    }
    update();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *e)
{
    const bool click = m_dragging && !m_moved;
    m_dragging = false;
    if (click && e->button() == Qt::LeftButton) {
        const qint64 ms = std::clamp(xToMs(e->position().x()), qint64(0), kDay - 1000);
        emit seekRequested(QDateTime(m_day, QTime(0, 0)).addMSecs(ms));
    }
}

void TimelineWidget::wheelEvent(QWheelEvent *e)
{
    const double x = e->position().x();
    const qint64 anchor = xToMs(x);
    const double factor = e->angleDelta().y() > 0 ? 0.7 : 1.0 / 0.7;
    m_viewSpan = qint64(m_viewSpan * factor);
    m_viewSpan = std::clamp(m_viewSpan, kMinSpan, kDay);
    m_viewStart = anchor - qint64(x / std::max(1, width()) * m_viewSpan);
    clampView();
    update();
}

void TimelineWidget::leaveEvent(QEvent *)
{
    m_hoverX = -1;
    update();
}
