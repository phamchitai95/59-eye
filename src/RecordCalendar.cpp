#include "RecordCalendar.h"
#include "Theme.h"

#include <QPainter>
#include <QTextCharFormat>

RecordCalendar::RecordCalendar(QWidget *parent) : QCalendarWidget(parent)
{
    setGridVisible(false);
    setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    setFirstDayOfWeek(Qt::Monday);
    setMinimumSize(320, 250);
    // Thu 7 / CN khong to do mac dinh (de khong lan voi mau danh dau)
    QTextCharFormat wk;
    wk.setForeground(Theme::text);
    setWeekdayTextFormat(Qt::Saturday, wk);
    setWeekdayTextFormat(Qt::Sunday, wk);
}

void RecordCalendar::setMarks(const QHash<QDate, int> &marks)
{
    m_marks = marks;
    m_loading = false;
    updateCells();
}

void RecordCalendar::setLoading(bool on)
{
    m_loading = on;
    if (on) m_marks.clear();
    updateCells();
}

void RecordCalendar::paintCell(QPainter *p, const QRect &rect, QDate date) const
{
    const bool inMonth = date.month() == monthShown() && date.year() == yearShown();
    const bool selected = date == selectedDate();
    const int flags = inMonth ? m_marks.value(date, 0) : 0;
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRect r = rect.adjusted(2, 2, -2, -2);
    if (selected) {
        p->setPen(Qt::NoPen);
        p->setBrush(Theme::accent);
        p->drawRoundedRect(r, 5, 5);
    } else if (flags & HasRecord) {
        p->setPen(Qt::NoPen);
        QColor c = Theme::accent;
        c.setAlpha(70);
        p->setBrush(c);
        p->drawRoundedRect(r, 5, 5);
    }
    if (date == QDate::currentDate() && !selected) {
        p->setPen(QPen(Theme::textDim, 1));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(r, 5, 5);
    }
    QFont f = p->font();
    f.setBold(flags & HasRecord);
    p->setFont(f);
    QColor tc = !inMonth ? QColor("#5e4a4d") : (flags & HasRecord || selected) ? QColor("#ffffff") : Theme::textDim;
    p->setPen(tc);
    p->drawText(r, Qt::AlignCenter, QString::number(date.day()));
    if (flags & HasEvent) {
        p->setPen(Qt::NoPen);
        p->setBrush(Theme::warn);
        p->drawEllipse(QPointF(r.center().x() + 0.5, r.bottom() - 4), 2.5, 2.5);
    }
    p->restore();
}
