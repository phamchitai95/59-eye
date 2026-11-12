#include "AdjustDialog.h"
#include "ImageSettings.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

AdjustDialog::AdjustDialog(GstPlayer *player, const ChannelRef &ref, const QString &title, QWidget *parent)
    : QDialog(parent), m_player(player), m_ref(ref), m_original(player->color())
{
    setWindowTitle(QStringLiteral("Chỉnh hình · %1").arg(title));
    setMinimumWidth(380);
    auto *root = new QVBoxLayout(this);
    root->setSpacing(10);

    if (!player->colorSupported()) {
        auto *warn = new QLabel(QStringLiteral("Bộ hiển thị hiện tại không hỗ trợ chỉnh màu trên GPU."), this);
        warn->setStyleSheet("color:#f0a020;");
        root->addWidget(warn);
    }

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    auto mk = [&](int row, const QString &name, int min, int max, QSlider *&s, QLabel *&v) {
        grid->addWidget(new QLabel(name, this), row, 0);
        s = new QSlider(Qt::Horizontal, this);
        s->setRange(min, max);
        grid->addWidget(s, row, 1);
        v = new QLabel(this);
        v->setMinimumWidth(40);
        v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(v, row, 2);
        connect(s, &QSlider::valueChanged, this, &AdjustDialog::preview);
    };
    // luu x100: brightness -1..1 -> dung -50..50 cho de chinh; contrast/saturation 0..2 -> 50..170
    mk(0, QStringLiteral("Độ sáng"), -50, 50, m_b, m_bv);
    mk(1, QStringLiteral("Tương phản"), 50, 170, m_c, m_cv);
    mk(2, QStringLiteral("Độ đậm màu"), 0, 200, m_s, m_sv);
    mk(3, QStringLiteral("Sắc độ"), -30, 30, m_h, m_hv);
    root->addLayout(grid);

    auto *presets = new QHBoxLayout;
    presets->addWidget(new QLabel(QStringLiteral("Mẫu:"), this));
    auto addPreset = [&](const QString &name, ColorAdjust c) {
        auto *b = new QPushButton(name, this);
        connect(b, &QPushButton::clicked, this, [this, c] { setValues(c); });
        presets->addWidget(b);
    };
    addPreset(QStringLiteral("Gốc"), ImageSettings::presetOriginal());
    addPreset(QStringLiteral("Nịnh mắt"), ImageSettings::presetPleasant());
    addPreset(QStringLiteral("Ban đêm"), ImageSettings::presetNight());
    presets->addStretch();
    root->addLayout(presets);

    auto *btns = new QHBoxLayout;
    auto *all = new QPushButton(QStringLiteral("Áp dụng cho mọi kênh"), this);
    all->setToolTip(QStringLiteral("Lưu làm mặc định và bỏ chỉnh riêng của các kênh khác"));
    auto *cancel = new QPushButton(QStringLiteral("Hủy"), this);
    auto *save = new QPushButton(QStringLiteral("Lưu cho kênh này"), this);
    save->setDefault(true);
    save->setStyleSheet("QPushButton{background:#e53935;border-color:#e53935;color:white;}"
                        "QPushButton:hover{background:#5a9dfd;}");
    btns->addWidget(all);
    btns->addStretch();
    btns->addWidget(cancel);
    btns->addWidget(save);
    root->addLayout(btns);

    connect(save, &QPushButton::clicked, this, [this] {
        ImageSettings::set(m_ref, values());
        accept();
    });
    connect(all, &QPushButton::clicked, this, [this] {
        ImageSettings::setDefaults(values());
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(this, &QDialog::rejected, this, [this] { m_player->setColor(m_original); });

    setValues(m_original);
}

void AdjustDialog::setValues(const ColorAdjust &c)
{
    for (QSlider *s : {m_b, m_c, m_s, m_h}) s->blockSignals(true);
    m_b->setValue(qRound(c.brightness * 100));
    m_c->setValue(qRound(c.contrast * 100));
    m_s->setValue(qRound(c.saturation * 100));
    m_h->setValue(qRound(c.hue * 100));
    for (QSlider *s : {m_b, m_c, m_s, m_h}) s->blockSignals(false);
    preview();
}

ColorAdjust AdjustDialog::values() const
{
    return {m_b->value() / 100.0, m_c->value() / 100.0, m_s->value() / 100.0, m_h->value() / 100.0};
}

void AdjustDialog::preview()
{
    const ColorAdjust c = values();
    m_bv->setText(QString::asprintf("%+d", m_b->value()));
    m_cv->setText(QStringLiteral("%1%").arg(m_c->value()));
    m_sv->setText(QStringLiteral("%1%").arg(m_s->value()));
    m_hv->setText(QString::asprintf("%+d", m_h->value()));
    m_player->setColor(c);
}
