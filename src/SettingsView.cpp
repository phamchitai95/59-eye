#include "SettingsView.h"
#include "Config.h"
#include "DahuaClient.h"
#include "Theme.h"
#include "Users.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

static QLabel *hintLabel(const QString &t, QWidget *parent)
{
    auto *l = new QLabel(t, parent);
    l->setWordWrap(true);
    l->setStyleSheet("color:#a8959a;");
    return l;
}

// ======================= Hop thoai thiet bi =======================

static bool deviceDialog(QWidget *parent, NvrConfig &n, bool isNew)
{
    const bool rtsp = !n.isDahua();
    QDialog dlg(parent);
    dlg.setWindowTitle(isNew ? (rtsp ? QStringLiteral("Thêm camera RTSP") : QStringLiteral("Thêm đầu ghi Dahua"))
                             : QStringLiteral("Sửa thiết bị"));
    dlg.setMinimumWidth(rtsp ? 560 : 420);
    auto *root = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout;
    form->setSpacing(8);
    root->addLayout(form);

    auto *name = new QLineEdit(n.name, &dlg);
    auto *host = new QLineEdit(n.host, &dlg);
    auto *http = new QSpinBox(&dlg);
    http->setRange(1, 65535);
    http->setValue(n.httpPort);
    auto *rtspPort = new QSpinBox(&dlg);
    rtspPort->setRange(1, 65535);
    rtspPort->setValue(n.rtspPort);
    auto *chans = new QSpinBox(&dlg);
    chans->setRange(1, 256);
    chans->setValue(n.channels);
    auto *mainUrl = new QLineEdit(n.mainUrl, &dlg);
    auto *subUrl = new QLineEdit(n.subUrl, &dlg);
    auto *user = new QLineEdit(n.user.isEmpty() && isNew ? QStringLiteral("admin") : n.user, &dlg);
    auto *pw = new QLineEdit(&dlg);
    pw->setEchoMode(QLineEdit::Password);
    if (!isNew) pw->setPlaceholderText(QStringLiteral("(để trống = giữ mật khẩu cũ)"));

    form->addRow(QStringLiteral("Tên hiển thị"), name);
    if (rtsp) {
        mainUrl->setPlaceholderText("rtsp://192.168.1.50:554/stream1");
        subUrl->setPlaceholderText(QStringLiteral("(không bắt buộc) rtsp://192.168.1.50:554/stream2"));
        form->addRow(QStringLiteral("Luồng chính"), mainUrl);
        form->addRow(QStringLiteral("Luồng phụ"), subUrl);
    } else {
        host->setPlaceholderText("192.168.1.137");
        form->addRow(QStringLiteral("Địa chỉ IP"), host);
        form->addRow(QStringLiteral("Cổng HTTP"), http);
        form->addRow(QStringLiteral("Cổng RTSP"), rtspPort);
        form->addRow(QStringLiteral("Số kênh"), chans);
    }
    form->addRow(QStringLiteral("Tài khoản"), user);
    form->addRow(QStringLiteral("Mật khẩu"), pw);
    if (rtsp)
        root->addWidget(hintLabel(QStringLiteral("Camera lẻ chỉ xem trực tiếp (không có xem lại). Ví dụ đường dẫn camera Dahua: "
                                                 "rtsp://IP:554/cam/realmonitor?channel=1&subtype=0"), &dlg));

    auto *status = new QLabel(&dlg);
    status->setWordWrap(true);
    status->setStyleSheet("color:#a8959a;");
    root->addWidget(status);

    auto *box = new QDialogButtonBox(&dlg);
    QPushButton *test = nullptr;
    if (!rtsp) test = box->addButton(QStringLiteral("Kiểm tra kết nối"), QDialogButtonBox::ActionRole);
    box->addButton(QStringLiteral("Lưu"), QDialogButtonBox::AcceptRole)->setDefault(true);
    box->addButton(QStringLiteral("Huỷ"), QDialogButtonBox::RejectRole);
    root->addWidget(box);

    auto password = [&] { return pw->text().isEmpty() && !isNew ? n.password : pw->text(); };

    if (test) {
        // Chi doc: hoi loai thiet bi + so kenh (ChannelTitle). Khong doi gi tren dau ghi.
        QObject::connect(test, &QPushButton::clicked, &dlg, [&, test] {
            if (host->text().trimmed().isEmpty()) return status->setText(QStringLiteral("Nhập địa chỉ IP trước"));
            test->setEnabled(false);
            status->setStyleSheet("color:#a8959a;");
            status->setText(QStringLiteral("Đang kết nối..."));
            auto *c = new DahuaClient(host->text().trimmed(), http->value(), user->text(), password(), &dlg);
            QPointer<QDialog> guard(&dlg);
            c->get("/cgi-bin/magicBox.cgi?action=getDeviceType", [=](int, const QByteArray &b, const QString &e) {
                if (!guard) return;
                if (!e.isEmpty()) {
                    test->setEnabled(true);
                    status->setStyleSheet("color:#f08a84;");
                    status->setText(QStringLiteral("Lỗi: %1").arg(e));
                    c->deleteLater();
                    return;
                }
                const QString type = QString::fromUtf8(b).section('=', 1).trimmed();
                c->get("/cgi-bin/configManager.cgi?action=getConfig&name=ChannelTitle",
                       [=](int, const QByteArray &b2, const QString &) {
                    if (!guard) return;
                    c->deleteLater();
                    test->setEnabled(true);
                    static const QRegularExpression re(QStringLiteral("ChannelTitle\\[(\\d+)\\]\\.Name="));
                    int maxCh = 0;
                    for (auto it = re.globalMatch(QString::fromUtf8(b2)); it.hasNext();)
                        maxCh = qMax(maxCh, it.next().captured(1).toInt() + 1);
                    if (maxCh > 0) chans->setValue(maxCh);
                    status->setStyleSheet("color:#7ee2a8;");
                    status->setText(QStringLiteral("Kết nối được: %1%2").arg(type.isEmpty() ? QStringLiteral("thiết bị Dahua") : type,
                                    maxCh > 0 ? QStringLiteral(" · %1 kênh").arg(maxCh) : QString()));
                    if (name->text().trimmed().isEmpty()) name->setText(type);
                });
            });
        });
    }

    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        status->setStyleSheet("color:#f08a84;");
        if (rtsp) {
            if (!mainUrl->text().trimmed().startsWith("rtsp://", Qt::CaseInsensitive))
                return status->setText(QStringLiteral("Luồng chính phải bắt đầu bằng rtsp://"));
        } else if (host->text().trimmed().isEmpty()) {
            return status->setText(QStringLiteral("Nhập địa chỉ IP"));
        }
        dlg.accept();
    });
    if (dlg.exec() != QDialog::Accepted) return false;

    n.user = user->text().trimmed();
    n.password = password();
    if (rtsp) {
        n.mainUrl = mainUrl->text().trimmed();
        n.subUrl = subUrl->text().trimmed();
        n.channels = 1;
        n.name = name->text().trimmed().isEmpty() ? QStringLiteral("Camera") : name->text().trimmed();
    } else {
        n.host = host->text().trimmed();
        n.httpPort = http->value();
        n.rtspPort = rtspPort->value();
        n.channels = chans->value();
        n.name = name->text().trimmed().isEmpty() ? n.host : name->text().trimmed();
        for (auto it = n.hidden.begin(); it != n.hidden.end();)
            it = (*it > n.channels) ? n.hidden.erase(it) : std::next(it);
    }
    return true;
}

