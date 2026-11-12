#pragma once
#include <QString>

class QWidget;

namespace AppInfo {
QString displayName();      // "59-eye"
QString version();          // tu CMake (project VERSION)
// Thu muc du lieu ghi duoc (config.json, users.json, image.ini, ui.ini, log):
//  - ban dev / portable: canh file chay neu co config.json hoac portable.txt o do
//  - ban cai dat: %ProgramData%\59-eye (Windows), ~/.config/59-eye (Linux)
QString dataDir();
QString dataFile(const QString &name);
void showAbout(QWidget *parent);
}
