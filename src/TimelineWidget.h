#pragma once
#include "DahuaClient.h"

#include <QDate>
#include <QWidget>

// Thanh thoi gian 24h: to mau doan da ghi, vach dang phat, nhap de tua, lan chuot de phong to.
class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget *parent = nullptr);

    void setDay(const QDate &d);
    QDate day() const { return m_day; }
    void setSegments(const QVector<RecordSegment> &s);
    void setPlayhead(const QDateTime &t);
    QDateTime firstRecordStart() const;

signals:
    void seekRequested(const QDateTime &t);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void leaveEvent(QEvent *) override;

private:
    qint64 xToMs(double x) const;
    double msToX(qint64 ms) const;
    qint64 toDayMs(const QDateTime &t) const;
    void clampView();

    QDate m_day = QDate::currentDate();
    QVector<RecordSegment> m_segs;
    qint64 m_play = -1;          // ms tinh tu 00:00 cua ngay
    qint64 m_viewStart = 0;
    qint64 m_viewSpan = 24LL * 3600 * 1000;
    int m_hoverX = -1;
    bool m_dragging = false;
    int m_dragStartX = 0;
    qint64 m_dragStartView = 0;
    bool m_moved = false;
};
