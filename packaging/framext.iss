; FrameXT Windows 安装包（M6.1，Inno Setup 6）
; 产物：dist/FrameXT-<版本>-setup.exe
; 前置：scripts\package-win.bat 完成部署目录 build\deploy 的装配。
; AppId 为本项目专用新 GUID（勿复用原版应用，避免升级冲突）。
; 注：本文件 UTF-8 with BOM（中文向导文本必需）。

#define MyAppName "FrameXT"
#define MyAppVersion "2.0.1"
#define MyAppPublisher "FrameXT"
#define MyAppExeName "framext.exe"
#define MyAppHome "https://framext.com"

[Setup]
AppId={{BE56665D-14F9-4B4F-A1C6-CB19107D29A4}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppHome}
AppSupportURL={#MyAppHome}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; 开源声明（GPLv3+）：许可页展示仓库 LICENSE
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename=FrameXT-{#MyAppVersion}-setup
SetupIconFile=..\resources\icons\logo.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
; 管理员安装到 Program Files（R4 教训：应用数据在 %APPDATA%，普通用户
; 运行不受影响，从设计上规避原版「无管理员权限不能用」的问题）
PrivilegesRequired=admin
ChangesAssociations=no

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式(&D)"; GroupDescription: "附加任务："

[Files]
; 应用与运行时 + licenses（windeployqt + vcpkg DLL，package-win.bat 装配；
; 许可全文已并入部署树——安装包与 portable zip 共用同一棵树，关于框承诺：
; GPLv3 / 思源黑体 OFL / Aileron CC0 随包）
Source: "..\build\deploy\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{#MyAppName} 卸载"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "运行 {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 防御：日志等运行时残留（用户数据在 %APPDATA%\FrameXT，卸载保留）
Type: files; Name: "{app}\*.log"
