#pragma once
#include "Config.h"
#include "GstPlayer.h"

// Luu chinh mau theo tung kenh vao image.ini canh file chay.
// Kenh chua chinh rieng se dung "mac dinh" (ban dau la kieu "Nịnh mắt").
namespace ImageSettings {
ColorAdjust presetOriginal();
ColorAdjust presetPleasant();   // tang nhe tuong phan + mau
ColorAdjust presetNight();      // sang hon cho cam toi / ban dem

ColorAdjust defaults();
void setDefaults(const ColorAdjust &c);   // ap dung cho moi kenh, xoa chinh rieng
ColorAdjust get(const ChannelRef &r);
void set(const ChannelRef &r, const ColorAdjust &c);
bool hasCustom(const ChannelRef &r);
}
