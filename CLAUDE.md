# CLAUDE.md

Guidance for Claude Code working in this repo. Released history lives in [CHANGELOG.md](CHANGELOG.md).

## Project

OpenA2DP is a Windows-only Bluetooth A2DP control tool: lists paired audio devices, persists per-device codec profiles, shows live status, and toggles AudioSink/Handsfree services. GPL-3.0.

## Tech Stack

- **C** for first-party code; **C++** only behind `extern "C"` for COM, ImGui backends, and WinRT.
- **UI**: cimgui (Dear ImGui C bindings) on Win32 + Direct3D 11.
- **Build**: MSVC (VS 2026), x64.
- **Win32 libs**: bthprops, ole32, propsys, advapi32, setupapi, runtimeobject, oleaut32, d3d11, dxgi, dwmapi.

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

Each binary embeds `VERSIONINFO` + `IDI_APP_ICON` via `scripts/version_{gui,cli}.rc`. Bump `OA2DP_VER_*` in BOTH .rc files and add a `CHANGELOG.md` entry on every release.

### CI / releases

`.github/workflows/build.yml` runs on push, PR, and `v*.*.*` tags. Uses `actions/checkout@v4` (recursive submodules), `ilammy/msvc-dev-cmd@v1`, then `scripts\build.bat`. Tag pushes zip both binaries and create a GitHub Release via `softprops/action-gh-release@v2`. Workflow-level env `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24=true` opts into Node 24 ahead of GitHub's 2026-06-02 forced cutover.

To cut a release: bump versions, update CHANGELOG, commit, then `git tag v0.X.Y -a -m "..." && git push --tags`.

### Tests

```
tests\build_and_test.bat
```

## Architecture

