# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OpenA2DP is a minimal Windows Bluetooth A2DP (Advanced Audio Distribution Profile) control tool. It lists Bluetooth stereo audio devices, saves per-device codec/bitpool profiles, shows connection status, and supports reconnect/reset actions with a live diagnostic log. Licensed under GPL-3.0.

## Tech Stack & Build

- **Language**: C (all first-party code)
- **UI**: cimgui (Dear ImGui C bindings)
- **Graphics**: Win32 + Direct3D 11
- **Build**: Visual Studio + MSBuild + Windows SDK, x64 target
- **Optional driver**: KMDF (only if user-mode proves insufficient)

Build configurations: Debug and Release, x64. Warnings must be enabled. Third-party dependencies are pinned.

## Architecture

Layered design with four modules:

```
app/       UI, windowing, D3D11 rendering, cimgui panels, log view
core/      Enums, structs, config, validation, quirks, serialization
service/   Device enumeration, notifications, runtime status, reconnect/reset
driver/    Optional kernel-mode (KMDF) — only after proving user-mode gaps
```

Shared headers go in `include/`, dependencies in `third_party/`, build/test scripts in `scripts/`, tests in `tests/`.

Data flows top-down: `app` calls `service`, `service` uses `core`. The `driver` module (if needed) exposes a narrow interface consumed only by `service`.

## Design Constraints

- Plain C structs and functions — no heavy abstractions or frameworks
- One profile per device, human-readable config format
- Safe defaults; reject or clamp invalid values; unknown config fields must not crash loading
- User-mode first — before writing any driver code, document what user-mode cannot do
- Linux Bluetooth behavior is a concept reference only — do not port Linux code or architecture

## Logging

All code paths must log: startup/shutdown, device changes, profile load/save, reconnect/reset actions, API failures. Use timestamps, severity levels, and an in-memory ring buffer. File logging is deferred.

## Implementation Order (v0.1)

1. Repo skeleton
2. Core structs and config
3. Win32 + D3D11 + cimgui shell
4. Static mock UI
5. Device enumeration
6. Status panel
7. Logging panel
8. Reconnect/reset action
9. Persistence (profile save/load)
10. Evaluate driver need
