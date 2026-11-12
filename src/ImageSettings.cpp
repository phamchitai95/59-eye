#include "ImageSettings.h"
#include "AppInfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

static QSettings &store()
{
    static QSettings s(AppInfo::dataFile("image.ini"), QSettings::IniFormat);
    return s;
}

static QString oldKey(const ChannelRef &r)
{
    return QStringLiteral("nvr%1_ch%2").arg(r.nvr).arg(r.ch);
}

// Khoa theo ma thiet bi (khong doi khi them/xoa thiet bi khac); van doc duoc khoa cu "nvrN_chM"
static QString key(const ChannelRef &r)
{
    if (r.nvr < 0 || r.nvr >= cfg().nvrs.size()) return oldKey(r);
    const QString k = QStringLiteral("dev_%1_ch%2").arg(cfg().nvrs[r.nvr].id).arg(r.ch);
    if (!store().childGroups().contains(k) && store().childGroups().contains(oldKey(r))) return oldKey(r);
    return k;
}

static ColorAdjust read(const QString &group, const ColorAdjust &fallback)
{
    QSettings &s = store();
    if (!s.childGroups().contains(group)) return fallback;
    s.beginGroup(group);
    ColorAdjust c;
    c.brightness = s.value("brightness", fallback.brightness).toDouble();
    c.contrast = s.value("contrast", fallback.contrast).toDouble();
    c.saturation = s.value("saturation", fallback.saturation).toDouble();
    c.hue = s.value("hue", fallback.hue).toDouble();
    s.endGroup();
    return c;
}

static void write(const QString &group, const ColorAdjust &c)
{
    QSettings &s = store();
    s.beginGroup(group);
    s.setValue("brightness", c.brightness);
    s.setValue("contrast", c.contrast);
    s.setValue("saturation", c.saturation);
    s.setValue("hue", c.hue);
    s.endGroup();
    s.sync();
}

namespace ImageSettings {

ColorAdjust presetOriginal() { return {}; }
ColorAdjust presetPleasant() { return {0.02, 1.08, 1.18, 0.0}; }
ColorAdjust presetNight() { return {0.10, 1.15, 1.05, 0.0}; }

ColorAdjust defaults() { return read("default", presetPleasant()); }

void setDefaults(const ColorAdjust &c)
{
    QSettings &s = store();
    for (const QString &g : s.childGroups())
        if (g.startsWith("nvr") || g.startsWith("dev_")) s.remove(g);
    write("default", c);
}

ColorAdjust get(const ChannelRef &r)
{
    return r.valid() ? read(key(r), defaults()) : defaults();
}

void set(const ChannelRef &r, const ColorAdjust &c)
{
    if (!r.valid()) return;
    store().remove(oldKey(r));
    write(QStringLiteral("dev_%1_ch%2").arg(cfg().nvrs[r.nvr].id).arg(r.ch), c);
}

bool hasCustom(const ChannelRef &r)
{
    return r.valid() && store().childGroups().contains(key(r));
}

}
