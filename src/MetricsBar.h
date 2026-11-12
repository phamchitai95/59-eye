#pragma once
#include "Metrics.h"

#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

// Thanh do hieu nang o day cua so: ung dung nay + tien trinh de so sanh (vd SmartPSS.exe),
// co the ghi ra CSV de doi chieu sau.
class MetricsBar : public QWidget {
    Q_OBJECT
public:
    explicit MetricsBar(QWidget *parent = nullptr);
    void toggleRecord();
    void setCompare(const QString &exe);

private:
    void tick();
    static QString fmt(const ProcSample &s);

    ProcessMonitor m_self;
    ProcessMonitor m_cmp;
    QLabel *m_selfLbl;
    QLabel *m_sysLbl;
    QLabel *m_cmpLbl;
    QLineEdit *m_cmpName;
    QPushButton *m_recBtn;
    QFile m_csv;
    QTimer m_timer;
};
