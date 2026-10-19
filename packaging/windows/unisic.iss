; Per-user Windows installer for the folder packaging/windows/deploy.sh stages.
; Build it with Inno Setup 6 after deploy.sh, from pwsh or cmd (an MSYS2 shell
; rewrites the /D switches as paths):
;
;   iscc /DAppVersion=<UNISIC_VERSION_STRING> /DBundleDir=<abs path to dist-win>
;        /DOutputDir=<abs dir> packaging\windows\unisic.iss
;
; It installs for the current user only, into %LOCALAPPDATA%\Programs\Unisic,
; and never asks for admin rights, so nothing outside the user's profile is
; touched. The layout is the bundle's (bin\ + share\), so the installed copy and
; the windows-portable artifact are the same files, and the installed copy
; updates itself the way the zip does: a newer release is staged under
; %APPDATA%\Unisic\unisic\updates and the installed unisic.exe starts it, while
; {app} itself is only ever written by this installer.
;
; Every wizard string comes from Inno Setup's own translations, one per
; language Unisic ships, so the installer adds nothing to i18n/.

#ifndef AppVersion
  #error Pass /DAppVersion=<UNISIC_VERSION_STRING>
#endif
#ifndef BundleDir
  #error Pass /DBundleDir=<the deploy.sh output folder, absolute>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; Never change the AppId: it is how an upgrade finds the copy it replaces, and
; the uninstall entry is HKCU\...\Uninstall\{AppId}_is1.
AppId={{E5B31D14-31A5-4639-800D-2E19950EFEC3}
AppName=Unisic
AppVersion={#AppVersion}
AppVerName=Unisic {#AppVersion}
AppPublisher=Unisic
AppPublisherURL=https://unisic.app
AppSupportURL=https://github.com/unisic/unisic/issues
AppUpdatesURL=https://github.com/unisic/unisic/releases
PrivilegesRequired=lowest
; With PrivilegesRequired=lowest {autopf} is %LOCALAPPDATA%\Programs.
DefaultDirName={autopf}\Unisic
DefaultGroupName=Unisic
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Qt 6 needs Windows 10 1809 or later.
MinVersion=10.0.17763
WizardStyle=modern dynamic
ShowLanguageDialog=auto
SetupIconFile=..\..\src\platform\win\unisic.ico
UninstallDisplayIcon={app}\bin\unisic.exe
UninstallDisplayName=Unisic
; A second setup started while one runs (the user and an update at once) waits
; for the first instead of writing over the same files.
SetupMutex=UnisicSetup
; Unisic keeps running in the tray, so an upgrade or an uninstall closes it
; through the Restart Manager instead of failing on files in use. It does not
; start it again: Unisic registers no restart with Windows, and the wizard's
; last page offers to launch it.
CloseApplications=yes
RestartApplications=no
OutputDir={#OutputDir}
; "experimental" while the Windows port is a preview, like the release zip.
OutputBaseFilename=unisic-{#AppVersion}-windows-x64-experimental-setup
Compression=lzma2/max
SolidCompression=yes

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; An upgrade starts from an empty bin\ and share\: a Qt plugin or QML module
; the new version no longer ships must not be left behind for Qt to load.
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\share"

[UninstallDelete]
; The staged self-updates live outside {app}, a few hundred MB per version, and
; are useless once the launcher is gone. Settings and capture history next to
; them stay: they are the user's data, not this installer's.
Type: filesandordirs; Name: "{userappdata}\Unisic\unisic\updates"

[Registry]
; "Start at login" is a value Unisic itself writes under the Run key (named
; after its desktop file name, DesktopShell_win.cpp), plus the one Windows adds
; under StartupApproved when Task Manager toggles it. Install leaves both alone,
; so an upgrade keeps the user's choice; uninstall deletes them, or Windows
; would try to start a deleted exe at every login. ValueType none writes nothing.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "app.unisic.Unisic"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run"; ValueType: none; ValueName: "app.unisic.Unisic"; Flags: uninsdeletevalue

[Files]
Source: "{#BundleDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Unisic"; Filename: "{app}\bin\unisic.exe"
Name: "{autodesktop}\Unisic"; Filename: "{app}\bin\unisic.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\unisic.exe"; Description: "{cm:LaunchProgram,Unisic}"; Flags: nowait postinstall skipifsilent
