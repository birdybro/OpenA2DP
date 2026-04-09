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

Requires: Visual Studio with "Desktop development with C++" workload, Windows SDK. The build script auto-detects whether `cl.exe` is already on PATH (developer command prompt or CI environment) and only falls back to a hardcoded local-dev `vcvarsall.bat` location otherwise. Each invocation also wipes `build/*.obj`, `*.res`, `*.lib`, `*.exp` first so stale objects from removed source files can never be linked in.

**Outputs two binaries from the same .obj set:**
- `build\OpenA2DP.exe` — `/SUBSYSTEM:WINDOWS`, entry `wWinMain`. The GUI. No console window.
- `build\OpenA2DP-cli.exe` — `/SUBSYSTEM:CONSOLE`, entry `wmain`. Headless CLI. cmd.exe / PowerShell wait for it.

`main.c` defines both `wWinMain` and `wmain`; the linker pulls in only the entry point matching each binary's `/SUBSYSTEM`. The CLI binary refuses to launch the GUI on no-args (prints usage); the GUI binary ignores any args. **Do not collapse these back into a single binary** — every previous attempt has either flashed a console window on Explorer launch or made cmd.exe not wait for CLI mode.

Each binary also has a `VERSIONINFO` resource embedded via `scripts/version_gui.rc` / `scripts/version_cli.rc` (compiled by `rc.exe`, linked alongside the .obj files). Right-click → Properties → Details shows the version. Bumping the version means updating the `OA2DP_VER_*` macros in BOTH .rc files plus a `CHANGELOG.md` entry.

### Continuous integration

`.github/workflows/build.yml` runs on every push to `main`, every PR, and every tag matching `v*.*.*`:
- `actions/checkout@v4` with `submodules: recursive` for cimgui + nested imgui
- `ilammy/msvc-dev-cmd@v1` to put `cl.exe` / `link.exe` / `rc.exe` on PATH
- `scripts\build.bat` to compile both binaries
- `actions/upload-artifact@v4` uploads `OpenA2DP.exe` + `OpenA2DP-cli.exe` as workflow artifacts on every run
- On tag pushes only: `Compress-Archive` zips both binaries and `softprops/action-gh-release@v2` creates a GitHub Release of the same tag and attaches the zip

To cut a new release: bump version in both .rc files, update `CHANGELOG.md`, commit, then `git tag v0.X.Y -a -m "..." && git push --tags`. CI does the rest.

### Running tests

```
tests\build_and_test.bat
```

## Architecture

Layered design with four modules:

