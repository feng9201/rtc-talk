# talk-desktop production UI

## Purpose

`talk-desktop` is the production Qt Widgets desktop application for Windows.
It is separate from the legacy `talk-qt` diagnostic UI and the visual-only
`qt-ui-prototype` project.

## P1/P2 boundary

The current P1/P2 deliverable is an application and state-layer skeleton:

- production executable target and CMake structure
- frameless window and window controls
- production prejoin page shell
- production room page shell
- shared theme and UI widgets
- domain state types and page state setters
- typed core events and event mapping
- core command correlation and timeout handling
- `TalkCoreClient` C ABI lifetime and thread bridge
- `SessionStore` as the single room/session state owner
- screenshot and smoke-test command line modes

The production pages are not yet connected to `SessionStore`, and the
application does not yet connect to signaling. Real connection and page wiring
start in P3.

## Layers

```text
apps/talk-desktop
  main.cpp / logging / CLI

ui/desktop
  base/       frameless window
  domain/     production state types
  pages/      prejoin and room pages
  theme/      icons, colors and QSS entry point
  widgets/    reusable production widgets
  resources/  QSS and image resources
```

## Build

```powershell
cd client-win
cmake --build --preset windows-x64-release --parallel
```

Executable:

```text
build/windows-x64/bin/Release/talk-desktop.exe
```

## Smoke and screenshots

```powershell
.\build\windows-x64\bin\Release\talk-desktop.exe --smoke-seconds 2
.\build\windows-x64\bin\Release\talk-desktop.exe --screenshot build\windows-x64\desktop-shots\prejoin.png
.\build\windows-x64\bin\Release\talk-desktop.exe --show-room-preview --screenshot build\windows-x64\desktop-shots\room.png
```

## Next integration phases

- P3: real connect/join/leave/settings
- P4: members, controls, PTT and host policy
- P5: real video stream registry and local preview
- P6: stats, diagnostics and error handling
- P7: packaging and release verification
