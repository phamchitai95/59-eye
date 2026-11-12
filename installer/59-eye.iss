; Bo cai 59-eye (Inno Setup 6). Build: tools\package.ps1
; Cai vao C:\Program Files\59-eye, du lieu (config.json, users.json...) o C:\ProgramData\59-eye.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define AppName "59-eye"
#define AppExe "59-eye.exe"

[Setup]
AppId={{6E1F59A2-3C4B-4D59-9E7E-590000000E59}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Trung tam 59
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\assets\59-eye.ico
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\dist
OutputBaseFilename=59-eye-setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
MinVersion=10.0

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
en.DesktopIcon=Tao bieu tuong ngoai man hinh (Desktop)
en.LaunchApp=Mo 59-eye

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIcon}"; Flags: checkedonce

[Dirs]
; Moi tai khoan Windows tren may deu doc/ghi duoc cau hinh chung; giu lai khi go cai dat
Name: "{commonappdata}\{#AppName}"; Permissions: users-modify; Flags: uninsneveruninstall

[Files]
Source: "..\dist\59-eye\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; xoa plugin cu de khong lan ban GStreamer khac phien ban
Type: filesandordirs; Name: "{app}\lib\gstreamer-1.0"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; Chay ca khi cap nhat im lang (OTA); mo bang tai khoan nguoi dung, khong phai quyen admin
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall runasoriginaluser