// ======================= Hop thoai tai khoan =======================

static bool userDialog(QWidget *parent, UserAccount &u, bool isNew, QString *newPassword)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(isNew ? QStringLiteral("Thêm tài khoản") : QStringLiteral("Sửa tài khoản %1").arg(u.name));
    dlg.resize(560, 620);
    auto *root = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout;
    root->addLayout(form);
    auto *name = new QLineEdit(u.name, &dlg);
    auto *role = new QComboBox(&dlg);
    role->addItem(QStringLiteral("Nhân viên"), false);
    role->addItem(QStringLiteral("Quản trị"), true);
    role->setCurrentIndex(u.admin ? 1 : 0);
    auto *pw = new QLineEdit(&dlg), *pw2 = new QLineEdit(&dlg);
    pw->setEchoMode(QLineEdit::Password);
    pw2->setEchoMode(QLineEdit::Password);
    if (!isNew) pw->setPlaceholderText(QStringLiteral("(để trống = giữ nguyên)"));
    form->addRow(QStringLiteral("Tài khoản"), name);
    form->addRow(QStringLiteral("Vai trò"), role);
    form->addRow(QStringLiteral("Mật khẩu"), pw);
    form->addRow(QStringLiteral("Nhập lại"), pw2);

    auto *perms = new QGroupBox(QStringLiteral("Quyền của nhân viên"), &dlg);
    auto *pg = new QGridLayout(perms);
    auto *cLive = new QCheckBox(QStringLiteral("Xem trực tiếp"), perms);
    auto *cPlay = new QCheckBox(QStringLiteral("Xem lại"), perms);
    auto *cExport = new QCheckBox(QStringLiteral("Trích đoạn ra MP4"), perms);
    auto *cAudio = new QCheckBox(QStringLiteral("Nghe âm thanh"), perms);
    auto *cLight = new QCheckBox(QStringLiteral("Bật/tắt đèn camera"), perms);
    auto *cAdjust = new QCheckBox(QStringLiteral("Chỉnh màu hình ảnh"), perms);
    auto *cCount = new QCheckBox(QStringLiteral("Đếm số người"), perms);
    cLive->setChecked(u.live);
    cPlay->setChecked(u.playback);
    cExport->setChecked(u.exportClip);
    cAudio->setChecked(u.audio);
    cLight->setChecked(u.light);
    cAdjust->setChecked(u.adjust);
    cCount->setChecked(u.count);
    pg->addWidget(cLive, 0, 0);
    pg->addWidget(cPlay, 0, 1);
    pg->addWidget(cExport, 0, 2);
    pg->addWidget(cAudio, 1, 0);
    pg->addWidget(cLight, 1, 1);
    pg->addWidget(cAdjust, 1, 2);
    pg->addWidget(cCount, 2, 0);
    root->addWidget(perms);

    auto *chBox = new QGroupBox(QStringLiteral("Kênh được xem"), &dlg);
    auto *cl = new QVBoxLayout(chBox);
    auto *rAll = new QRadioButton(QStringLiteral("Tất cả kênh"), chBox);
    auto *rSome = new QRadioButton(QStringLiteral("Chỉ các kênh được chọn"), chBox);
    (u.allChannels ? rAll : rSome)->setChecked(true);
    auto *rl = new QHBoxLayout;
    rl->addWidget(rAll);
    rl->addWidget(rSome);
    rl->addStretch();
    cl->addLayout(rl);
    auto *tree = new QTreeWidget(chBox);
    tree->setHeaderHidden(true);
    for (int d = 0; d < cfg().nvrs.size(); ++d) {
        const NvrConfig &n = cfg().nvrs[d];
        auto *top = new QTreeWidgetItem(tree, {n.name});
        top->setFlags(top->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        top->setIcon(0, n.isDahua() ? Icons::recorder() : Icons::camera());
        for (int ch = 1; ch <= n.channels; ++ch) {
            const ChannelRef r{d, ch};
            auto *it = new QTreeWidgetItem(top, {QStringLiteral("%1  %2").arg(ch, 2, 10, QChar('0')).arg(cfg().shortTitle(r))});
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setData(0, Qt::UserRole, cfg().channelKey(r));
            it->setCheckState(0, u.channels.contains(cfg().channelKey(r)) ? Qt::Checked : Qt::Unchecked);
        }
        if (n.channels > 0 && n.channels <= 1) top->setExpanded(true);
    }
    cl->addWidget(tree, 1);
    root->addWidget(chBox, 1);

    auto *err = new QLabel(&dlg);
    err->setStyleSheet("color:#f08a84;");
    root->addWidget(err);
    auto *box = new QDialogButtonBox(&dlg);
    box->addButton(QStringLiteral("Lưu"), QDialogButtonBox::AcceptRole)->setDefault(true);
    box->addButton(QStringLiteral("Huỷ"), QDialogButtonBox::RejectRole);
    root->addWidget(box);

    auto sync = [=] {
        const bool admin = role->currentData().toBool();
        perms->setEnabled(!admin);
        chBox->setEnabled(!admin);
        tree->setEnabled(!admin && rSome->isChecked());
        cExport->setEnabled(!admin && cPlay->isChecked());
    };
    QObject::connect(role, &QComboBox::currentIndexChanged, &dlg, sync);
    QObject::connect(rSome, &QRadioButton::toggled, &dlg, sync);
    QObject::connect(cPlay, &QCheckBox::toggled, &dlg, sync);
    sync();

    const QString oldName = u.name;
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        const QString n = name->text().trimmed();
        if (n.isEmpty()) return err->setText(QStringLiteral("Nhập tên tài khoản"));
        const int other = users().indexOf(n);
        if (other >= 0 && (isNew || users().list()[other].name.compare(oldName, Qt::CaseInsensitive) != 0))
            return err->setText(QStringLiteral("Tài khoản \"%1\" đã có").arg(n));
        if (isNew && pw->text().isEmpty()) return err->setText(QStringLiteral("Nhập mật khẩu"));
        if (!pw->text().isEmpty() && pw->text().size() < 4) return err->setText(QStringLiteral("Mật khẩu tối thiểu 4 ký tự"));
        if (pw->text() != pw2->text()) return err->setText(QStringLiteral("Hai lần nhập mật khẩu không khớp"));
        const bool admin = role->currentData().toBool();
        if (!isNew && u.admin && !admin) {
            if (oldName.compare(users().current().name, Qt::CaseInsensitive) == 0)
                return err->setText(QStringLiteral("Không thể tự bỏ quyền quản trị của chính mình"));
            if (users().adminCount() <= 1) return err->setText(QStringLiteral("Phải còn ít nhất 1 quản trị viên"));
        }
        dlg.accept();
    });
    if (dlg.exec() != QDialog::Accepted) return false;

    u.name = name->text().trimmed();
    u.admin = role->currentData().toBool();
    u.live = cLive->isChecked();
    u.playback = cPlay->isChecked();
    u.exportClip = cExport->isChecked();
    u.audio = cAudio->isChecked();
    u.light = cLight->isChecked();
    u.adjust = cAdjust->isChecked();
    u.count = cCount->isChecked();
    u.allChannels = rAll->isChecked();
    u.channels.clear();
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        for (int j = 0; j < tree->topLevelItem(i)->childCount(); ++j) {
            QTreeWidgetItem *it = tree->topLevelItem(i)->child(j);
            if (it->checkState(0) == Qt::Checked) u.channels << it->data(0, Qt::UserRole).toString();
        }
    *newPassword = pw->text();
    return true;
}

