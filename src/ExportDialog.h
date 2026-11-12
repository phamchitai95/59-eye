#pragma once
#include "Config.h"

#include <QDialog>

class ClipExporter;
class QDateTimeEdit;
class QComboBox;
class QCheckBox;
class QLineEdit;
class QProgressBar;
class QLabel;
class QPushButton;

// Hop thoai trich doan ban ghi ra MP4
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    ExportDialog(const ChannelRef &ref, const QDateTime &from, const QDateTime &to, bool mainStream,
                 QWidget *parent = nullptr);
    // Chay khong giao dien (thu nghiem): tra ve ngay, ket qua qua finished cua dialog
    void startNow(const QString &outFile);

protected:
    void reject() override;

private:
    void start();
    void updateDefaultName();
    void setRunning(bool on);

    ChannelRef m_ref;
    QDateTimeEdit *m_from, *m_to;
    QComboBox *m_stream;
    QCheckBox *m_audio;
    QLineEdit *m_path;
    QProgressBar *m_bar;
    QLabel *m_status;
    QPushButton *m_startBtn, *m_stopBtn, *m_openBtn, *m_closeBtn, *m_browse;
    ClipExporter *m_exp;
    bool m_pathEdited = false;
    QString m_lastFile;
};
