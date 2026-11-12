#include "AppInfo.h"
#include "Theme.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

namespace AppInfo {

QString displayName() { return QStringLiteral("59-eye"); }
QString version() { return QStringLiteral(APP_VERSION); }

QString dataDir()
{
    static QString dir;
    if (!dir.isEmpty()) return dir;
    const QDir exe(QCoreApplication::applicationDirPath());
    if (exe.exists("config.json") || exe.exists("portable.txt")) {
        dir = exe.absolutePath();
        return dir;
    }
#ifdef _WIN32
    QString base = qEnvironmentVariable("ProgramData");
    if (base.isEmpty()) base = "C:/ProgramData";
    dir = QDir(QDir::fromNativeSeparators(base)).filePath("59-eye");
#else
    dir = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath("59-eye");
#endif
    QDir().mkpath(dir);
    return dir;
}

QString dataFile(const QString &name)
{
    return QDir(dataDir()).filePath(name);
}

void showAbout(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("Giới thiệu"));
    auto *h = new QHBoxLayout(&dlg);
    h->setContentsMargins(20, 20, 20, 16);
    h->setSpacing(18);
    auto *logo = new QLabel(&dlg);
    logo->setPixmap(Icons::app().pixmap(96, 96));
    logo->setAlignment(Qt::AlignTop);
    h->addWidget(logo);
    auto *v = new QVBoxLayout;
    auto *t = new QLabel(QStringLiteral("<span style='font-size:18pt;font-weight:700'>%1</span><br>"
                                        "<span style='color:#a8959a'>Phần mềm xem camera · Trung tâm 59</span>")
                             .arg(displayName()), &dlg);
    v->addWidget(t);
    v->addWidget(new QLabel(QStringLiteral("Phiên bản %1").arg(version()), &dlg));
    auto *d = new QLabel(QStringLiteral("Dữ liệu: %1").arg(QDir::toNativeSeparators(dataDir())), &dlg);
    d->setStyleSheet("color:#a8959a;");
    d->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(d);
    v->addStretch();
    auto *ok = new QPushButton(QStringLiteral("Đóng"), &dlg);
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    v->addWidget(ok, 0, Qt::AlignRight);
    h->addLayout(v);
    dlg.exec();
}

}