// ======================= SettingsView =======================

SettingsView::SettingsView(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 6);
    auto *tabs = new QTabWidget(this);
    tabs->tabBar()->setUsesScrollButtons(false);
    tabs->tabBar()->setDrawBase(false);
    tabs->addTab(buildDevicesPage(), QStringLiteral("Thiết bị và kênh"));
    tabs->addTab(buildUsersPage(), QStringLiteral("Tài khoản và phân quyền"));
    root->addWidget(tabs);
    reload();
}

void SettingsView::reload()
{
    reloadDevices();
    reloadUsers();
}

bool SettingsView::saveConfig()
{
    QString e;
    if (cfg().save(&e)) return true;
    QMessageBox::warning(this, "59-eye", QStringLiteral("Không ghi được config.json:\n%1").arg(e));
    return false;
}

QWidget *SettingsView::buildDevicesPage()
{
    auto *page = new QWidget(this);
    auto *h = new QHBoxLayout(page);
    h->setSpacing(12);

    auto *left = new QVBoxLayout;
    auto *lt = new QLabel(QStringLiteral("THIẾT BỊ"), page);
    lt->setStyleSheet("color:#a8959a;font-weight:600;");
    left->addWidget(lt);
    m_devList = new QListWidget(page);
    m_devList->setIconSize(QSize(18, 18));
    left->addWidget(m_devList, 1);
    auto *b1 = new QHBoxLayout;
    auto *addNvr = new QPushButton(Icons::recorder(), QStringLiteral(" Thêm đầu ghi"), page);
    auto *addCam = new QPushButton(Icons::camera(), QStringLiteral(" Thêm camera RTSP"), page);
    b1->addWidget(addNvr);
    b1->addWidget(addCam);
    left->addLayout(b1);
    auto *b2 = new QHBoxLayout;
    m_editDev = new QPushButton(QStringLiteral("Sửa"), page);
    m_delDev = new QPushButton(QStringLiteral("Xoá"), page);
    b2->addWidget(m_editDev);
    b2->addWidget(m_delDev);
    b2->addStretch();
    left->addLayout(b2);
    left->addWidget(hintLabel(QStringLiteral("Chỉ thay đổi danh sách trong 59-eye (config.json), không đổi cấu hình đầu ghi."), page));
    h->addLayout(left, 2);

    auto *right = new QVBoxLayout;
    m_chTitle = new QLabel(QStringLiteral("KÊNH"), page);
    m_chTitle->setStyleSheet("color:#a8959a;font-weight:600;");
    right->addWidget(m_chTitle);
    m_chList = new QListWidget(page);
    m_chList->setIconSize(QSize(16, 16));
    right->addWidget(m_chList, 1);
    auto *b3 = new QHBoxLayout;
    m_allOn = new QPushButton(QStringLiteral("Bật tất cả"), page);
    m_allOff = new QPushButton(QStringLiteral("Tắt tất cả"), page);
    b3->addWidget(m_allOn);
    b3->addWidget(m_allOff);
    b3->addStretch();
    right->addLayout(b3);
    right->addWidget(hintLabel(QStringLiteral("Bỏ chọn để tắt kênh: kênh bị ẩn khỏi danh sách, không mở được ở Xem trực tiếp / Xem lại "
                                              "với mọi tài khoản. Đầu ghi vẫn ghi hình bình thường."), page));
    h->addLayout(right, 3);

    connect(addNvr, &QPushButton::clicked, this, [this] { addDevice(false); });
    connect(addCam, &QPushButton::clicked, this, [this] { addDevice(true); });
    connect(m_editDev, &QPushButton::clicked, this, &SettingsView::editDevice);
    connect(m_delDev, &QPushButton::clicked, this, &SettingsView::deleteDevice);
    connect(m_devList, &QListWidget::itemDoubleClicked, this, &SettingsView::editDevice);
    connect(m_devList, &QListWidget::currentRowChanged, this, &SettingsView::reloadChannels);
    connect(m_allOn, &QPushButton::clicked, this, [this] { setAllChannels(true); });
    connect(m_allOff, &QPushButton::clicked, this, [this] { setAllChannels(false); });
    connect(m_chList, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
        const int d = m_devList->currentRow();
        if (m_filling || d < 0 || d >= cfg().nvrs.size()) return;
        const int ch = it->data(Qt::UserRole).toInt();
        if (it->checkState() == Qt::Checked) cfg().nvrs[d].hidden.remove(ch);
        else cfg().nvrs[d].hidden.insert(ch);
        it->setForeground(it->checkState() == Qt::Checked ? Theme::text : Theme::textDim);
        if (saveConfig()) emit channelsChanged();
        m_filling = true;
        const NvrConfig &n = cfg().nvrs[d];
        m_devList->item(d)->setText(QStringLiteral("%1\n%2").arg(n.name, n.isDahua()
            ? QStringLiteral("%1 · %2/%3 kênh bật").arg(n.host).arg(n.channels - n.hidden.size()).arg(n.channels)
            : QStringLiteral("Camera RTSP")));
        m_filling = false;
    });
    return page;
}

