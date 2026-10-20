; RTC Talk Windows 客户端安装包（结构与 device_tool\omniStation.iss 一致）
; 打包内容：build\windows-x64\package，由 scripts\package.ps1 用 windeployqt 准备
; 版本号自动读取 talk-qt.exe 的 VERSIONINFO（cmake/version.rc.in 生成）

#define Stage "..\build\windows-x64\package"
#define MyAppVersion GetFileVersion(SourcePath + "\" + Stage + "\talk-qt.exe")
#define APPFAST "rtc-talk"
#define MyAppName "RTC Talk"
#define MyAppPublisher "feng9201"
#define MyAppURL "https://github.com/feng9201/rtc-talk"
#define MyAppExeName "talk-qt.exe"
; 输出安装包文件名（不含扩展），格式：rtc-talk_Setup_0.1.0.0
#define OutputBaseFilename APPFAST + "_Setup_" + MyAppVersion

[Setup]
; 注意: AppId 唯一标识此应用程序，请勿在其他安装程序中复用
AppId={{7B2C4E9A-1D3F-4A6B-9C58-0E2D7F1A4B63}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
VersionInfoVersion={#MyAppVersion}
VersionInfoTextVersion={#MyAppVersion}
;支持识别命令行指定安装路径
DefaultDirName={param:InstallPath|{autopf}\{#MyAppName}}
DisableProgramGroupPage=yes
PrivilegesRequiredOverridesAllowed=commandline
; 客户端按用户安装，测试机无需管理员权限
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename={#OutputBaseFilename}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "ChineseSimple"; MessagesFile: "compiler:ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkablealone

[Files]
; 主程序
Source: "{#Stage}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; 其余文件（Qt 运行时、talk_core.dll、talk-cli.exe、说明），去掉调试符号
Source: "{#Stage}\*"; Excludes: "*.pdb,talk-qt.exe"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Registry]
; 卸载时清理界面写下的偏好（QSettings: HKCU\Software\rtc-talk）
Root: HKCU; Subkey: "Software\rtc-talk"; Flags: uninsdeletekey dontcreatekey

[Code]
function TaskKill(FileName: String): Integer;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('taskkill.exe'), '/f /im ' + '"' + FileName + '"', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode);
  Result := ResultCode;
end;

// 安装前结束正在运行的客户端
function InitializeSetup(): Boolean;
var
  ret: Integer;
begin
  Result := True;
  ret := TaskKill('talk-qt.exe');
  if (ret <> 0) and (ret <> 128) then
  begin
    Result := False;
    MsgBox('talk-qt.exe 关闭失败, code:' + IntToStr(ret), MBInformation, MB_OK);
  end;
end;

// 卸载前结束正在运行的客户端
function InitializeUninstall(): Boolean;
var
  ret: Integer;
begin
  Result := True;
  ret := TaskKill('talk-qt.exe');
  if (ret <> 0) and (ret <> 128) then
  begin
    Result := False;
    MsgBox('talk-qt.exe 关闭失败, code:' + IntToStr(ret), MBInformation, MB_OK);
  end;
end;

// 覆盖安装前清空旧版本文件（仅当目录里确实有本程序时）
procedure CurStepChanged(CurStep: TSetupStep);
var
  AppDir: String;
begin
  if CurStep = ssInstall then
  begin
    AppDir := ExpandConstant('{app}');
    if FileExists(AppDir + '\{#MyAppExeName}') then
      DelTree(AppDir, True, True, True);
  end;
end;

// 卸载后删除残留文件
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usDone then
    DelTree(ExpandConstant('{app}'), True, True, True);
end;