```
app/          UI, windowing, D3D11 rendering, cimgui panels, tray icon
  main.c        wmain (CLI entry) and wWinMain (GUI entry); message loop;
                device scan; periodic save; HFP watchdog tick; window state save
  panels.c/h    All UI panels (device list, A2DP stacks, settings, status,
                connection history, activity counters, log)
  cli.c         Headless command-line action runner — listing, reconnect,
                stack switch, service start/stop
  tray.c        Shell_NotifyIcon, popup menu, balloon-tip notifications
  renderer.cpp/h  D3D11 + ImGui backend wrapper (C++ with extern "C" API)

core/         Enums, structs, config, validation, serialization
  config.c      INI-style profile save/load, window state, config directory
  validation.c  Safe defaults, value clamping
  log.c         In-memory ring buffer logger
  stats.c       In-memory atomic activity counters
  history.c     Per-device connect/disconnect history file IO

service/      Device enumeration, notifications, runtime status, actions
  device_enum.c     Bluetooth device scan/refresh via BluetoothAPIs
  audio_status.cpp  Audio endpoint query via MMDevice/WASAPI (C++) — per-call
                    enumerator, no global, with thread-init helpers for workers
  actions.c         Reconnect/reset/service toggle (async, threaded)
  auto_heal.c       Connect-but-no-audio watchdog (async, threaded)
  hfp_watchdog.c    Periodic Handsfree re-disable (idempotent, fired from main loop)
  driver_control.c  SCM scan + service start/stop + stack switch worker
  altdriver_config.c Read+write Alternative A2DP Driver per-device codec config
                    via its registry storage; bit-encoding decoded for SBC + AAC
  registry_probe.c  Read-only registry diagnostic dump (used by --probe-registry)
  device_probe.c    Background worker for slow Bluetooth APIs (installed
                    services + battery via SetupAPI)

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
- **Endpoint matching**: `oa2dp_audio_status_query` does two passes — first looks for the BT address (lowercase, no separators) inside the WASAPI endpoint device ID (works for the Microsoft stack), then falls back to substring-matching the device's display name against `PKEY_Device_FriendlyName` (works for Alternative A2DP Driver and other stacks that don't embed the address in IDs). On a complete miss it dumps all enumerated render endpoints to the log so the matcher can be iterated on real data.
- **Reconnect/reset**: `BluetoothSetServiceState` to toggle AudioSink/Handsfree services, with retry logic
- **Auto-heal**: Per-device opt-in (`auto_heal_enabled` in profile). Triggered from `oa2dp_device_refresh_status` on a disconnected→connected transition. Worker thread waits a settle period, probes WASAPI via `oa2dp_audio_status_query`, and runs a synchronous reconnect cycle if no endpoint is found, with a hard attempt cap. Skips attempts when `oa2dp_action_busy()` is set so it can't race a manual button click. Single-slot via its own busy flag. Fires `oa2dp_tray_notify` on real recovery (not on healthy connects, which would be too noisy) and on give-up.
- **HFP watchdog**: Per-device opt-in (`hfp_watchdog_enabled`). `oa2dp_hfp_watchdog_tick` is called every 30s from the main loop and fires async Handsfree-disable on connected, opted-in devices. Idempotent: re-disabling an already-off service is a no-op at the API level. One device per tick (single-slot async actions).
- **A2DP stack control**: `service/driver_control.c` enumerates SCM services with `EnumServicesStatusExW(SERVICE_WIN32 | SERVICE_DRIVER)` looking for "a2dp" in name or display name. `oa2dp_driver_start`/`_stop` open the service with `SERVICE_START`/`SERVICE_STOP`, then poll state with a 10s budget. `ERROR_ACCESS_DENIED` is logged with an explicit "relaunch as Administrator" hint. `oa2dp_stack_switch_async`/`_sync` orchestrate stop-other / start-target / reconnect-each-device on a single-slot worker; `oa2dp_process_is_elevated` (cached `TokenElevation` check) lets the UI grey out the buttons in non-elevated processes.
- **Background device probe**: `service/device_probe.c` is the off-thread home for Bluetooth APIs that block. Calls `BluetoothEnumerateInstalledServices` (which previously hung the UI when called inline — see commit 1af9b66), reads `DEVPKEY_Bluetooth_Battery` via SetupAPI, and pulls the Alt A2DP Driver per-device codec config (Capability + Current + Next) via `oa2dp_altdriver_read_current` / `_read_next`. Writes results back to the status struct atomically. Single-slot, triggered after the initial scan, after every device-change rescan, AND after every stack switch.
- **Alternative A2DP Driver registry I/O**: `service/altdriver_config.c` reads from and writes to `HKLM\SYSTEM\CurrentControlSet\Services\AltA2DP\Parameters\Devices\{Capability,Current,Next}\<addr>`. The bit encoding for `SbcChannelMode` / `SbcSamplingFrequency` / `AacChannelMode` / `AacSamplingFrequency` etc. is documented in `include/oa2dp_altdriver_config.h` (decoded by inspecting Kevin's Pixel Buds Pro 2). `oa2dp_altdriver_write_next` requires admin and clamps `bitpool` to `Capability.SbcMaximumBitpool` unless the user explicitly enabled the override flag — exceeding the device's reported max can damage some chips.
- **Cross-thread COM**: `audio_status.cpp` no longer caches a global enumerator. Each `audio_status_query` creates and releases its own per call. Worker threads (auto_heal especially) call `oa2dp_audio_status_thread_init` / `_thread_shutdown` to set up their own per-thread COM apartment.
- **System tray**: `app/tray.c` adds a `Shell_NotifyIcon` and routes a custom `OA2DP_WM_TRAY` callback through the main WndProc. Right-click builds a fresh popup menu each time so it reflects current device/stack state. Minimize button hides to tray (`SC_MINIMIZE` intercepted in WndProc). `oa2dp_tray_notify` uses `NIM_MODIFY` with `NIF_INFO` for balloon notifications.
- **CLI mode**: Two binaries from one .obj set (see Building section). `oa2dp_cli_run` in `app/cli.c` dispatches `--reconnect` / `--disable-hfp` / `--enable-a2dp` / `--list-devices` / `--list-stacks` / `--switch-stack ms|alt` / `--start-service` / `--stop-service`. Stack switch and service start/stop check `oa2dp_process_is_elevated` and refuse with a clear error when not admin.
- **Stats counters**: `core/stats.c` keeps in-memory atomic counters (`InterlockedIncrement`) for reconnects, auto-heal triggers/recoveries/failures, HFP watchdog fires, and stack switches. Per-session, not persisted. Displayed as a footer in the device list panel.
- **Connection history**: `core/history.c` appends each connect/disconnect transition (from `device_enum.c`) to `%APPDATA%\OpenA2DP\<addr>.history` as a timestamped line. Auto-prunes the oldest half when over 200 lines. Status panel renders the most recent 12 events for the selected device.
- **Window state**: `oa2dp_window_state_save`/`_load` in `core/config.c` persist the window rect to `window.ini`. Saved on shutdown via `GetWindowPlacement` (so the captured rect is the "normal" position even when minimized to tray). Loaded before `CreateWindowW`.
- **Config persistence**: INI files via `oa2dp_profile_save`/`oa2dp_profile_load`, dirty detection via memcmp snapshot
- **Logging**: Ring buffer (1024 entries), severity-filtered UI with auto-scroll, "Copy to Clipboard" button for issue dumps

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

v0.4.0 (complete, released 2026-04-09 — see [CHANGELOG.md](CHANGELOG.md)):

- ~~Alternative A2DP Driver registry integration: read Current/Next/Capability~~
- ~~Live editable codec settings (SBC + AAC) via Apply / Apply & Reconnect / Discard~~
- ~~Bitpool device-cap safety guard with admin-gated override~~
- ~~CLI codec commands: --show-codec-config, --set-codec, --set-bitpool, --set-aac-bitrate~~
- ~~Registry probe diagnostic (--probe-registry CLI command)~~
- ~~Cross-thread COM correctness in audio_status.cpp~~
- ~~Vestigial profile field cleanup (allow_mono, allow_stereo, override_bitpool, auto_reduce_bitpool)~~
- ~~Build script CI-friendly (auto-detect cl.exe on PATH) and clean step~~
- ~~VERSIONINFO embedded in both binaries~~
- ~~GitHub Actions CI + tag-triggered release publishing~~
- ~~CHANGELOG.md~~

v0.3 (complete):

- ~~A2DP stack control: Start/Stop and one-click "Switch to Microsoft" / "Switch to Alternative" with elevation detection~~
- ~~System tray icon with right-click menu for Reconnect / Disable HFP / Switch Stack / Show / Quit~~
- ~~Stack switch worker that stops the wrong-stack services, starts the right ones, then reconnects each device~~
- ~~Window state persistence (size + position via `GetWindowPlacement`)~~
- ~~More CLI commands: `--list-devices`, `--list-stacks`, `--switch-stack`, `--start-service`, `--stop-service`~~
- ~~Toast notifications on auto-heal recovery and failure (`oa2dp_tray_notify` via `NIM_MODIFY` + `NIF_INFO`)~~
- ~~Two-binary build: `OpenA2DP.exe` (`/SUBSYSTEM:WINDOWS`) and `OpenA2DP-cli.exe` (`/SUBSYSTEM:CONSOLE`)~~
- ~~Status panel: BT address, matched WASAPI endpoint name, active stack inference~~
- ~~In-memory activity counters (reconnects, heal triggers/recoveries/failures, HFP watchdog, stack switches)~~
- ~~Persistent per-device connection history (`%APPDATA%\OpenA2DP\<addr>.history`)~~
- ~~Background device probe for slow Bluetooth APIs (installed services + battery via SetupAPI)~~