void SettingsView::reloadDevices()
{
    const int keep = m_devList->currentRow();
    m_filling = true;
    m_devList->clear();
    for (const NvrConfig &n : cfg().nvrs) {
        auto *it = new QListWidgetItem(n.isDahua() ? Icons::recorder() : Icons::camera(),
            QStringLiteral("%1\n%2").arg(n.name, n.isDahua()
                ? QStringLiteral("%1 · %2/%3 kênh bật").arg(n.host).arg(n.channels - n.hidden.size()).arg(n.channels)
                : (n.hidden.contains(1) ? QStringLiteral("Camera RTSP · đang tắt") : QStringLiteral("Camera RTSP"))));
        m_devList->addItem(it);
    }
    m_filling = false;
    if (m_devList->count() > 0) m_devList->setCurrentRow(qBound(0, keep, m_devList->count() - 1));
    reloadChannels();
}

void SettingsView::reloadChannels()
{
    const int d = m_devList->currentRow();
    const bool has = d >= 0 && d < cfg().nvrs.size();
    m_editDev->setEnabled(has);
    m_delDev->setEnabled(has);
    m_allOn->setEnabled(has);
    m_allOff->setEnabled(has);
    m_filling = true;
    m_chList->clear();
    if (has) {
        const NvrConfig &n = cfg().nvrs[d];
        m_chTitle->setText(QStringLiteral("KÊNH CỦA %1").arg(n.name.toUpper()));
        for (int ch = 1; ch <= n.channels; ++ch) {
            const ChannelRef r{d, ch};
            auto *it = new QListWidgetItem(Icons::camera(),
                QStringLiteral("%1  %2").arg(ch, 2, 10, QChar('0')).arg(cfg().shortTitle(r)), m_chList);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setData(Qt::UserRole, ch);
            const bool on = !n.hidden.contains(ch);
            it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            it->setForeground(on ? Theme::text : Theme::textDim);
        }
    } else {
        m_chTitle->setText(QStringLiteral("KÊNH"));
    }
    m_filling = false;
}

