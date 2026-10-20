#Requires -Version 5.1
<#
  打包 Windows 客户端为便携目录 + zip（目标机器无需安装 Qt / VS 运行时）。

  用法：
    pwsh -File scripts\package.ps1                 # 构建 Release 并打包
    pwsh -File scripts\package.ps1 -NoBuild        # 用已有 Release 产物打包
    pwsh -File scripts\package.ps1 -Qt D:\Qt\5.15.2\msvc2019_64
#>
[CmdletBinding()]
param(
  [string]$Qt = "C:\Qt\5.15.2\msvc2019_64",
  [switch]$NoBuild
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root "build\windows-x64\bin\Release"

if (-not $NoBuild) {
  Push-Location $root
  try {
    cmake --build --preset windows-x64-release
    if ($LASTEXITCODE -ne 0) { throw "Release 构建失败" }
  } finally { Pop-Location }
}

$exe = Join-Path $bin "talk-qt.exe"
if (-not (Test-Path $exe)) { throw "未找到 $exe，请先构建 Release" }
$windeployqt = Join-Path $Qt "bin\windeployqt.exe"
if (-not (Test-Path $windeployqt)) { throw "未找到 windeployqt：$windeployqt（用 -Qt 指定 Qt 目录）" }

$name = "rtc-talk-win-" + (Get-Date -Format "yyyyMMdd-HHmm")
$dist = Join-Path $root "dist\$name"
New-Item -ItemType Directory -Force -Path $dist | Out-Null

# talk_core.dll 与 talk-cli.exe 都是 /MT，只有 talk-qt.exe 需要 Qt 运行时。
foreach ($file in "talk-qt.exe", "talk_core.dll", "talk-cli.exe") {
  $src = Join-Path $bin $file
  if (Test-Path $src) { Copy-Item $src $dist }
}

& $windeployqt --release --no-translations --no-opengl-sw --compiler-runtime (Join-Path $dist "talk-qt.exe") | Out-Host
if ($LASTEXITCODE -ne 0) { throw "windeployqt 失败" }

foreach ($file in "Qt5Core.dll", "Qt5Widgets.dll", "Qt5Network.dll", "platforms\qwindows.dll", "talk_core.dll") {
  if (-not (Test-Path (Join-Path $dist $file))) { throw "打包缺少 $file" }
}

# talk-qt.exe 走 /MD，目标机器没有 VC++ 运行时会启不来；带上 CRT。
$crt = @("msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "vcruntime140.dll", "vcruntime140_1.dll", "concrt140.dll")
$redistDirs = @()
if ($env:VCToolsRedistDir) { $redistDirs += (Join-Path $env:VCToolsRedistDir "x64\Microsoft.VC143.CRT") }
$redistDirs += @(Get-ChildItem "${env:ProgramFiles}\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT" -Directory -ErrorAction SilentlyContinue |
  Sort-Object FullName -Descending | ForEach-Object { $_.FullName })
$crtSource = $redistDirs | Where-Object { Test-Path (Join-Path $_ "msvcp140.dll") } | Select-Object -First 1
foreach ($file in $crt) {
  if (Test-Path (Join-Path $dist $file)) { continue }
  $candidates = @()
  if ($crtSource) { $candidates += (Join-Path $crtSource $file) }
  $candidates += (Join-Path $env:WINDIR "System32\$file")
  $found = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
  if ($found) { Copy-Item $found $dist }
}
if (-not (Test-Path (Join-Path $dist "msvcp140.dll"))) {
  Write-Warning "未找到 VC 运行时，目标机器需安装 VC++ 2015-2022 x64 Redistributable"
}

$readme = @"
RTC Talk Windows 客户端（便携版 $name）

1. 双击 talk-qt.exe 启动。
2. 默认连接测试服务 ws://47.117.159.193:8080/talk/v1/ws，房间 rk-client-test。
3. 勾选“开发测试：从 /dev/token 获取凭证”，点“连接”→“加入房间”→“开麦”。
   （入房后默认静音，不开麦对方听不到）
4. 两台电脑对测时：房间 ID 必须一致，用户标识必须不同，否则报 409 already in room。
5. 音视频设备不对时，用“采集 / 播放”下拉框切换；切换后重新“连接”生效。
6. 排障：
   - 日志区会打印 信令状态 / 媒体状态 / 错误码。
   - talk-cli.exe --list-devices        列出本机音频设备
   - talk-cli.exe --help                命令行联调用法
7. talk-cli.exe 为 /MT 独立可执行，可在无 Qt 环境的机器上单独用于抓日志。

正式环境请去掉“开发测试”勾选，改填业务后端签发的 Token。
"@
Set-Content -Path (Join-Path $dist "使用说明.txt") -Value $readme -Encoding utf8

$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $dist "*") -DestinationPath $zip
$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "打包完成：$zip（$size MB）"
Write-Host "解压目录：$dist"
