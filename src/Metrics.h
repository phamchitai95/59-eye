#pragma once
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <memory>

struct ProcSample {
    int processCount = 0;
    double cpu = -1;        // % tren tong so nhan (0-100)
    double privateMB = -1;  // bo nho rieng (commit)
    double workingMB = -1;  // working set / RSS
    double gpuDecode = -1;  // % engine VideoDecode
    double gpu3d = -1;      // % engine 3D
    double gpuMemMB = -1;   // bo nho GPU (dedicated + shared)
};

// Do CPU/RAM/GPU cho mot nhom tien trinh: chinh ung dung nay, hoac theo ten exe (vd SmartPSS.exe)
class ProcessMonitor {
public:
    ProcessMonitor();
    ~ProcessMonitor();
    void setSelf();
    void setExeName(const QString &exe);
    QString exeName() const { return m_exe; }
    ProcSample sample();

private:
    QVector<quint32> resolvePids();
    QString m_exe;
    bool m_self = true;
    QHash<quint32, quint64> m_prevCpu; // pid -> thoi gian CPU (100ns hoac tick)
    qint64 m_prevWallNs = 0;
    int m_resolveCounter = 0;
    QVector<quint32> m_pids;
};

class SystemMetrics {
public:
    static SystemMetrics &instance();
    void collect();                 // goi 1 lan moi giay truoc khi doc
    double systemCpu() const { return m_sysCpu; }
    QString gpuName() const { return m_gpuName; }
    double gpuEngine(const QVector<quint32> &pids, const QString &engType) const;
    double gpuMemMB(const QVector<quint32> &pids) const;
    bool gpuAvailable() const { return m_gpuOk; }

private:
    SystemMetrics();
    struct Impl;
    std::unique_ptr<Impl> d;
    double m_sysCpu = -1;
    bool m_gpuOk = false;
    QString m_gpuName;
};
