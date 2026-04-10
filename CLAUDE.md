# CLAUDE.md

Guidance for Claude Code working in this repo. Released history lives in [CHANGELOG.md](CHANGELOG.md).

## Project

OpenA2DP is a Windows-only Bluetooth A2DP control tool: lists paired audio devices, persists per-device codec profiles, shows live status, and toggles AudioSink/Handsfree services. GPL-3.0.

## Tech Stack

- **C** for first-party code; **C++** only behind `extern "C"` for COM, ImGui backends, and WinRT.
- **UI**: cimgui (Dear ImGui C bindings) on Win32 + Direct3D 11.
- **Build**: MSVC (VS 2026), x64.
- **Win32 libs**: bthprops, ole32, propsys, advapi32, setupapi, runtimeobject, oleaut32, winmm, winhttp, d3d11, dxgi, dwmapi.

## Build

```
scripts\build.bat
```

`build.bat` auto-detects `cl.exe` on PATH (CI / dev cmd prompt) and falls back to a hardcoded vcvarsall path otherwise. Cleans `build/*.{obj,res,lib,exp}` first so stale objects from removed sources can't link in.

**Optimization stack**: `/O2 /GL /Gy /MT /Zc:inline` (plus `/GR-` for C++) and link-time `/LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO`. Whole-program optimization, function-level linking, dead-code elimination, identical-COMDAT folding. `/MT` keeps the binaries fully self-contained — no VC++ Redistributable required. Don't switch to `/MD` without removing the "portable zip" promise from the README.

**Outputs two binaries from one .obj set**:
- `OpenA2DP.exe` — `/SUBSYSTEM:WINDOWS`, `wWinMain`. GUI, no console flash.
- `OpenA2DP-cli.exe` — `/SUBSYSTEM:CONSOLE`, `wmain`. CLI, cmd.exe waits.

`main.c` defines both entry points; the linker picks one per `/SUBSYSTEM`. **Do not collapse to a single binary** — every previous attempt either flashed a console on Explorer launch or broke cmd.exe waiting.

Each binary embeds `VERSIONINFO` + `IDI_APP_ICON` via `scripts/version_{gui,cli}.rc`. Both .rc files `#include "oa2dp_version.h"` so the version macros live in **one place**: `include/oa2dp_version.h`. Bumping the version is a single-file edit + a CHANGELOG.md entry.

### CI / releases

`.github/workflows/build.yml` runs on push, PR, and `v*.*.*` tags. Uses `actions/checkout@v4` (recursive submodules), `ilammy/msvc-dev-cmd@v1`, then `scripts\build.bat`. Tag pushes zip both binaries and create a GitHub Release via `softprops/action-gh-release@v2`. Workflow-level env `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24=true` opts into Node 24 ahead of GitHub's 2026-06-02 forced cutover.

To cut a release: bump `oa2dp_version.h`, update CHANGELOG, commit, then `git tag v0.X.Y -a -m "..." && git push --tags`.

### Tests

```
tests\build_and_test.bat
```

## Architecture