void SettingsView::setAllChannels(bool on)
{
    const int d = m_devList->currentRow();
    if (d < 0 || d >= cfg().nvrs.size()) return;
    NvrConfig &n = cfg().nvrs[d];
    n.hidden.clear();
    if (!on)
        for (int ch = 1; ch <= n.channels; ++ch) n.hidden.insert(ch);
    if (saveConfig()) emit channelsChanged();
    reloadDevices();
}

void SettingsView::addDevice(bool rtsp)
{
    NvrConfig n;
    n.type = rtsp ? "rtsp" : "dahua";
    n.channels = rtsp ? 1 : 16;
    if (!deviceDialog(this, n, true)) return;
    n.id = AppConfig::newId();
    cfg().nvrs.push_back(n);
    if (!saveConfig()) {
        cfg().nvrs.pop_back();
        return;
    }
    reloadDevices();
    m_devList->setCurrentRow(m_devList->count() - 1);
    emit devicesChanged();
}

void SettingsView::editDevice()
{
    const int d = m_devList->currentRow();
    if (d < 0 || d >= cfg().nvrs.size()) return;
    NvrConfig n = cfg().nvrs[d];
    if (!deviceDialog(this, n, false)) return;
    const NvrConfig old = cfg().nvrs[d];
    cfg().nvrs[d] = n;
    if (!saveConfig()) {
        cfg().nvrs[d] = old;
        return;
    }
    reloadDevices();
    emit devicesChanged();
}

