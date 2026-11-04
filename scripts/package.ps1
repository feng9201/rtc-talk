#Requires -Version 5.1
<#
  Build a portable ZIP and optionally an Inno Setup installer.

  Usage:
    pwsh -File scripts\package.ps1
    pwsh -File scripts\package.ps1 -NoBuild
    pwsh -File scripts\package.ps1 -Installer -IncrementVersion
    pwsh -File scripts\package.ps1 -Installer -NoBuild
#>
[CmdletBinding()]
param(
  [string]$Qt = "C:\Qt\5.15.2\msvc2019_64",
  [switch]$NoBuild,
  [switch]$Installer,
  [switch]$IncrementVersion,
  [string]$ISCC = $env:ISCC
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root "build\windows-x64\bin\Release"
$stage = Join-Path $root "build\windows-x64\package"
$dist = Join-Path $root "dist"

if ($IncrementVersion -and ($NoBuild -or -not $Installer)) {
  throw "IncrementVersion requires an installer build (without NoBuild)"
}
if ($Installer) {
  if (-not $ISCC) {
    $compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($compiler) { $ISCC = $compiler.Source }
  }
  if (-not $ISCC) {
    $ISCC = @(
      "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
      "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
      Select-Object -First 1
  }
  if (-not $ISCC -or -not (Test-Path -LiteralPath $ISCC -PathType Leaf)) {
    throw "Inno Setup 6 not found. Set ISCC to the full ISCC.exe path."
  }
}
$windeployqt = Join-Path $Qt "bin\windeployqt.exe"
if (-not (Test-Path -LiteralPath $windeployqt -PathType Leaf)) {
  throw "windeployqt not found: $windeployqt"
}

function Copy-CrtRuntime([string]$target) {
  $files = @(
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "concrt140.dll"
  )
  $redistRoots = @()
  if ($env:VCToolsRedistDir) {
    $redistRoots += Join-Path $env:VCToolsRedistDir "x64\Microsoft.VC143.CRT"
  }
  $redistRoots += Get-ChildItem `
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*" `
    -Directory -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    ForEach-Object { Join-Path $_.FullName "x64\Microsoft.VC143.CRT" }

  foreach ($file in $files) {
    $destination = Join-Path $target $file
    if (Test-Path $destination) { continue }
    $source = $redistRoots |
      ForEach-Object { Join-Path $_ $file } |
      Where-Object { Test-Path $_ } |
      Select-Object -First 1
    if (-not $source) {
      $source = Join-Path $env:WINDIR "System32\$file"
    }
    if (Test-Path $source) {
      Copy-Item -LiteralPath $source -Destination $destination
    }
  }
  if (-not (Test-Path (Join-Path $target "msvcp140.dll"))) {
    throw "VC++ x64 runtime is missing from the package"
  }
}

if (-not $NoBuild) {
  $env:QTDIR = $Qt
  if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = "D:\vcpkg" }
  if (-not $env:WEBRTC_SDK_DIR) {
    $env:WEBRTC_SDK_DIR = "D:\google\webrtc_sdk_hevc"
  }
  Push-Location $root
  try {
    if ($IncrementVersion) {
      cmake "-DVERSION_FILE=$root/Version.txt" -P (Join-Path $root "cmake/Version.cmake")
      if ($LASTEXITCODE -ne 0) { throw "Version increment failed" }
    }
    cmake --preset windows-x64
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
    cmake --build --preset windows-x64-release --parallel
    if ($LASTEXITCODE -ne 0) { throw "Release build failed" }
  } finally {
    Pop-Location
  }
}

$productVersion = (Get-Item -LiteralPath (Join-Path $bin "rtc-talk.exe")).VersionInfo.FileVersion
if ($Installer) {
  $expectedVersion = (Get-Content -LiteralPath (Join-Path $root "Version.txt") -Raw).Trim()
  if ($productVersion -ne $expectedVersion) {
    throw "EXE version $productVersion differs from Version.txt $expectedVersion. Rebuild first."
  }
  foreach ($dll in @('rtcwin_engine.dll', 'rtcwin_crash_handler.dll', 'rtcwin_log_adapter.dll')) {
    $dllVersion = (Get-Item -LiteralPath (Join-Path $bin $dll)).VersionInfo.FileVersion
    if ($dllVersion -ne $expectedVersion) {
      throw "$dll version $dllVersion differs from $expectedVersion. Rebuild the complete application."
    }
  }
}

$required = @(
  "rtc-talk.exe",
  "rtcwin_engine.dll",
  "rtcwin_crash_handler.dll",
  "rtcwin_log_adapter.dll",
  "log-cpp.dll",
  "fmt.dll",
  "avutil-59.dll"
)
foreach ($file in $required) {
  if (-not (Test-Path (Join-Path $bin $file))) {
    throw "Missing release artifact: $file"
  }
}

if (Test-Path $stage) {
  $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
  $resolvedBuild = (Resolve-Path -LiteralPath (Join-Path $root "build\windows-x64")).Path
  if (-not $resolvedStage.StartsWith($resolvedBuild, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove unexpected package directory: $resolvedStage"
  }
  Remove-Item -LiteralPath $stage -Recurse -Force
}
$iconSource = Join-Path $root "resources\icon.ico"
if (-not (Test-Path $iconSource)) { throw "Missing icon: $iconSource" }
New-Item -ItemType Directory -Force -Path $stage, $dist | Out-Null
Copy-Item -LiteralPath $iconSource -Destination (Join-Path $stage "icon.ico")
Copy-Item -LiteralPath (Join-Path $root "third_party\breakpad\LICENSE") `
  -Destination (Join-Path $stage "Breakpad-LICENSE.txt")

foreach ($file in $required) {
  Copy-Item -LiteralPath (Join-Path $bin $file) -Destination $stage
}

& $windeployqt --release --no-translations --no-opengl-sw `
  (Join-Path $stage "rtc-talk.exe") | Out-Host
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }
Copy-CrtRuntime $stage

$runtimeChecks = @(
  "Qt5Core.dll",
  "Qt5Widgets.dll",
  "Qt5Network.dll",
  "platforms\qwindows.dll",
  "rtcwin_engine.dll",
  "rtcwin_crash_handler.dll",
  "rtcwin_log_adapter.dll",
  "log-cpp.dll",
  "fmt.dll",
  "avutil-59.dll",
  "msvcp140.dll",
  "vcruntime140.dll",
  "icon.ico"
)
foreach ($file in $runtimeChecks) {
  if (-not (Test-Path (Join-Path $stage $file))) {
    throw "Package is missing runtime file: $file"
  }
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$commit = git -c safe.directory=$root -C $root rev-parse --short HEAD
$worktree = if ([string]::IsNullOrWhiteSpace(
    (git -c safe.directory=$root -C $root status --short))) {
  "clean"
} else {
  "dirty"
}
$versionText = @"
rtc-talk-win
Version: $productVersion
Build: $stamp
Commit: $commit
Worktree: $worktree
WebRTC ABI: 2
"@
Set-Content -LiteralPath (Join-Path $stage "VERSION.txt") `
  -Value $versionText -Encoding UTF8

$usage = @"
rtc-talk-win 使用说明

1. 双击 rtc-talk.exe 启动。
2. 测试服信令地址：ws://47.117.159.193:8080/talk/v1/ws
3. 两台设备必须使用相同房间 ID、不同用户 ID。
4. 首测建议双方都选择：
   - 媒体模式：SFU
   - 音频后端：WASAPI
5. 摄像头只在房间页点击“摄像头”后开始发布。
6. 如果对端看不到画面，使用“诊断启动.bat”启动。
   日志位置：%LOCALAPPDATA%\rtc-talk-win\logs\rtcwin.log
7. 必须同时更新 rtc-talk.exe 和 rtcwin_engine.dll，不能只替换一个文件。
   rtcwin_crash_handler.dll 也必须随程序分发。
   崩溃转储保存在 %LOCALAPPDATA%\rtc-talk-win\crash_dumps\*.dmp，仅本地保存。
8. 当前版本支持摄像机、屏幕共享、P2P/SFU 双路视频，
   以及“共享主画面 + 摄像头 PIP”和底部成员胶片条。
9. M3 已接入 Floor/PTT、主持人授权/静音/踢人/策略修改和实时成员音量。
10. M4/M5/M6 已接入设备切换回退、resume/ICE 恢复、RTT/丢包/码率状态
    和脱敏诊断包导出。
11. 已支持 WSS/TLS 和 Windows Credential Manager；正式证书/认证联调、
    安装签名、升级回滚及干净机发布验收仍待完成。
"@
Set-Content -LiteralPath (Join-Path $stage "使用说明.txt") `
  -Value $usage -Encoding UTF8

$diagnostic = @"
@echo off
set "RTCWIN_TRACE_EVENTS=1"
set "RTCWIN_LOG_PATH=%~dp0logs\rtcwin.log"
if not exist "%~dp0logs" mkdir "%~dp0logs"
start "" "%~dp0rtc-talk.exe"
"@
Set-Content -LiteralPath (Join-Path $stage "诊断启动.bat") `
  -Value $diagnostic -Encoding ASCII

$hashLines = Get-ChildItem -LiteralPath $stage -Recurse -File |
  Where-Object { $_.Name -ne "SHA256SUMS.txt" } |
  ForEach-Object {
    $relative = $_.FullName.Substring($stage.Length + 1).Replace("\", "/")
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $relative"
  }
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllLines(
  (Join-Path $stage "SHA256SUMS.txt"),
  [string[]]$hashLines,
  $utf8NoBom)

$zip = Join-Path $dist "rtc-talk-win-$stamp.zip"
if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
# Use the framework API in both Windows PowerShell 5.1 and PowerShell 7; avoid
# loading an incompatible Archive module inherited through PSModulePath.
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
  $stage, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $false)

Write-Host "Package directory: $stage"
Write-Host ("ZIP: {0} ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))

if ($Installer) {
  & $ISCC /Qp (Join-Path $root "rtc-talk.iss")
  if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed" }
  $setup = Join-Path $dist "rtc-talk-win_Setup_${productVersion}_x64.exe"
  if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) {
    throw "Installer output is missing: $setup"
  }
  Write-Host "Installer: $setup"
}
