#include "Theme.h"
#include "AppInfo.h"
#include "GstPlayer.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLinearGradient>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>
#include <functional>

void Theme::apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, panel);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, bg);
    p.setColor(QPalette::AlternateBase, panel2);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, panel2);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, panel2);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, textDim);
    p.setColor(QPalette::Disabled, QPalette::Text, textDim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, textDim);
    app.setPalette(p);

    QFont f = app.font();
#ifdef _WIN32
    f.setFamily("Segoe UI");
#endif
    f.setPointSizeF(9.5);
    app.setFont(f);

    app.setStyleSheet(QStringLiteral(R"(
QMainWindow, QDialog { background:#110b0c; }
QToolTip { background:#231718; color:#f3eaeb; border:1px solid #3a2528; padding:4px 6px; border-radius:4px; }
QSplitter::handle { background:#110b0c; width:4px; }

QTreeWidget { background:#1a1112; border:none; padding:6px 2px; outline:0; }
QTreeWidget::item { height:26px; border-radius:6px; padding-left:2px; color:#dccfd1; }
QTreeWidget::item:hover { background:#2c1b1d; }
QTreeWidget::item:selected { background:#5a1a20; color:#ffffff; }
QHeaderView::section { background:#1a1112; color:#a8959a; border:none; padding:8px 6px; font-weight:600; }

QTabWidget::pane { border:none; background:#110b0c; }
QTabBar::tab { background:transparent; color:#a8959a; padding:8px 18px; border:none; font-weight:600; }
QTabBar::tab:selected { color:#ffffff; border-bottom:2px solid #e53935; }
QTabBar::tab:hover:!selected { color:#dccfd1; }

QPushButton, QToolButton { background:#231718; color:#f3eaeb; border:1px solid #3a2528; border-radius:7px; padding:5px 12px; }
QPushButton:hover, QToolButton:hover { background:#2e1d1f; border-color:#563236; }
QPushButton:pressed, QToolButton:pressed { background:#3a2226; }
QPushButton:checked, QToolButton:checked { background:#5a1a20; border-color:#e53935; color:#ffffff; }
QPushButton:disabled { color:#6e5a5d; }
QToolButton#tileBtn { background:transparent; border:none; padding:1px; border-radius:4px; }
QToolButton#tileBtn:hover { background:rgba(255,255,255,0.12); }
QToolButton#tileBtn:checked { background:rgba(229,57,53,0.35); }

QComboBox, QDateEdit, QLineEdit { background:#231718; color:#f3eaeb; border:1px solid #3a2528; border-radius:7px; padding:4px 8px; min-height:20px; }
QComboBox:hover, QDateEdit:hover, QLineEdit:hover { border-color:#563236; }
QComboBox QAbstractItemView { background:#231718; border:1px solid #3a2528; selection-background-color:#5a1a20; outline:0; }
QComboBox::drop-down, QDateEdit::drop-down { border:none; width:18px; }

QCheckBox { color:#dccfd1; spacing:6px; }
QCheckBox::indicator { width:16px; height:16px; border-radius:4px; border:1px solid #563236; background:#231718; }
QCheckBox::indicator:checked { background:#e53935; border-color:#e53935; }

QSlider::groove:horizontal { height:4px; background:#3a2528; border-radius:2px; }
QSlider::sub-page:horizontal { background:#e53935; border-radius:2px; }
QSlider::handle:horizontal { background:#f3eaeb; width:14px; height:14px; margin:-5px 0; border-radius:7px; }

QStatusBar { background:#1a1112; border-top:1px solid #2c1b1d; }
QStatusBar QLabel { color:#c4b3b6; }
QScrollBar:vertical { background:transparent; width:10px; }
QScrollBar::handle:vertical { background:#3a2528; border-radius:5px; min-height:24px; }
QScrollBar::add-line, QScrollBar::sub-line { height:0; }
QMenu { background:#231718; border:1px solid #3a2528; padding:4px; }
QMenu::item { padding:5px 18px; border-radius:4px; }
QMenu::item:selected { background:#5a1a20; }
QCalendarWidget QWidget { background:#231718; color:#f3eaeb; }
)"));
}

// ------------------------------- Am luong -------------------------------

static QSettings &uiStore()
{
    static QSettings s(AppInfo::dataFile("ui.ini"), QSettings::IniFormat);
    return s;
}

QWidget *createVolumeControl(QWidget *parent)
{
    static bool loaded = false;
    if (!loaded) {
        GstPlayer::setGlobalVolume(uiStore().value("volume", 0.6).toDouble());
        loaded = true;
    }
    auto *w = new QWidget(parent);
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    auto *icon = new QLabel(w);
    icon->setPixmap(Icons::speaker(true).pixmap(16, 16));
    l->addWidget(icon);
    auto *s = new QSlider(Qt::Horizontal, w);
    s->setRange(0, 150);
    s->setFixedWidth(110);
    s->setValue(qRound(GstPlayer::globalVolume() * 100));
    s->setToolTip(QStringLiteral("Âm lượng tiếng camera. Nếu bị hú: giảm âm lượng hoặc dùng tai nghe"));
    l->addWidget(s);
    auto *val = new QLabel(QStringLiteral("%1%").arg(s->value()), w);
    val->setMinimumWidth(34);
    val->setStyleSheet("color:#a8959a;");
    l->addWidget(val);
    QObject::connect(s, &QSlider::valueChanged, w, [val](int v) {
        val->setText(QStringLiteral("%1%").arg(v));
        GstPlayer::setGlobalVolume(v / 100.0);
        uiStore().setValue("volume", v / 100.0);
    });
    // dong bo cac thanh am luong khac (tab kia) khi nguoi dung chuyen focus
    QObject::connect(qApp, &QApplication::focusChanged, w, [s] {
        const int v = qRound(GstPlayer::globalVolume() * 100);
        if (s->value() != v) { QSignalBlocker b(s); s->setValue(v); }
    });
    return w;
}

// ------------------------------- Icons -------------------------------

static QIcon draw(const std::function<void(QPainter &)> &fn, const QColor &c = Theme::text)
{
    QIcon icon;
    for (int sz : {16, 20, 24, 32, 48}) {
        QPixmap pm(sz, sz);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(sz / 24.0, sz / 24.0);
        p.setPen(QPen(c, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        fn(p);
        icon.addPixmap(pm);
    }
    return icon;
}

QIcon Icons::grid(int side)
{
    return draw([side](QPainter &p) {
        const double gap = side >= 4 ? 1.2 : 2.0;
        const double cell = (18.0 - gap * (side - 1)) / side;
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::text);
        for (int r = 0; r < side; ++r)
            for (int c = 0; c < side; ++c)
                p.drawRoundedRect(QRectF(3 + c * (cell + gap), 3 + r * (cell + gap), cell, cell), 1.2, 1.2);
    });
}

QIcon Icons::speaker(bool on)
{
    return draw([on](QPainter &p) {
        QPainterPath body;
        body.moveTo(4, 9); body.lineTo(8, 9); body.lineTo(13, 5); body.lineTo(13, 19);
        body.lineTo(8, 15); body.lineTo(4, 15); body.closeSubpath();
        p.setBrush(p.pen().color());
        p.drawPath(body);
        p.setBrush(Qt::NoBrush);
        if (on) {
            p.drawArc(QRectF(11, 8, 6, 8), -60 * 16, 120 * 16);
            p.drawArc(QRectF(11, 5, 10, 14), -60 * 16, 120 * 16);
        } else {
            p.drawLine(QPointF(16, 9), QPointF(21, 15));
            p.drawLine(QPointF(21, 9), QPointF(16, 15));
        }
    }, on ? QColor("#ffffff") : Theme::textDim);
}

QIcon Icons::sliders()
{
    return draw([](QPainter &p) {
        p.drawLine(QPointF(4, 7), QPointF(20, 7));
        p.drawLine(QPointF(4, 17), QPointF(20, 17));
        p.setBrush(Theme::panel2);
        p.drawEllipse(QPointF(9, 7), 2.6, 2.6);
        p.drawEllipse(QPointF(15, 17), 2.6, 2.6);
    });
}

QIcon Icons::camera()
{
    return draw([](QPainter &p) {
        p.drawRoundedRect(QRectF(3, 7, 13, 10), 2, 2);
        QPainterPath lens;
        lens.moveTo(16, 10); lens.lineTo(21, 7); lens.lineTo(21, 17); lens.lineTo(16, 14);
        p.drawPath(lens);
    }, Theme::textDim);
}

QIcon Icons::recorder()
{
    return draw([](QPainter &p) {
        p.drawRoundedRect(QRectF(3, 7, 18, 10), 2, 2);
        p.setBrush(Theme::ok);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(7, 12), 1.6, 1.6);
        p.setBrush(Theme::textDim);
        p.drawRect(QRectF(11, 11, 7, 2));
    });
}

QIcon Icons::play()
{
    return draw([](QPainter &p) {
        QPainterPath t;
        t.moveTo(8, 5); t.lineTo(19, 12); t.lineTo(8, 19); t.closeSubpath();
        p.setBrush(p.pen().color());
        p.drawPath(t);
    });
}

QIcon Icons::pause()
{
    return draw([](QPainter &p) {
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::text);
        p.drawRoundedRect(QRectF(6, 5, 4, 14), 1, 1);
        p.drawRoundedRect(QRectF(14, 5, 4, 14), 1, 1);
    });
}

QIcon Icons::back()
{
    return draw([](QPainter &p) {
        QPainterPath t;
        t.moveTo(12, 6); t.lineTo(4, 12); t.lineTo(12, 18); t.closeSubpath();
        t.moveTo(20, 6); t.lineTo(12, 12); t.lineTo(20, 18); t.closeSubpath();
        p.setBrush(p.pen().color());
        p.drawPath(t);
    });
}

QIcon Icons::forward()
{
    return draw([](QPainter &p) {
        QPainterPath t;
        t.moveTo(4, 6); t.lineTo(12, 12); t.lineTo(4, 18); t.closeSubpath();
        t.moveTo(12, 6); t.lineTo(20, 12); t.lineTo(12, 18); t.closeSubpath();
        p.setBrush(p.pen().color());
        p.drawPath(t);
    });
}

QIcon Icons::fill()
{
    return draw([](QPainter &p) {
        p.drawRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
        p.drawLine(QPointF(12, 8), QPointF(12, 16));
        p.drawLine(QPointF(8, 12), QPointF(16, 12));
    });
}

QIcon Icons::closeAll()
{
    return draw([](QPainter &p) {
        p.drawLine(QPointF(6, 6), QPointF(18, 18));
        p.drawLine(QPointF(18, 6), QPointF(6, 18));
    });
}

QIcon Icons::bulb()
{
    return draw([](QPainter &p) {
        QPainterPath b;
        b.addEllipse(QPointF(12, 10), 6, 6);
        p.drawPath(b);
        p.drawLine(QPointF(9.5, 17), QPointF(14.5, 17));
        p.drawLine(QPointF(10, 20), QPointF(14, 20));
        p.drawLine(QPointF(12, 1.5), QPointF(12, 2.5));
        p.drawLine(QPointF(3, 10), QPointF(4, 10));
        p.drawLine(QPointF(20, 10), QPointF(21, 10));
    });
}

QIcon Icons::search()
{
    return draw([](QPainter &p) {
        p.drawEllipse(QPointF(10.5, 10.5), 6, 6);
        p.drawLine(QPointF(15, 15), QPointF(20, 20));
    });
}

QIcon Icons::download()
{
    return draw([](QPainter &p) {
        p.drawLine(QPointF(12, 4), QPointF(12, 15));
        p.drawLine(QPointF(7.5, 10.5), QPointF(12, 15));
        p.drawLine(QPointF(16.5, 10.5), QPointF(12, 15));
        p.drawLine(QPointF(5, 19.5), QPointF(19, 19.5));
    });
}

QIcon Icons::user()
{
    return draw([](QPainter &p) {
        p.drawEllipse(QPointF(12, 8.5), 3.8, 3.8);
        p.drawArc(QRectF(5, 14, 14, 12), 20 * 16, 140 * 16);
    });
}

QIcon Icons::gear()
{
    return draw([](QPainter &p) {
        p.drawEllipse(QPointF(12, 12), 3, 3);
        for (int i = 0; i < 8; ++i) {
            p.save();
            p.translate(12, 12);
            p.rotate(i * 45);
            p.drawLine(QPointF(0, -6), QPointF(0, -9));
            p.restore();
        }
        p.drawEllipse(QPointF(12, 12), 6.2, 6.2);
    });
}

QIcon Icons::app()
{
    static QIcon icon;
    if (!icon.isNull()) return icon;
    if (QFile::exists(":/icon.png")) {
        icon = QIcon(":/icon.png");
        return icon;
    }
    // du phong: o vuong do voi chu 59
    for (int sz : {16, 24, 32, 48, 64, 256}) {
        QPixmap pm(sz, sz);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, sz, sz);
        g.setColorAt(0, QColor("#e53935"));
        g.setColorAt(1, QColor("#5a0d16"));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawRoundedRect(QRectF(0, 0, sz, sz), sz * 0.18, sz * 0.18);
        QFont f("Segoe UI");
        f.setBold(true);
        f.setPixelSize(int(sz * 0.52));
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRectF(0, 0, sz, sz), Qt::AlignCenter, "59");
        icon.addPixmap(pm);
    }
    return icon;
}