```
app/        UI, windowing, D3D11, panels, tray, CLI dispatch
  main.c          wmain + wWinMain, message loop, periodic ticks,
                  WM_SIZE/WM_TIMER live resize, save-on-WM_CLOSE
  panels.c        all ImGui panels + audio visualizer drawing
  cli.c           --reconnect/--list-*/--switch-stack/--set-codec/...
  tray.c          Shell_NotifyIcon + popup menu + balloon notifications
  renderer.cpp    D3D11 + ImGui backend (extern "C")

core/       Types, INI config, validation, ring-buffer log, stats, history
service/    Bluetooth + WASAPI + SCM + Alt A2DP registry + workers
  device_enum.c       BluetoothFindFirstDevice scan, refresh
  audio_status.cpp    WASAPI mix-format query (per-call enumerator)
  audio_visualizer.cpp WASAPI loopback capture + Goertzel band split
  actions.c           reconnect/reset/service toggle (async)
  auto_heal.c         connect-but-silent watchdog (async)
  hfp_watchdog.c      periodic Handsfree re-disable
  driver_control.c    SCM scan, service start/stop, stack switch worker
  altdriver_config.c  Alt A2DP Driver Capability/Current/Next R/W
  registry_probe.c    --probe-registry diagnostic dump
  remote_events.cpp   WH_KEYBOARD_LL + WASAPI volume polling
  smtc_observer.cpp   WinRT GlobalSystemMediaTransportControlsSessionManager
  device_probe.c      single-slot worker for slow BT APIs

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
- **Cross-thread COM**: `audio_status.cpp` does NOT cache an enumerator. Each call creates and releases its own. Workers use `oa2dp_audio_status_thread_init`/`_shutdown` for per-thread COM apartments. The visualizer worker initialises its own apartment directly.
- **Reconnect / reset**: `BluetoothSetServiceState` toggle with retry. AudioSink restored last so A2DP wins.
- **Auto-heal**: per-device opt-in (`auto_heal_enabled`). Triggered on disconnected→connected transition. Worker waits a settle period, probes WASAPI, runs sync reconnect cycle if no endpoint, capped attempts. Skips if `oa2dp_action_busy()`. Single-slot. Tray-notifies on real recovery and on give-up only (not healthy connects).
- **HFP watchdog**: per-device opt-in. `oa2dp_hfp_watchdog_tick` every 30s fires async Handsfree-disable on connected, opted-in devices. Idempotent.
- **A2DP stack control** (`driver_control.c`): SCM scan via `EnumServicesStatusExW(SERVICE_WIN32 | SERVICE_DRIVER)` for "a2dp" in name or display name. Start/Stop via `StartServiceW`/`ControlService` with state-poll wait. `ERROR_ACCESS_DENIED` → "relaunch as Administrator" hint. `oa2dp_stack_switch_async`/`_sync` orchestrate stop-other / start-target / sync-reconnect-each on a single-slot worker, then triggers `oa2dp_device_probe_start`. `oa2dp_process_is_elevated` (cached `TokenElevation`) greys out controls.
- **Alt A2DP Driver registry I/O** (`altdriver_config.c`): R/W under `HKLM\SYSTEM\CurrentControlSet\Services\AltA2DP\Parameters\Devices\{Capability,Current,Next}\<addr>`. SBC + AAC bit encoding documented in `include/oa2dp_altdriver_config.h`. **`oa2dp_altdriver_write_next` clamps `bitpool` to `Capability.SbcMaximumBitpool` unless override flag is set** — exceeding the device's max can damage some chips. Write path enforces clamp regardless of UI state.
- **Background device probe** (`device_probe.c`): single-slot worker for `BluetoothEnumerateInstalledServices` (which hung the UI when called inline — see commit 1af9b66), `DEVPKEY_Bluetooth_Battery` via SetupAPI, and Alt A2DP Capability/Current/Next reads. Triggered after initial scan, after rescans, and after stack switches.
- **Audio visualizer** (`audio_visualizer.cpp`): WASAPI loopback capture on the system default render endpoint, on its own background thread with its own COM apartment. Goertzel filter computes 16 log-spaced frequency bands (60 Hz–17 kHz), normalised to dB and curved with a sqrt for visibility. Peak-hold + decay smoothing. Critical-section protected float array snapshot for the UI thread. Renders via cimgui's draw list as colored bars in a dedicated child below the device list. Visible in both modes — works without headphones because it captures whatever Windows is mixing.
- **Remote-event tracking**: `remote_events.cpp` installs `WH_KEYBOARD_LL` for `VK_MEDIA_*` keys (catches synthetic media-key drivers) and polls WASAPI default-render master volume on a 200 ms tick (catches AVRCP volume swipes). `smtc_observer.cpp` (C++/WinRT) polls `Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager` for `PlaybackStatus` / Title / Artist / `SourceAppUserModelId` deltas (catches AVRCP play/pause/next/prev that flow through SMTC — required for Pixel Buds Pro 2). All log to the ring buffer with a `remote:` prefix. Needs `runtimeobject.lib` + `oleaut32.lib`.
- **Endpoint-miss debouncing**: `oa2dp_audio_status_query` runs every 2 s tick. `endpoint_miss_count` increments on misses for connected devices; the "connected but silent" warning fires only at `>= 2` (~4 s) so transient races during codec switches don't trigger it.
- **Per-field dirty highlighting**: `panels.c` `push_dirty_highlight`/`pop_dirty_highlight` wrap each codec widget with an amber `ImGuiCol_FrameBg` when the profile field differs from the registry snapshot. Bitpool comparison uses the post-clamp effective value.
- **Advanced Mode toggle**: `OA2DP_UIState.advanced_mode` defaults to 0, persisted in `window.ini` alongside the rect. When off, the UI hides A2DP stack control, codec editor, service toggles, watchdogs, capabilities, history, and the diagnostic log — leaving just Reconnect / Reset and a minimal status readout. The checkbox is pinned to the top-right of the header via `igCalcTextSize` + `igSameLine(content_w - box_w, 0)`.
- **Reset Settings button**: bottom of the device list, both modes. Confirm modal restores every device profile to `oa2dp_profile_defaults` (preserving `device_id`/`display_name`), writes them to disk, and sets `OA2DP_UIState.pending_window_reset = 1`. Main loop reads the flag AFTER `render_one_frame` and calls `SetWindowPos(hwnd, NULL, 100, 100, 1440, 900, ...)`. **Never call SetWindowPos from inside draw** — it synchronously dispatches WM_SIZE → render_one_frame, re-entering ImGui mid-frame and crashing.
- **Live window resize**: `WM_SIZE`, `WM_PAINT`, and a `WM_TIMER` driven from `WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE` all call `render_one_frame` so contents reflow in real time during a border drag instead of staying frozen until the modal loop exits. Minimum size enforced via `WM_GETMINMAXINFO` (880 × 232).
- **Save window placement on WM_CLOSE**: `save_window_placement(hwnd)` runs in the `WM_CLOSE` handler BEFORE `DestroyWindow`. The post-loop save is a fallback for tray-quit paths. Without this, X-button closes destroyed the HWND before `GetWindowPlacement` could see it and the size never persisted.
- **Modal popup centering**: `host_window_center(hwnd)` uses `GetWindowRect` for screen-space center (not `GetMainViewport().WorkPos`, which drifts to wrong monitors under multi-viewport). Position applied with `ImGuiCond_Always` so the pivot recenters against the popup's true auto-resized dimensions instead of collapsing on first frame.
- **System tray** (`tray.c`): `Shell_NotifyIcon` with custom `OA2DP_WM_TRAY` callback routed through main WndProc. Right-click rebuilds the popup menu each time. Minimize button hides to tray (`SC_MINIMIZE` intercepted). `oa2dp_tray_notify` uses `NIM_MODIFY` + `NIF_INFO` for balloon notifications.
- **CLI** (`cli.c`): `oa2dp_cli_run` dispatches all `--*` commands. Service-control and codec-set commands check `oa2dp_process_is_elevated` and refuse with a clear error when not admin. Sync-via-busy-poll for the otherwise async action set.
- **Stats** (`stats.c`): in-memory `InterlockedIncrement` counters for reconnects, heal triggers/recoveries/failures, HFP watchdog, stack switches. Per-session, not persisted. Footer in device list panel (advanced mode only).
- **History** (`history.c`): every connect/disconnect transition appended to `%APPDATA%\OpenA2DP\<addr>.history`. Auto-prunes oldest half over 200 lines. Status panel shows last 12 (advanced mode only).
- **Window state**: `oa2dp_window_state_save`/`_load` in `core/config.c` persist `x/y/w/h/advanced_mode` via `GetWindowPlacement` (captures the normal rect even when minimized to tray). Loaded before `CreateWindowW`.
- **Config persistence**: `oa2dp_profile_save`/`_load`, dirty-detected via memcmp snapshot, auto-saved every few seconds.
- **Logging**: 1024-entry ring buffer, severity-filtered UI, auto-scroll, "Copy to Clipboard" for issue dumps. All code paths must log: startup/shutdown, device changes, profile load/save, action results, API failures.
