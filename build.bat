@echo off
setlocal

set "BUILD_CONFIG=Release"
set "BUILD_INSTALLER=0"
if /I "%~1"=="-p" (
  set "BUILD_INSTALLER=1"
  goto config_ok
)
if not "%~1"=="" set "BUILD_CONFIG=%~1"
if not "%~2"=="" goto usage
if /I "%BUILD_CONFIG%"=="Release" goto config_ok
if /I "%BUILD_CONFIG%"=="Debug" goto config_ok
goto usage

:config_ok
if not "%~2"=="" goto usage
if not defined VCPKG_ROOT set "VCPKG_ROOT=D:\vcpkg"
if not defined QTDIR set "QTDIR=C:\Qt\5.15.2\msvc2019_64"
if not defined WEBRTC_SDK_DIR set "WEBRTC_SDK_DIR=D:\google\webrtc_sdk_hevc"
set "BUILD_PRESET=windows-x64-release"
if /I "%BUILD_CONFIG%"=="Debug" set "BUILD_PRESET=windows-x64-debug"

where cmake >nul 2>&1
if errorlevel 1 (
  echo ERROR: cmake was not found in PATH.
  exit /b 1
)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo ERROR: Invalid VCPKG_ROOT: "%VCPKG_ROOT%"
  exit /b 1
)
if not exist "%QTDIR%\lib\cmake\Qt5\Qt5Config.cmake" (
  echo ERROR: Invalid QTDIR: "%QTDIR%"
  exit /b 1
)
if not exist "%WEBRTC_SDK_DIR%\include\api\peer_connection_interface.h" (
  echo ERROR: Invalid WEBRTC_SDK_DIR: "%WEBRTC_SDK_DIR%"
  exit /b 1
)

pushd "%~dp0"
if errorlevel 1 exit /b 1
echo VCPKG_ROOT=%VCPKG_ROOT%
echo QTDIR=%QTDIR%
echo WEBRTC_SDK_DIR=%WEBRTC_SDK_DIR%
echo Build preset: %BUILD_PRESET%

if "%BUILD_INSTALLER%"=="1" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\package.ps1" -Qt "%QTDIR%" -Installer -IncrementVersion
  if errorlevel 1 goto failed
  popd
  exit /b 0
)

cmake --preset windows-x64
if errorlevel 1 goto failed
cmake --build --preset %BUILD_PRESET% --parallel
if errorlevel 1 goto failed

popd
exit /b 0

:failed
set "BUILD_EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %BUILD_EXIT_CODE%

:usage
echo Usage: build.bat [Release^|Debug^|-p]
echo -p: Increment version build component, build Release, create ZIP and Inno Setup installer.
echo Default: Release. Existing VCPKG_ROOT, QTDIR and WEBRTC_SDK_DIR override defaults.
exit /b 2
