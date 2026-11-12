#pragma once
#include <QCalendarWidget>
#include <QHash>

// Lich chon ngay: danh dau ngay co ban ghi (xanh) va ngay co su kien (cham cam).
class RecordCalendar : public QCalendarWidget {
    Q_OBJECT
public:
    enum Flag { HasRecord = 1, HasEvent = 2 };
    explicit RecordCalendar(QWidget *parent = nullptr);
    void setMarks(const QHash<QDate, int> &marks);
    void setLoading(bool on);

protected:
    void paintCell(QPainter *p, const QRect &rect, QDate date) const override;

private:
    QHash<QDate, int> m_marks;
    bool m_loading = false;
};