```
app/        UI, windowing, D3D11, panels, tray, CLI dispatch
  main.c          wmain + wWinMain, message loop, periodic ticks,
                  WM_SIZE/WM_TIMER live resize, save-on-WM_CLOSE,
                  battery + HFP fallback notification checks
  panels.c        all ImGui panels + audio visualizer drawing
  cli.c           --reconnect/--list-*/--switch-stack/--set-codec/...
  tray.c          Shell_NotifyIcon + popup menu + balloon notifications
                  + master enable/disable for notifications
  renderer.cpp    D3D11 + ImGui backend (extern "C")

core/       Types, INI config, validation, ring-buffer log, stats, history
service/    Bluetooth + WASAPI + SCM + Alt A2DP registry + workers
  device_enum.c        BluetoothFindFirstDevice scan, refresh
  audio_status.cpp     WASAPI mix-format query (per-call enumerator)
                       + IPolicyConfig "set as default" via undocumented COM
  audio_visualizer.cpp WASAPI loopback capture + Goertzel band split + L/R
                       sample ring + outer reconnect loop with 1 Hz default-
                       endpoint poll for auto-recovery
  actions.c            reconnect/reset/service toggle + PlaySound test audio
  auto_heal.c          connect-but-silent watchdog (async)
  hfp_watchdog.c       periodic Handsfree re-disable
  driver_control.c     SCM scan, service start/stop, stack switch worker
  altdriver_config.c   Alt A2DP Driver Capability/Current/Next R/W
  registry_probe.c     --probe-registry diagnostic dump
  remote_events.cpp    WH_KEYBOARD_LL + WASAPI volume polling
  smtc_observer.cpp    WinRT GlobalSystemMediaTransportControlsSessionManager
  device_probe.c       single-slot worker for slow BT APIs
  update_check.c       GitHub Releases version check via WinHTTP

include/    Shared C headers
third_party/ cimgui (submodule)
```

Data flows top-down: `app` → `service` → `core`. C++ files expose `extern "C"` so first-party stays pure C.

## Design Constraints

