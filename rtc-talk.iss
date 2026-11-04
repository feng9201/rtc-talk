; rtc-talk-win x64 installer. Runtime payload is prepared by scripts/package.ps1.
#ifndef RuntimeDir
  #define RuntimeDir SourcePath + "\build\windows-x64\package"
#endif
#define MyAppName "rtc-talk-win"
#define MyAppExeName "rtc-talk.exe"
#if !FileExists(RuntimeDir + "\" + MyAppExeName)
  #error Run build.bat -p or scripts/package.ps1 before compiling this installer.
#endif
#define MyAppVersion GetFileVersion(RuntimeDir + "\" + MyAppExeName)

[Setup]
AppId={{53dc91bb-5dac-434f-b164-1a3b5ed95cce}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher=hxf
VersionInfoVersion={#MyAppVersion}
VersionInfoTextVersion={#MyAppVersion}
DefaultDirName={param:InstallPath|{autopf}\{#MyAppName}}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19045
SourceDir={#RuntimeDir}
OutputDir={#SourcePath}\dist
OutputBaseFilename=rtc-talk-win_Setup_{#MyAppVersion}_x64
SetupIconFile={#SourcePath}\resources\icon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupMutex=rtc-talk-win-setup-53dc91bb-5dac-434f-b164-1a3b5ed95cce
CloseApplications=yes
CloseApplicationsFilter=rtc-talk.exe
RestartApplications=no

[Languages]
Name: "ChineseSimple"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "*"; Excludes: "*.pdb,*.dmp,logs\*,crash_dumps\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent runasoriginaluser

; No autorun registration. Inno removes installed files on uninstall; user logs,
; dumps and credentials are preserved. Never recursively delete a chosen app dir.