void SettingsView::deleteDevice()
{
    const int d = m_devList->currentRow();
    if (d < 0 || d >= cfg().nvrs.size()) return;
    const NvrConfig old = cfg().nvrs[d];
    if (QMessageBox::question(this, QStringLiteral("Xoá thiết bị"),
            QStringLiteral("Xoá \"%1\" khỏi 59-eye?\n\nChỉ xoá khỏi danh sách của phần mềm, đầu ghi/camera vẫn hoạt động và ghi hình bình thường. "
                           "Tài khoản nhân viên sẽ mất quyền xem các kênh của thiết bị này.").arg(old.name)) != QMessageBox::Yes)
        return;
    cfg().nvrs.removeAt(d);
    if (!saveConfig()) {
        cfg().nvrs.insert(d, old);
        return;
    }
    // bo cac kenh cua thiet bi da xoa khoi danh sach quyen
    const QString prefix = old.id + "/";
    for (UserAccount &u : users().list())
        u.channels.erase(std::remove_if(u.channels.begin(), u.channels.end(),
                                        [&](const QString &k) { return k.startsWith(prefix); }), u.channels.end());
    users().save();
    reloadDevices();
    reloadUsers();
    emit devicesChanged();
}

// ---------------- Tai khoan ----------------

QWidget *SettingsView::buildUsersPage()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    m_userTable = new QTableWidget(0, 4, page);
    m_userTable->setHorizontalHeaderLabels({QStringLiteral("Tài khoản"), QStringLiteral("Vai trò"),
                                            QStringLiteral("Quyền"), QStringLiteral("Kênh")});
    m_userTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_userTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_userTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_userTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_userTable->verticalHeader()->hide();
    m_userTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_userTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_userTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->addWidget(m_userTable, 1);
    auto *b = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("Thêm tài khoản"), page);
    m_editUser = new QPushButton(QStringLiteral("Sửa"), page);
    m_delUser = new QPushButton(QStringLiteral("Xoá"), page);
    b->addWidget(add);
    b->addWidget(m_editUser);
    b->addWidget(m_delUser);
    b->addStretch();
    v->addLayout(b);
    v->addWidget(hintLabel(QStringLiteral("Quản trị: toàn quyền, vào được tab Cài đặt. Nhân viên: chỉ các quyền và kênh được chọn. "
                                          "Tài khoản lưu trong users.json trong thư mục dữ liệu (mật khẩu đã băm)."), page));

    connect(add, &QPushButton::clicked, this, [this] { editUser(-1); });
    connect(m_editUser, &QPushButton::clicked, this, [this] { editUser(m_userTable->currentRow()); });
    connect(m_userTable, &QTableWidget::cellDoubleClicked, this, [this](int r, int) { editUser(r); });
    connect(m_delUser, &QPushButton::clicked, this, &SettingsView::deleteUser);
    connect(m_userTable, &QTableWidget::currentCellChanged, this, [this](int r) {
        m_editUser->setEnabled(r >= 0);
        m_delUser->setEnabled(r >= 0);
    });
    return page;
}

