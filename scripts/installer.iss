; Inno Setup script for the OpenRung VPN WinUI3 client.
; Consumes an assembled dist\ tree (OpenRung.WinUI.exe + core\openrung-core.exe
; + the self-contained WindowsAppSDK payload) and produces one installer exe.
; AppVersion is injected by CI: iscc /DAppVersion=v0.1.1 scripts\installer.iss

#define AppName "OpenRung VPN"
#ifndef AppVersion
#define AppVersion "0.0.0"
#endif
; iscc resolves Source/Output paths relative to THIS script's directory,
; not the invoking working directory — the repo layout puts the assembled
; tree and the redist one level up.
#define DistDir "..\dist"
#define RootDir ".."

[Setup]
AppId={{7A1E5C36-9B44-4F8D-9A02-51D0E7C3B6A4}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=SakurakaiCat
AppPublisherURL=https://github.com/SakurakaiCat/OpenrungVPN-winui3
DefaultDirName={autopf}\OpenRung
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Output goes next to the assembled dist tree; CI uploads the exe as-is.
OutputDir=installer-out
OutputBaseFilename=OpenRung-Setup-{#AppVersion}-x64
Compression=lzma2/max
SolidCompression=yes
LZMAUseSeparateProcess=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayIcon={app}\OpenRung.WinUI.exe
; The core is killed on uninstall only if the UI is not holding it; the app
; closes gracefully via its own single-instance shutdown path.
CloseApplications=no

[Messages]
; The upstream client is Chinese-first; keep the wizard bilingual where the
; stock language file allows (English fallback).
SetupAppTitle={#AppName} {#AppVersion} 安装向导 / Setup

; The app links the dynamic CRT (MSB8024 forbids static linking for this
; project type), so the installer ships the VC++ 2015-2022 redistributable
; and installs it silently before anything else. CI stages vc_redist.x64.exe
; next to dist\.

[Files]
Source: "{#DistDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#RootDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\OpenRung.WinUI.exe"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\OpenRung.WinUI.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式 / Create a &desktop shortcut"; GroupDescription: "附加任务 / Additional tasks:"

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "正在安装 VC++ 运行库 / Installing VC++ runtime..."; Flags: waituntilterminated
Filename: "{app}\OpenRung.WinUI.exe"; Description: "启动 {#AppName} / Launch {#AppName}"; Flags: nowait postinstall skipifsilent
