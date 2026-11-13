#pragma once
#include "Metrics.h"

#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

// Thanh do hieu nang o day cua so (CPU/RAM/GPU/mang cua ung dung), co the ghi ra CSV de doi chieu sau.
class MetricsBar : public QWidget {
    Q_OBJECT
public:
    explicit MetricsBar(QWidget *parent = nullptr);
    void toggleRecord();

private:
    void tick();
    static QString fmt(const ProcSample &s);

    ProcessMonitor m_self;
    QLabel *m_selfLbl;
    QLabel *m_sysLbl;
    QPushButton *m_recBtn;
    QFile m_csv;
    QTimer m_timer;
};