void SettingsView::reloadUsers()
{
    const int keep = m_userTable->currentRow();
    const auto &list = users().list();
    m_userTable->setRowCount(list.size());
    for (int i = 0; i < list.size(); ++i) {
        const UserAccount &u = list[i];
        QStringList p;
        if (u.admin) p << QStringLiteral("Toàn quyền");
        else {
            if (u.live) p << QStringLiteral("Trực tiếp");
            if (u.playback) p << QStringLiteral("Xem lại");
            if (u.playback && u.exportClip) p << QStringLiteral("Trích MP4");
            if (u.audio) p << QStringLiteral("Âm thanh");
            if (u.light) p << QStringLiteral("Đèn");
            if (u.adjust) p << QStringLiteral("Chỉnh màu");
            if (u.live && u.count) p << QStringLiteral("Đếm người");
        }
        QString name = u.name;
        if (u.name.compare(users().current().name, Qt::CaseInsensitive) == 0) name += QStringLiteral("  (đang dùng)");
        m_userTable->setItem(i, 0, new QTableWidgetItem(name));
        m_userTable->setItem(i, 1, new QTableWidgetItem(u.admin ? QStringLiteral("Quản trị") : QStringLiteral("Nhân viên")));
        m_userTable->setItem(i, 2, new QTableWidgetItem(p.isEmpty() ? QStringLiteral("(không có)") : p.join(", ")));
        m_userTable->setItem(i, 3, new QTableWidgetItem(u.admin || u.allChannels ? QStringLiteral("Tất cả")
                                                                                 : QStringLiteral("%1 kênh").arg(u.channels.size())));
    }
    m_userTable->setCurrentCell(qMin(keep, int(list.size()) - 1), 0);
    m_editUser->setEnabled(m_userTable->currentRow() >= 0);
    m_delUser->setEnabled(m_userTable->currentRow() >= 0);
}

void SettingsView::editUser(int index)
{
    const bool isNew = index < 0;
    if (!isNew && index >= users().list().size()) return;
    UserAccount u = isNew ? UserAccount() : users().list()[index];
    QString pw;
    if (!userDialog(this, u, isNew, &pw)) return;
    if (!pw.isEmpty()) Users::setPassword(u, pw);
    const QVector<UserAccount> backup = users().list();
    const bool editingSelf = !isNew && backup[index].name.compare(users().current().name, Qt::CaseInsensitive) == 0;
    if (isNew) users().list().push_back(u);
    else users().list()[index] = u;
    QString e;
    if (!users().save(&e)) {
        users().list() = backup;
        QMessageBox::warning(this, "59-eye", QStringLiteral("Không ghi được users.json:\n%1").arg(e));
        return;
    }
    if (editingSelf) {
        // doi ten chinh minh: cap nhat phien hien tai
        const_cast<UserAccount &>(users().current()).name = u.name;
        users().refreshCurrent();
    }
    reloadUsers();
    emit usersChanged();
}

void SettingsView::deleteUser()
{
    const int r = m_userTable->currentRow();
    if (r < 0 || r >= users().list().size()) return;
    const UserAccount u = users().list()[r];
    if (u.name.compare(users().current().name, Qt::CaseInsensitive) == 0) {
        QMessageBox::information(this, "59-eye", QStringLiteral("Không thể xoá tài khoản đang đăng nhập."));
        return;
    }
    if (u.admin && users().adminCount() <= 1) {
        QMessageBox::information(this, "59-eye", QStringLiteral("Phải còn ít nhất 1 quản trị viên."));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("Xoá tài khoản"), QStringLiteral("Xoá tài khoản \"%1\"?").arg(u.name))
        != QMessageBox::Yes)
        return;
    users().list().removeAt(r);
    QString e;
    if (!users().save(&e)) {
        users().list().insert(r, u);
        QMessageBox::warning(this, "59-eye", QStringLiteral("Không ghi được users.json:\n%1").arg(e));
        return;
    }
    reloadUsers();
    emit usersChanged();
}
