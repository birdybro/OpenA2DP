# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OpenA2DP is a minimal Windows Bluetooth A2DP (Advanced Audio Distribution Profile) control tool. It lists Bluetooth stereo audio devices, saves per-device codec/bitpool profiles, shows connection status, and supports reconnect/reset actions with a live diagnostic log. Licensed under GPL-3.0.

## Tech Stack & Build

- **Language**: C (first-party code), C++ only for COM/ImGui backend wrappers
- **UI**: cimgui (Dear ImGui C bindings, pinned at commit 0e533fd with imgui 1.92.7)
- **Graphics**: Win32 + Direct3D 11
- **Build**: Visual Studio 2026 + MSVC, x64 target
- **Bluetooth**: Windows Bluetooth APIs (BluetoothAPIs.h, bthprops.lib)
- **Audio status**: MMDevice/WASAPI COM APIs (ole32.lib, propsys.lib)
- **Optional driver**: KMDF (only if user-mode proves insufficient)

### Building

```
scripts\build.bat
```

Requires: Visual Studio with "Desktop development with C++" workload, Windows SDK. Output: `build\OpenA2DP.exe`.

### Running tests

```
tests\build_and_test.bat
```

## Architecture

Layered design with four modules:

```
app/          UI, windowing, D3D11 rendering, cimgui panels, log view
  main.c        Win32 entry point, message loop, device scan, periodic save
  panels.c/h    All UI panels (device list, settings, status, log)
  renderer.cpp/h  D3D11 + ImGui backend wrapper (C++ with extern "C" API)

core/         Enums, structs, config, validation, serialization
  config.c      INI-style profile save/load, config directory management
  validation.c  Safe defaults, value clamping
  log.c         In-memory ring buffer logger

service/      Device enumeration, notifications, runtime status, actions
  device_enum.c     Bluetooth device scan/refresh via BluetoothAPIs
  audio_status.cpp  Audio endpoint query via MMDevice/WASAPI (C++)
  actions.c         Reconnect/reset/service toggle (async, threaded)

include/      Shared C headers (oa2dp_types.h, oa2dp_config.h, etc.)
third_party/  cimgui (git submodule)
scripts/      Build and test scripts
tests/        Core module tests
```

Data flows top-down: `app` calls `service`, `service` uses `core`. C++ files (renderer.cpp, audio_status.cpp) expose `extern "C"` APIs so the rest of the app stays pure C.

## Design Constraints

- Plain C structs and functions — no heavy abstractions or frameworks
- C++ only where required (COM, ImGui backends) — always behind extern "C"
- One profile per device, human-readable INI config in %APPDATA%\OpenA2DP\
- Safe defaults; reject or clamp invalid values; unknown config fields must not crash loading
- Blocking operations (Bluetooth service toggles) run on background threads
- User-mode first — before writing any driver code, document what user-mode cannot do
- Linux Bluetooth behavior is a concept reference only — do not port Linux code or architecture

## Key APIs and Patterns

- **Device scan**: `BluetoothFindFirstDevice`/`BluetoothFindNextDevice`, filtered by Class of Device audio bits
- **Status refresh**: `BluetoothGetDeviceInfo` for connection state, MMDevice `IAudioClient::GetMixFormat` for audio endpoint data
- **Reconnect/reset**: `BluetoothSetServiceState` to toggle AudioSink/Handsfree services, with retry logic
- **Config persistence**: INI files via `oa2dp_profile_save`/`oa2dp_profile_load`, dirty detection via memcmp snapshot
- **Logging**: Ring buffer (1024 entries), severity-filtered UI with auto-scroll

## Logging

All code paths must log: startup/shutdown, device changes, profile load/save, reconnect/reset actions, API failures. Use timestamps, severity levels, and an in-memory ring buffer. File logging is deferred.

## Implementation Status (v0.1)

1. ~~Repo skeleton~~ (done)
2. ~~Core structs and config~~ (done)
3. ~~Win32 + D3D11 + cimgui shell~~ (done)
4. ~~Static mock UI~~ (done)
5. ~~Device enumeration~~ (done)
6. ~~Status panel~~ (done)
7. ~~Logging panel~~ (done)
8. ~~Reconnect/reset action~~ (done)
9. ~~Persistence (profile save/load)~~ (done)
10. Evaluate driver need
