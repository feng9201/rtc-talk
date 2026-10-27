# rtc-talk-win

Windows x64 production RTC desktop client.

## Build

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-release --parallel
ctest --test-dir build/windows-x64 -C Release --output-on-failure
```

## Run

```powershell
.\build\windows-x64\bin\Release\rtc-talk.exe
```

Development-only room preview:

```powershell
.\build\windows-x64\bin\Release\rtc-talk.exe --room-preview
```
