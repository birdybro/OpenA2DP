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
  main.c        Win32 entry point, message loop, device scan, periodic save, CLI dispatch
  panels.c/h    All UI panels (device list, settings, status, log)
  cli.c         Headless command-line action runner (--reconnect/--disable-hfp/--enable-a2dp)
  renderer.cpp/h  D3D11 + ImGui backend wrapper (C++ with extern "C" API)

core/         Enums, structs, config, validation, serialization
  config.c      INI-style profile save/load, config directory management
  validation.c  Safe defaults, value clamping
  log.c         In-memory ring buffer logger

service/      Device enumeration, notifications, runtime status, actions
  device_enum.c     Bluetooth device scan/refresh via BluetoothAPIs
  audio_status.cpp  Audio endpoint query via MMDevice/WASAPI (C++)
  actions.c         Reconnect/reset/service toggle (async, threaded)
  auto_heal.c       Connect-but-no-audio watchdog (async, threaded)
  hfp_watchdog.c    Periodic Handsfree re-disable (idempotent, fired from main loop)
  driver_detect.c   Read-only SCM scan for A2DP-related services at startup

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
- **Auto-heal**: Per-device opt-in (`auto_heal_enabled` in profile). Triggered from `oa2dp_device_refresh_status` on a disconnected→connected transition. Worker thread waits a settle period, probes WASAPI via `oa2dp_audio_status_query`, and runs a synchronous reconnect cycle if no endpoint is found, with a hard attempt cap. Skips attempts when `oa2dp_action_busy()` is set so it can't race a manual button click. Single-slot via its own busy flag.
- **Config persistence**: INI files via `oa2dp_profile_save`/`oa2dp_profile_load`, dirty detection via memcmp snapshot
- **Logging**: Ring buffer (1024 entries), severity-filtered UI with auto-scroll

## Logging

All code paths must log: startup/shutdown, device changes, profile load/save, reconnect/reset actions, auto-heal triggers/results, API failures. Use timestamps, severity levels, and an in-memory ring buffer. File logging is deferred.

## Implementation Status

v0.1 (complete):

1. ~~Repo skeleton~~
2. ~~Core structs and config~~
3. ~~Win32 + D3D11 + cimgui shell~~
4. ~~Static mock UI~~
5. ~~Device enumeration~~
6. ~~Status panel~~
7. ~~Logging panel~~
8. ~~Reconnect/reset action~~
9. ~~Persistence (profile save/load)~~
10. ~~Driver evaluation~~ — see [docs/driver-evaluation.md](docs/driver-evaluation.md). Decision: stay user-mode, no KMDF.

v0.2 (complete):

- ~~Auto-heal: connect-but-no-audio watchdog (per-device opt-in)~~
- ~~HFP watchdog: periodic re-disable of Handsfree, per-device opt-in~~
- ~~CLI mode (`--reconnect`, `--disable-hfp`, `--enable-a2dp`) for Task Scheduler use~~
- ~~Honest status panel: dropped hard-coded SBC/bitpool fields, only show measured WASAPI values~~
- ~~A2DP driver detection (read-only): logs all SCM services with "a2dp" in name/display name at startup~~
