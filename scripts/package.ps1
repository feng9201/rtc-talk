#Requires -Version 5.1
<#
  Build a self-contained rtc-talk-win portable package.

  Usage:
    pwsh -File scripts\package.ps1
    pwsh -File scripts\package.ps1 -NoBuild
#>
[CmdletBinding()]
param(
  [string]$Qt = "C:\Qt\5.15.2\msvc2019_64",
  [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root "build\windows-x64\bin\Release"
$stage = Join-Path $root "build\windows-x64\package"
$dist = Join-Path $root "dist"

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
    cmake --build --preset windows-x64-release
    if ($LASTEXITCODE -ne 0) { throw "Release build failed" }
  } finally {
    Pop-Location
  }
}

$required = @(
  "rtc-talk.exe",
  "rtcwin_engine.dll",
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

$windeployqt = Join-Path $Qt "bin\windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
  throw "windeployqt not found: $windeployqt"
}

if (Test-Path $stage) {
  $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
  $resolvedBuild = (Resolve-Path -LiteralPath (Join-Path $root "build\windows-x64")).Path
  if (-not $resolvedStage.StartsWith($resolvedBuild, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove unexpected package directory: $resolvedStage"
  }
  Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stage, $dist | Out-Null

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
  "rtcwin_log_adapter.dll",
  "log-cpp.dll",
  "fmt.dll",
  "avutil-59.dll",
  "msvcp140.dll",
  "vcruntime140.dll"
)
foreach ($file in $runtimeChecks) {
  if (-not (Test-Path (Join-Path $stage $file))) {
    throw "Package is missing runtime file: $file"
  }
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$versionText = @"
rtc-talk-win
Build: $stamp
Commit: $(git -C $root rev-parse --short HEAD)
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
8. 本版本 S4.1 只完成摄像头 SFU 发布和远端 muxPlayer 显示。
   屏幕共享与 P2P 视频仍待后续实现。
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
Set-Content -LiteralPath (Join-Path $stage "SHA256SUMS.txt") `
  -Value $hashLines -Encoding ASCII

$zip = Join-Path $dist "rtc-talk-win-$stamp.zip"
if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -CompressionLevel Optimal

Write-Host "Package directory: $stage"
Write-Host ("ZIP: {0} ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
