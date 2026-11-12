#pragma once
#include <QColor>
#include <QIcon>

class QApplication;

namespace Theme {
// Mau chu dao
inline const QColor bg("#110b0c");
inline const QColor panel("#1a1112");
inline const QColor panel2("#231718");
inline const QColor border("#3a2528");
inline const QColor text("#f3eaeb");
inline const QColor textDim("#a8959a");
inline const QColor accent("#e53935");
inline const QColor ok("#2ecc71");
inline const QColor warn("#f0a020");
inline const QColor bad("#e5534b");

void apply(QApplication &app);
}

class QWidget;
// Thanh am luong dung chung (icon loa + thanh truot), luu vao ui.ini
QWidget *createVolumeControl(QWidget *parent);

// Icon ve bang QPainter de dung duoc ca Windows lan Ubuntu, khong can file anh.
namespace Icons {
QIcon grid(int side);
QIcon speaker(bool on);
QIcon sliders();
QIcon camera();
QIcon recorder();
QIcon play();
QIcon pause();
QIcon back();
QIcon forward();
QIcon fill();
QIcon closeAll();
QIcon search();
QIcon bulb();
QIcon download();
QIcon user();
QIcon gear();
QIcon app();      // logo ung dung (tu resource :/logo.png neu co)
}
