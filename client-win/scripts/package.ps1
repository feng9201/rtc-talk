#Requires -Version 5.1
<#
  RTC Talk Windows 客户端打包（对齐 D:\work\device_tool\build.bat 的做法）：
    Release 构建 -> windeployqt 准备运行环境 -> ISCC 编译安装包 -> 附带便携 zip

  用法：
    pwsh -File scripts\package.ps1                  # 构建 + 出安装包 + 出 zip
    pwsh -File scripts\package.ps1 -NoBuild         # 复用已有 Release 产物
    pwsh -File scripts\package.ps1 -NoZip           # 只出安装包
    pwsh -File scripts\package.ps1 -NoSetup         # 只出便携 zip
    pwsh -File scripts\package.ps1 -Qt D:\Qt\5.15.2\msvc2019_64 -Iscc "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
#>
[CmdletBinding()]
param(
  [string]$Qt = "C:\Qt\5.15.2\msvc2019_64",
  [string]$Iscc = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
  [switch]$NoBuild,
  [switch]$NoZip,
  [switch]$NoSetup
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root "build\windows-x64\bin\Release"
$stage = Join-Path $root "build\windows-x64\package"
$dist = Join-Path $root "dist"

function Copy-Crt([string]$target) {
  # talk-qt.exe 走 /MD，目标机器没有 VC++ 运行时会启不来；带上 CRT。
  $crt = @("msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "vcruntime140.dll", "vcruntime140_1.dll", "concrt140.dll")
  $redistDirs = @()
  if ($env:VCToolsRedistDir) { $redistDirs += (Join-Path $env:VCToolsRedistDir "x64\Microsoft.VC143.CRT") }
  $redistDirs += @(Get-ChildItem "${env:ProgramFiles}\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT" -Directory -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | ForEach-Object { $_.FullName })
  $source = $redistDirs | Where-Object { Test-Path (Join-Path $_ "msvcp140.dll") } | Select-Object -First 1
  foreach ($file in $crt) {
    if (Test-Path (Join-Path $target $file)) { continue }
    $candidates = @()
    if ($source) { $candidates += (Join-Path $source $file) }
    $candidates += (Join-Path $env:WINDIR "System32\$file")
    $found = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($found) { Copy-Item $found $target }
  }
  if (-not (Test-Path (Join-Path $target "msvcp140.dll"))) {
    Write-Warning "未找到 VC 运行时，目标机器需安装 VC++ 2015-2022 x64 Redistributable"
  }
}

if (-not $NoBuild) {
  Push-Location $root
  try {
    cmake --build --preset windows-x64-release
    if ($LASTEXITCODE -ne 0) { throw "Release 构建失败" }
  } finally { Pop-Location }
}

foreach ($file in "talk-qt.exe", "talk_core.dll", "talk-cli.exe", "log-cpp.dll", "fmt.dll") {
  if (-not (Test-Path (Join-Path $bin $file))) { throw "未找到 $file，请先构建 Release" }
}
$windeployqt = Join-Path $Qt "bin\windeployqt.exe"
if (-not (Test-Path $windeployqt)) { throw "未找到 windeployqt：$windeployqt（用 -Qt 指定 Qt 目录）" }

# 1. 准备运行目录（不放测试程序与 pdb）
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage, $dist | Out-Null
foreach ($file in "talk-qt.exe", "talk_core.dll", "talk-cli.exe", "log-cpp.dll", "fmt.dll") {
  Copy-Item (Join-Path $bin $file) $stage
}
& $windeployqt --release --no-translations --no-opengl-sw --compiler-runtime (Join-Path $stage "talk-qt.exe") | Out-Host
if ($LASTEXITCODE -ne 0) { throw "windeployqt 失败" }
Copy-Crt $stage
foreach ($file in "Qt5Core.dll", "Qt5Widgets.dll", "Qt5Network.dll", "platforms\qwindows.dll", "talk_core.dll", "log-cpp.dll", "fmt.dll") {
  if (-not (Test-Path (Join-Path $stage $file))) { throw "打包缺少 $file" }
}

$readme = @"
RTC Talk Windows 客户端

1. 双击 talk-qt.exe 启动（安装版请用开始菜单/桌面快捷方式）。
2. 默认连接测试服务 ws://47.117.159.193:8080/talk/v1/ws，房间 rk-client-test。
3. 勾选“开发测试：从 /dev/token 获取凭证”，点“连接”→“加入房间”→“开麦”。
   （入房后默认静音，不开麦对方听不到）
4. 两台电脑对测时：房间 ID 必须一致，用户标识必须不同，否则报 409 already in room。
5. 屏幕共享：仅 SFU 模式可用，点“共享屏幕”把本机桌面发给对端，
   对方画面显示在窗口左侧（显示“等待远端视频”表示还没收到帧）。
   P2P 模式（两人直连）暂不支持视频，按钮会置灰；停止共享后对方停在最后一帧。
   两人房间若“模式”选的是 auto，服务端会切到 P2P，此时看不到对方画面；
   测视频请把两台机器的“模式”都改成 SFU 再重新加入房间。
6. 音视频设备不对时，用“采集 / 播放”下拉框切换；切换后重新“连接”生效。
7. 排障：
   - 日志文件默认在 %LOCALAPPDATA%\rtc-talk\logs\talk-client.log，
     10MB 滚动、保留 5 份；界面启动时会在日志区显示实际路径。
   - 日志区会打印 信令状态 / 媒体状态 / 错误码。
   - talk-cli.exe --list-devices        列出本机音频设备
   - talk-cli.exe --help                命令行联调用法
   - talk-cli.exe ... --log-file D:\talk.log --log-level debug
   - 环境变量 TALK_LOG_FILE / TALK_LOG_LEVEL 可覆盖日志路径与级别。
8. talk-cli.exe 为 /MT 独立可执行，需与 log-cpp.dll、fmt.dll 同目录；
   可在无 Qt 环境的机器上单独用于抓日志。

正式环境请去掉“开发测试”勾选，改填业务后端签发的 Token。
"@
Set-Content -Path (Join-Path $stage "使用说明.txt") -Value $readme -Encoding utf8

# 2. 编译安装包（版本号取自 talk-qt.exe 的 VERSIONINFO）
$setup = $null
if ($NoSetup) {
  Write-Host "跳过安装包（-NoSetup）"
} elseif (Test-Path $Iscc) {
  & $Iscc (Join-Path $root "installer\talk.iss") | Out-Host
  if ($LASTEXITCODE -ne 0) { throw "ISCC 编译失败" }
  $setup = Get-ChildItem (Join-Path $dist "rtc-talk_Setup_*.exe") | Sort-Object LastWriteTime -Descending | Select-Object -First 1
} else {
  Write-Warning "未找到 ISCC：$Iscc，跳过安装包（用 -Iscc 指定路径）"
}

# 3. 便携 zip（免安装，用于快速拷到另一台机器）
$zip = $null
if (-not $NoZip) {
  $zip = Join-Path $dist ("rtc-talk-win-" + (Get-Date -Format "yyyyMMdd-HHmm") + ".zip")
  if (Test-Path $zip) { Remove-Item $zip -Force }
  Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
}

$version = (Get-Item (Join-Path $bin "talk-qt.exe")).VersionInfo.FileVersion
Write-Host "版本：$version"
Write-Host "运行目录：$stage"
if ($setup) { Write-Host ("安装包：{0}（{1} MB）" -f $setup.FullName, [math]::Round($setup.Length / 1MB, 1)) }
if ($zip) { Write-Host ("便携包：{0}（{1} MB）" -f $zip, [math]::Round((Get-Item $zip).Length / 1MB, 1)) }
