#include "Users.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPasswordDigestor>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QVBoxLayout>

static constexpr int kIterations = 60000;

Users &Users::instance()
{
    static Users u;
    return u;
}

static QByteArray derive(const QString &password, const QByteArray &saltHex)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password.toUtf8(),
                                              QByteArray::fromHex(saltHex), kIterations, 32)
        .toHex();
}

void Users::setPassword(UserAccount &u, const QString &password)
{
    QByteArray salt(16, Qt::Uninitialized);
    for (char &c : salt) c = char(QRandomGenerator::system()->bounded(256));
    u.salt = salt.toHex();
    u.hash = derive(password, u.salt);
}

bool Users::checkPassword(const UserAccount &u, const QString &password)
{
    if (u.salt.isEmpty() || u.hash.isEmpty()) return false;
    const QByteArray h = derive(password, u.salt);
    // so sanh khong phu thuoc thoi gian
    if (h.size() != u.hash.size()) return false;
    unsigned char diff = 0;
    for (int i = 0; i < h.size(); ++i) diff |= uchar(h[i]) ^ uchar(u.hash[i]);
    return diff == 0;
}

bool Users::load(const QString &path)
{
    m_path = path;
    m_list.clear();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue &v : root.value("users").toArray()) {
        const QJsonObject o = v.toObject();
        UserAccount u;
        u.name = o.value("name").toString();
        u.admin = o.value("role").toString() == "admin";
        u.salt = o.value("salt").toString().toLatin1();
        u.hash = o.value("hash").toString().toLatin1();
        const QJsonObject p = o.value("perms").toObject();
        u.live = p.value("live").toBool(true);
        u.playback = p.value("playback").toBool(true);
        u.exportClip = p.value("export").toBool(false);
        u.audio = p.value("audio").toBool(true);
        u.light = p.value("light").toBool(false);
        u.adjust = p.value("adjust").toBool(true);
        const QJsonValue ch = o.value("channels");
        u.allChannels = !ch.isArray();
        for (const QJsonValue &c : ch.toArray()) u.channels << c.toString();
        if (!u.name.isEmpty()) m_list.push_back(u);
    }
    return true;
}

