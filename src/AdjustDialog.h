#pragma once
#include "Config.h"
#include "GstPlayer.h"

#include <QDialog>

class QSlider;
class QLabel;

// Bang chinh sang / tuong phan / mau / sac do cho 1 kenh, ap dung ngay tren GPU.
class AdjustDialog : public QDialog {
    Q_OBJECT
public:
    AdjustDialog(GstPlayer *player, const ChannelRef &ref, const QString &title, QWidget *parent = nullptr);

private:
    void setValues(const ColorAdjust &c);
    ColorAdjust values() const;
    void preview();

    GstPlayer *m_player;
    ChannelRef m_ref;
    ColorAdjust m_original;
    QSlider *m_b, *m_c, *m_s, *m_h;
    QLabel *m_bv, *m_cv, *m_sv, *m_hv;
};