- Plain C structs and functions; no heavy abstractions.
- C++ only where required (COM, ImGui, WinRT), always behind `extern "C"`.
- One INI profile per device under `%APPDATA%\OpenA2DP\`. Unknown keys must not crash loading.
- Safe defaults; clamp invalid values.
- Slow / blocking Win32 calls run on background workers, never the UI thread.
- User-mode only. No KMDF — see [docs/driver-evaluation.md](docs/driver-evaluation.md).
- Linux Bluetooth is concept reference only; do not port code or architecture.

## Key APIs and Patterns

- **Device scan**: `BluetoothFindFirstDevice`/`BluetoothFindNextDevice` filtered by audio Class of Device. `BluetoothGetDeviceInfo` for connection state.
- **Endpoint matching** (`audio_status.cpp`): two passes — (1) BT address (lowercase, no separators) substring in WASAPI device ID for the Microsoft stack, (2) device display name substring against `PKEY_Device_FriendlyName` for Alt A2DP and others. On total miss, dumps all enumerated render endpoints to the log.
- **Set as default endpoint** (`audio_status.cpp`): uses the undocumented `IPolicyConfig` COM interface (same as SoundSwitch/EarTrumpet/NirCmd). CLSID/IID/vtable order in the file is canonical — getting it wrong silently calls the wrong function and can corrupt audio settings, so do NOT reorder. Calls `SetDefaultEndpoint` for all three `ERole` values (Console, Multimedia, Communications).
- **Cross-thread COM**: `audio_status.cpp` does NOT cache an enumerator. Each call creates and releases its own. Workers use `oa2dp_audio_status_thread_init`/`_shutdown` for per-thread COM apartments. The visualizer worker initialises its own apartment directly.
- **Reconnect / reset / test sound**: `BluetoothSetServiceState` toggle with retry. AudioSink restored last so A2DP wins. `oa2dp_action_play_test_sound` plays `%WINDIR%\Media\tada.wav` via `PlaySoundA(SND_FILENAME | SND_ASYNC)` — independent of `action_busy`.
- **Auto-heal**: per-device opt-in (`auto_heal_enabled`). Triggered on disconnected→connected transition. Worker waits a settle period, probes WASAPI, runs sync reconnect cycle if no endpoint, capped attempts. Skips if `oa2dp_action_busy()`. Single-slot. Tray-notifies on real recovery and on give-up only (not healthy connects).
- **HFP watchdog**: per-device opt-in. `oa2dp_hfp_watchdog_tick` every 30s fires async Handsfree-disable on connected, opted-in devices. Idempotent.
- **HFP fallback detection** (`main.c` `check_hfp_fallback`): runs every refresh tick. Strict signature `channels == 1 && sample_rate <= 16000` (A2DP is always stereo at >=44.1 kHz; HFP narrowband is 8 kHz mono and mSBC wideband is 16 kHz mono). Per-address warned flag with hysteresis on the way out.
- **Battery low notifications** (`main.c` `check_battery_warnings`): runs every refresh tick. Threshold 20%, 5% hysteresis band. Per-address warned flag.
- **Update checker** (`update_check.c`): WinHTTP GET to `api.github.com/repos/birdybro/OpenA2DP/releases/latest`, ad-hoc parse of `tag_name` field (no JSON library), `version_newer` compares against `OA2DP_VER_STRING`. Once-per-process via `g_already_checked`. Network failure is silent. Triggered at startup if opted in, and on toggle-on.
- **Master tray notifications toggle** (`tray.c`): static `g_notifications_enabled` flag, default 0. `oa2dp_tray_notifications_set_enabled` flips it. `oa2dp_tray_notify` early-returns when disabled — tray icon and right-click menu still work, only balloons are suppressed. Persisted in `window.ini`.
- **A2DP stack control** (`driver_control.c`): SCM scan via `EnumServicesStatusExW(SERVICE_WIN32 | SERVICE_DRIVER)` for "a2dp" in name or display name. Start/Stop via `StartServiceW`/`ControlService` with state-poll wait. `ERROR_ACCESS_DENIED` → "relaunch as Administrator" hint. `ERROR_INVALID_SERVICE_CONTROL` (1052) is downgraded to INFO and treated as success — kernel drivers refuse to stop while in use, that's normal. `do_stack_switch` skips entries with `is_driver` set in both stop and start loops; only Win32 services flip during a switch. `oa2dp_process_is_elevated` (cached `TokenElevation`) greys out controls.
- **Stack inference** (`oa2dp_driver_active_stack_label`): when both BthA2dp and AltA2DP services are running, reports "Alternative A2DP Driver (Microsoft also loaded)" — AltA2DP's user-mode service intercepts WASAPI before BthA2dp can route audio. The "Use AltA2DP" button auto-disables when no non-BthA2dp service is detected, with hover help pointing at bluetoothgoodies.com.
- **Alt A2DP Driver registry I/O** (`altdriver_config.c`): R/W under `HKLM\SYSTEM\CurrentControlSet\Services\AltA2DP\Parameters\Devices\{Capability,Current,Next}\<addr>`. SBC + AAC bit encoding documented in `include/oa2dp_altdriver_config.h`. **`oa2dp_altdriver_write_next` clamps `bitpool` to `Capability.SbcMaximumBitpool` unless override flag is set** — exceeding the device's max can damage some chips.
- **Background device probe** (`device_probe.c`): single-slot worker for `BluetoothEnumerateInstalledServices` (which hung the UI when called inline — see commit 1af9b66), `DEVPKEY_Bluetooth_Battery` via SetupAPI, and Alt A2DP Capability/Current/Next reads.
- **Audio visualizer** (`audio_visualizer.cpp`): WASAPI loopback capture on the system default render endpoint, on its own background thread with its own COM apartment. Goertzel filter computes 16 log-spaced frequency bands (60 Hz–17 kHz), peak-hold + decay smoothing. Also writes raw L/R samples into a 4096-sample ring buffer (under the same critical section as the band updates) for the oscilloscope and vectorscope modes. **Outer reconnect loop**: capture is wrapped in an outer loop that re-acquires whenever the inner loop breaks. The inner loop polls the system default render endpoint ID once per second and breaks if it differs from the bound ID — without this the loopback would silently sit returning zero packets after a Reconnect cycle changed the default. **Four UI modes** rendered in `panels.c`, click anywhere on the visualizer to cycle: bars (with per-band peak-hold marker lines), spectrogram waterfall (jet palette, 512-frame history), oscilloscope (phosphor green polyline of mono samples), vectorscope (CRT-style XY scatter rotated 45° so mono collapses to vertical).
- **Remote-event tracking**: `remote_events.cpp` installs `WH_KEYBOARD_LL` for `VK_MEDIA_*` keys + polls WASAPI default-render master volume on a 200 ms tick. `smtc_observer.cpp` (C++/WinRT) polls `Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager` for `PlaybackStatus` / Title / Artist / `SourceAppUserModelId` deltas. Three orthogonal observation surfaces, all logging with a `remote:` prefix. Needs `runtimeobject.lib` + `oleaut32.lib`.
- **Endpoint-miss debouncing**: `oa2dp_audio_status_query` runs every 2 s. The "connected but silent" warning fires only after `endpoint_miss_count >= 2` so transient races during codec switches don't trigger it.
- **Per-field dirty highlighting**: `panels.c` `push_dirty_highlight`/`pop_dirty_highlight` wrap each codec widget with an amber `ImGuiCol_FrameBg` when the profile field differs from the registry snapshot. Bitpool comparison uses the post-clamp effective value.
- **Advanced Mode toggle**: `OA2DP_UIState.advanced_mode` defaults to 0, persisted in `window.ini`. When off, hides A2DP stack control, codec editor, service toggles, watchdogs, capabilities, history, and the diagnostic log. The header has three checkboxes pinned to the right: Tray Notifications, Auto Update Check, Advanced Mode — combined width computed via `igCalcTextSize` and `igSameLine(content_w - total_w, 0)`.
- **Reset Settings button**: bottom of the device list. Confirm modal restores every device profile to `oa2dp_profile_defaults` and sets `pending_window_reset = 1`. Main loop calls `SetWindowPos` AFTER `render_one_frame`. **Never call SetWindowPos from inside draw** — it synchronously dispatches WM_SIZE → render_one_frame, re-entering ImGui mid-frame and crashing.
- **Live window resize**: `WM_SIZE`, `WM_PAINT`, and a `WM_TIMER` from `WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE` all call `render_one_frame`. Min size enforced via `WM_GETMINMAXINFO` (880 × 232). Default size 1541 × 1010.
- **Save window placement on WM_CLOSE**: `save_window_placement(hwnd)` runs in WM_CLOSE BEFORE `DestroyWindow`. Without this, X-button closes destroyed the HWND before `GetWindowPlacement` could see it.
- **Modal popup centering**: `host_window_center(hwnd)` uses `GetWindowRect` for screen-space center (multi-viewport mode otherwise drifts to the wrong monitor). Position with `ImGuiCond_Always` pivot so the modal recenters against its true auto-resized dimensions.
- **System tray** (`tray.c`): `Shell_NotifyIcon` with custom `OA2DP_WM_TRAY` callback through main WndProc. Right-click rebuilds the popup menu each time. Minimize button hides to tray. `oa2dp_tray_notify` uses `NIM_MODIFY` + `NIF_INFO`, gated on the master enable flag.
- **CLI** (`cli.c`): `oa2dp_cli_run` dispatches all `--*` commands. Service-control and codec-set commands check `oa2dp_process_is_elevated` and refuse with a clear error when not admin.
- **Stats** (`stats.c`): in-memory `InterlockedIncrement` counters. Per-session, not persisted. Footer in device list panel (advanced mode only).
- **History** (`history.c`): every connect/disconnect transition appended to `%APPDATA%\OpenA2DP\<addr>.history`. Auto-prunes oldest half over 200 lines. Status panel shows last 12 (advanced mode only).
- **Window state** (`core/config.c`): persists `x/y/w/h/advanced_mode/update_check_enabled/tray_notifications_enabled` via `GetWindowPlacement`. Loaded before `CreateWindowW`.
- **Logging**: 1024-entry ring buffer, severity-filtered UI, auto-scroll, "Copy to Clipboard" for issue dumps.