bool Users::save(QString *error)
{
    QJsonArray arr;
    for (const UserAccount &u : m_list) {
        QJsonObject o;
        o["name"] = u.name;
        o["role"] = u.admin ? "admin" : "staff";
        o["salt"] = QString::fromLatin1(u.salt);
        o["hash"] = QString::fromLatin1(u.hash);
        QJsonObject p;
        p["live"] = u.live;
        p["playback"] = u.playback;
        p["export"] = u.exportClip;
        p["audio"] = u.audio;
        p["light"] = u.light;
        p["adjust"] = u.adjust;
        o["perms"] = p;
        if (!u.allChannels) o["channels"] = QJsonArray::fromStringList(u.channels);
        arr.append(o);
    }
    QJsonObject root;
    root["users"] = arr;
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

int Users::indexOf(const QString &name) const
{
    for (int i = 0; i < m_list.size(); ++i)
        if (m_list[i].name.compare(name, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

int Users::adminCount() const
{
    int n = 0;
    for (const UserAccount &u : m_list) n += u.admin;
    return n;
}

bool Users::login(const QString &name, const QString &password)
{
    const int i = indexOf(name);
    if (i < 0 || !checkPassword(m_list[i], password)) return false;
    m_current = m_list[i];
    return true;
}

void Users::refreshCurrent()
{
    const int i = indexOf(m_current.name);
    if (i >= 0) m_current = m_list[i];
}

bool Users::can(Perm p) const
{
    const UserAccount &u = m_current;
    if (u.admin) return true;
    switch (p) {
    case Live: return u.live;
    case Playback: return u.playback;
    case Export: return u.playback && u.exportClip;
    case Audio: return u.audio;
    case Light: return u.light;
    case Adjust: return u.adjust;
    }
    return false;
}

bool Users::canSee(const ChannelRef &r) const
{
    if (!cfg().channelEnabled(r)) return false;
    if (m_current.admin || m_current.allChannels) return true;
    return m_current.channels.contains(cfg().channelKey(r));
}

// ---------------- Dang nhap ----------------

LoginDialog::LoginDialog(QWidget *parent) : QDialog(parent)
{
    const bool firstRun = users().isEmpty();
    setWindowTitle(firstRun ? QStringLiteral("59-eye · Tạo tài khoản quản trị") : QStringLiteral("59-eye · Đăng nhập"));
    setMinimumWidth(360);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(12);

    auto *title = new QLabel(firstRun ? QStringLiteral("Chưa có tài khoản nào") : QStringLiteral("Đăng nhập"), this);
    QFont tf = title->font();
    tf.setPointSize(tf.pointSize() + 4);
    tf.setBold(true);
    title->setFont(tf);
    root->addWidget(title);
    if (firstRun) {
        auto *hint = new QLabel(QStringLiteral("Tạo tài khoản quản trị (admin) đầu tiên. Quản trị viên vào tab "
                                               "Cài đặt để thêm tài khoản nhân viên và phân quyền."), this);
        hint->setWordWrap(true);
        hint->setStyleSheet("color:#a8959a;");
        root->addWidget(hint);
    }

    auto *form = new QFormLayout;
    form->setSpacing(8);
    auto *name = new QLineEdit(this);
    auto *pw = new QLineEdit(this);
    pw->setEchoMode(QLineEdit::Password);
    auto *pw2 = new QLineEdit(this);
    pw2->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Tài khoản"), name);
    form->addRow(QStringLiteral("Mật khẩu"), pw);
    if (firstRun) {
        name->setText("admin");
        form->addRow(QStringLiteral("Nhập lại"), pw2);
    } else {
        pw2->hide();
        if (users().list().size() == 1) name->setText(users().list().first().name);
    }
    root->addLayout(form);

    auto *err = new QLabel(this);
    err->setStyleSheet("color:#f08a84;");
    err->hide();
    root->addWidget(err);

    auto *box = new QDialogButtonBox(this);
    QPushButton *ok = box->addButton(firstRun ? QStringLiteral("Tạo và đăng nhập") : QStringLiteral("Đăng nhập"),
                                     QDialogButtonBox::AcceptRole);
    ok->setDefault(true);
    box->addButton(QStringLiteral("Thoát"), QDialogButtonBox::RejectRole);
    root->addWidget(box);
    (name->text().isEmpty() ? name : pw)->setFocus();

    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, this, [=] {
        auto fail = [err](const QString &m) {
            err->setText(m);
            err->show();
        };
        const QString n = name->text().trimmed();
        if (n.isEmpty()) return fail(QStringLiteral("Nhập tên tài khoản"));
        if (firstRun) {
            if (pw->text().size() < 4) return fail(QStringLiteral("Mật khẩu tối thiểu 4 ký tự"));
            if (pw->text() != pw2->text()) return fail(QStringLiteral("Hai lần nhập mật khẩu không khớp"));
            UserAccount u;
            u.name = n;
            u.admin = true;
            Users::setPassword(u, pw->text());
            users().list().push_back(u);
            QString e;
            if (!users().save(&e)) {
                users().list().clear();
                return fail(QStringLiteral("Không ghi được users.json: %1").arg(e));
            }
        }
        if (!users().login(n, pw->text())) {
            pw->clear();
            pw->setFocus();
            return fail(QStringLiteral("Sai tài khoản hoặc mật khẩu"));
        }
        accept();
    });
}

bool changeOwnPassword(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("Đổi mật khẩu"));
    dlg.setMinimumWidth(340);
    auto *form = new QFormLayout(&dlg);
    auto *oldPw = new QLineEdit(&dlg), *pw = new QLineEdit(&dlg), *pw2 = new QLineEdit(&dlg);
    for (QLineEdit *e : {oldPw, pw, pw2}) e->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Mật khẩu hiện tại"), oldPw);
    form->addRow(QStringLiteral("Mật khẩu mới"), pw);
    form->addRow(QStringLiteral("Nhập lại"), pw2);
    auto *err = new QLabel(&dlg);
    err->setStyleSheet("color:#f08a84;");
    form->addRow(err);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(box);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        const int i = users().indexOf(users().current().name);
        if (i < 0) return;
        UserAccount &u = users().list()[i];
        if (!Users::checkPassword(u, oldPw->text())) return err->setText(QStringLiteral("Mật khẩu hiện tại không đúng"));
        if (pw->text().size() < 4) return err->setText(QStringLiteral("Mật khẩu mới tối thiểu 4 ký tự"));
        if (pw->text() != pw2->text()) return err->setText(QStringLiteral("Hai lần nhập không khớp"));
        Users::setPassword(u, pw->text());
        QString e;
        if (!users().save(&e)) return err->setText(QStringLiteral("Không ghi được: %1").arg(e));
        users().refreshCurrent();
        dlg.accept();
    });
    return dlg.exec() == QDialog::Accepted;
}
