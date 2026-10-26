# talk-desktop production UI

## Purpose

`talk-desktop` is the production Qt Widgets desktop application for Windows.
It is separate from the legacy `talk-qt` diagnostic UI and the visual-only
`qt-ui-prototype` project.

## P1-P5 boundary

The current P1-P5 deliverable is a production session, room-control and video
shell:

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
- real dev-token, connect, join and leave lifecycle
- settings persistence without storing the token
- reconnect, kicked and error state propagation
- real member rows with role, audio/video state and audio level
- microphone, camera, screen-share and deafen controls
- PTT/moderated floor request, release, queue and countdown
- host grant/kick, mute-all and room-policy editor
- toast feedback for control and host actions
- camera enumeration and device selection on the prejoin page
- local camera preview through the C ABI frame callback
- `mid`-keyed remote video registration and 33ms frame coalescing
- immediate producer-pause clearing and 5-second no-frame recycling
- grid, focus, 1v1 picture-in-picture and screen-share layouts
- real `QImage` video surfaces with injectable surface factory
- screenshot and smoke-test command line modes

## Layers

```text
apps/talk-desktop
  main.cpp / logging / CLI

ui/desktop
  base/       frameless window
  domain/     production state types
  pages/      prejoin and room pages
  services/   settings and session controller
  state/      session store and video stream registry
  theme/      icons, colors and QSS entry point
  video/      video surface, tile and layout engine
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
.\build\windows-x64\bin\Release\talk-desktop.exe --start-camera-preview --screenshot-delay-ms 7000 --screenshot build\windows-x64\desktop-shots\preview.png
```

## Next integration phases

- P6: stats, diagnostics and error handling
- P7: packaging and release verification

## Automated session smoke

```powershell
.\build\windows-x64\bin\Release\desktop_session_smoke.exe
```

The smoke obtains a development token, connects, joins, leaves and exits.
The P4 smoke additionally validates camera, screen share, PTT, policy update
and mute-all across two real clients:

```powershell
.\build\windows-x64\bin\Release\desktop_p4_smoke.exe
```

The P5 smoke validates remote screen-share registration, a real decoded frame,
producer-pause clearing and the local-preview command:

```powershell
.\build\windows-x64\bin\Release\desktop_p5_smoke.exe
```

For UI-level verification:

```powershell
.\build\windows-x64\bin\Release\talk-desktop.exe `
  --auto-join-url ws://127.0.0.1:8080/talk/v1/ws `
  --auto-join-room p3-room `
  --auto-join-uid p3-user `
  --auto-join-dev-token `
  --screenshot-delay-ms 2500 `
  --screenshot build\windows-x64\desktop-shots\connected-room.png
```

For a two-client video UI check, start one client with
`--auto-start-share-ms 5000` and another with a screenshot delay of at least
10 seconds. `--auto-start-camera-ms` is also available for camera testing.
