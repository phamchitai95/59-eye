#pragma once
#include "Config.h"

#include <QDateTime>
#include <QImage>
#include <QThread>
#include <QWidget>
#include <functional>

class CountWorker;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QNetworkAccessManager;

// Tab "Dem luong": nhan vien chuot phai o camera -> "Dem so nguoi" -> them vao hang doi o day.
// Moi luot: chup 8 khung luong chinh trong 30 giay, nhan dien dau nguoi, dong thuan nhieu khung,
// toi da 5 phut, hien anh ket luan co danh so de nhan vien tu kiem. Khong luu lai gi.
class CountView : public QWidget {
    Q_OBJECT
public:
    explicit CountView(QWidget *parent = nullptr);
    ~CountView() override;
    // from hop le = dem tu ban ghi tai thoi diem do (chan doan / thu nghiem)
    void enqueue(const ChannelRef &ref, const QDateTime &from = {});

    static QString modelPath();   // noi luu mo hinh (tai lan dau)

signals:
    void jobFinished(bool ok, int count, const QImage &result, const QString &message);

private:
    struct Job {
        enum State { Waiting, Running, Done, Failed, Canceled };
        int id = 0;
        ChannelRef ref;
        QString title;
        QDateTime created;
        QDateTime from; // ban ghi (thu nghiem)
        State state = Waiting;
        int percent = 0;
        QString step;
        int count = -1;
        QImage result;
        QString message;
    };
    Job *job(int id);
    void startNext();
    void ensureModel(int jobId, std::function<void(bool ok, const QString &err)> done);
    void refreshItem(int id);
    void showJob(int id);
    void onProgress(int id, int percent, const QString &step);
    void onFinished(int id, bool ok, int count, const QImage &img, const QString &msg);

    QVector<Job> m_jobs;
    int m_nextId = 1;
    int m_running = -1;
    int m_shown = -1;
    bool m_downloading = false;

    QListWidget *m_list;
    QLabel *m_title, *m_count, *m_step, *m_msg;
    QProgressBar *m_bar;
    class ResultImage *m_image;
    QPushButton *m_cancelBtn, *m_retryBtn, *m_saveBtn, *m_removeBtn;
    QThread m_thread;
    CountWorker *m_worker;
    QNetworkAccessManager *m_net;
};
