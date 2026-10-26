; MyChat (qtClient) installer script for Inno Setup 6.3+
;
; Normally invoked by packaging\windows\package.ps1, which stages the app into
; dist\MyChat-<version>\ and then runs:
;
;   ISCC.exe /DMyAppVersion=0.1.0 packaging\windows\MyChat.iss
;
; Manual run works too, as long as the stage directory exists.

#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif

#define MyAppName "MyChat"
#define MyAppPublisher "MyChat"
#define MyAppExeName "qtClient.exe"

[Setup]
; A stable AppId keeps upgrades in the same uninstall entry. Never change it.
AppId={{3E6C1D9A-7F42-4B58-A1C3-9D5E2F8B6A70}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\..\dist
OutputBaseFilename=MyChat-Setup-{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; Per-user install by default: %LOCALAPPDATA%\Programs\MyChat needs no admin,
; and it stays writable -- the chat database lives in <app>\data\chat_<uid>.db.
; Users can still pick "install for all users" from the dialog (then admin).
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
; The Chinese Simplified translation is not bundled with Inno Setup 6.
; To use it, download ChineseSimplified.isl from jrsoftware.org/files/istrans/
; into Inno Setup's Languages folder and add:
;   Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\..\dist\MyChat-{#MyAppVersion}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

; No [UninstallDelete]: <app>\data holds the local chat history (SQLite) and
; must survive an uninstall/reinstall. Inno already removes the files it put
; there; user data stays behind on purpose.
