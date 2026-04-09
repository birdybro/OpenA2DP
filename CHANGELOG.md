# Changelog

All notable changes to OpenA2DP. Format based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
versioning loosely follows [SemVer](https://semver.org/).

## [Unreleased]

Nothing yet.

## [0.5.0] — 2026-04-09

Polish, observability, and cosmetics on top of the v0.4 Alt A2DP
integration. Headline features: full Bluetooth remote-event
debug logging, per-field dirty highlighting, capability inspection,
custom application icon.

### Added

- **Bluetooth remote-event observer** with two surfaces:
  - **Phase A**: low-level keyboard hook for `VK_MEDIA_*` virtual
    keys (catches BT drivers that translate AVRCP to synthetic
    keystrokes), and WASAPI default-endpoint master volume polling
    (catches AVRCP volume swipes regardless of driver).
  - **Phase B**: WinRT `GlobalSystemMediaTransportControlsSessionManager`
    polling (catches AVRCP play/pause/next/prev events that go
    through the SMTC route on Win10/11 instead of synthesizing
    keystrokes — confirmed needed for Pixel Buds Pro 2).
  - All events log to the standard ring buffer with `remote:`
    prefix; the user reads them and correlates with whatever
    gesture their device uses.
- **Stack indicator at the top of the window** — single colored
  line above the side-by-side layout shows the inferred active
  A2DP stack (Microsoft / Alternative / Multiple / None) so you
  don't need to scroll the status panel to see it.
- **Device Capabilities subsection** in the status panel reads
  the full `Capability\<addr>` registry subtree and renders the
  device's claimed supported codec list, SBC channel modes /
  rates / bitpool range, AAC channel modes / rates / max +
  peak bitrate.
- **AAC bitrate slider** now caps at the device's reported
  `Capability.AacBitrate` (256 kbps for Pixel Buds Pro 2) instead
  of the previous hardcoded 320.
- **Audio Latency** row in the status panel reads
  `Current.Delay` (Alt A2DP Driver, 1/10 ms units).
- **Hover tooltips** on every codec / SBC / AAC / watchdog widget
  with explanations of what they do, via a small `hover_help`
  helper that wraps text at 360 px.
- **Confirm modal** before stack switching — "Use Microsoft" /
  "Use Alternative" buttons now open a centered modal popup
  describing what's about to happen and asking the user to
  confirm with Switch / Cancel buttons. Stack switching is a
  ~30s destructive operation that's easy to mis-click.
- **Per-field dirty highlighting** — codec widgets whose value
  differs from the registry snapshot get an amber `FrameBg`,
  so the user can see exactly which fields they've edited
  (in addition to the existing global Apply button).
- **Custom application icon** embedded in both binaries via
  `IDI_APP_ICON` resource. Generated from `icon.png` by a small
  `scripts/png_to_ico.ps1` PowerShell helper that wraps the PNG
  in a single-entry .ico container. Used for window class
  hIcon/hIconSm and the Shell_NotifyIcon tray. Artwork by
  Ramy W. on Flaticon.
- **Real screenshot in README** showing a fully populated v0.4+
  main window. Replaces the "Coming soon" placeholder.

### Changed

- **`audio_status_query` runs on every refresh tick** (not just
  on connect transitions) so the WASAPI mix-format / sample
  rate / channels stay current mid-session.
- **Endpoint-miss debouncing** — the "connected but silent"
  warning now requires ≥2 consecutive failed audio status
  queries (~4 s) before firing, so transient races during
  codec switches don't false-trigger it.
- **SMTC log lines are codec-neutral** — dropped the
  Pixel-Buds-Pro-2-specific "(likely tap or app control)"
  qualifiers. Different headphones map gestures differently;
  the events we observe (playback state change, track change,
  volume change) are universal.

## [0.4.0] — 2026-04-09

The big "Alternative A2DP Driver integration" release. The codec
settings panel now actually does something on machines running
Alternative A2DP Driver — it reads from and writes back to the
driver's per-device registry config.

### Added

- **Alternative A2DP Driver registry integration** — `service/altdriver_config.c`
  reads `Devices\Current\<addr>` and `Devices\Next\<addr>` to
  populate the status panel with real codec parameters and to
  power-write user changes back to the driver. Bit-encoding for
  SBC and AAC fields decoded by inspecting Kevin's Pixel Buds Pro
  2 — see `reference_alt_a2dp_registry.md` in the memory dir.
- **Live editable codec settings** — when running on Alt A2DP
  Driver, the settings panel becomes write-through. Picks codec
  (SBC / AAC), SBC parameters (stereo mode / block size /
  allocation / subbands / max bitpool), AAC parameters (channels /
  sample rates / bitrate slider 64-320 kbps), and ABR enable.
  Microsoft stack greys the section out with an explanation.
- **Apply / Apply & Reconnect / Discard buttons** — write changes
  to `Next\<addr>` (admin only), optionally fire reconnect so the
  new settings apply on the next AVDTP negotiation, or revert
  unsaved edits to the registry snapshot.
- **Bitpool device-cap safety guard** — slider's max defaults to
  the device's `Capability.SbcMaximumBitpool` (37 for Pixel Buds
  Pro 2). "Override device max" checkbox lets you push higher,
  but is gated on Administrator elevation. The write path also
  enforces the clamp regardless of UI state, so an INI value
  loaded from a pre-safety-guard config can't slip through.
- **CLI codec commands**: `--show-codec-config`, `--set-codec`,
  `--set-bitpool`, `--set-aac-bitrate`. Set commands require
  admin and go through the same clamp logic as the GUI.
- **Registry probe diagnostic** — `--probe-registry` CLI command
  dumps every A2DP-related Windows registry value (depth-limited,
  read-only) for reconnaissance when investigating new schemas.
- **Background device probe rework** — slow Bluetooth APIs
  (`BluetoothEnumerateInstalledServices`, SetupAPI battery,
  Alt A2DP registry reads) all run on a single-slot worker
  thread, never on the UI thread. Replaces the failed inline
  approach from the v0.3 cycle that hung the window on launch.
- **VERSIONINFO resource** in both binaries (right-click →
  Properties → Details now shows real version metadata).
- **GitHub Actions CI** — builds on push, PR, and tag, uploads
  the two binaries as workflow artifacts. Tag pushes matching
  `v*.*.*` additionally produce a release zip and attach it to a
  GitHub Release.

### Changed

- **`audio_status.cpp` no longer caches a global enumerator** —
  each `audio_status_query` call creates and releases its own,
  fixing the technically-undefined cross-apartment access from
  worker threads (auto-heal especially). Companion `*_thread_init`
  / `*_thread_shutdown` helpers let workers set up their own
  per-thread COM apartment.
- **Stack switch worker** now fires the device probe at the end so
  install-flags / battery / Alt A2DP snapshot all reflect the new
  stack instead of going stale until the next manual rescan.
- **Build script** wipes `build/*.obj`, `*.res`, `*.lib`, `*.exp`
  before each compile so stale objects from removed source files
  can never get linked in.
- **Build script** auto-detects whether `cl.exe` is already on
  PATH (CI / Developer Command Prompt) and only falls back to the
  hardcoded VS 2026 vcvarsall path for local dev.

### Removed

- **Vestigial profile fields**: `allow_mono`, `allow_stereo`,
  `override_bitpool`, `auto_reduce_bitpool`. These were
  aspirational from v0.1 and never wired to any code path that
  reached the Bluetooth stack. The INI loader silently ignores
  unknown keys, so old config files still load — the dropped
  keys just become inert.

## [0.3.0] — 2026-04-09

### Added

- **A2DP stack control** — `service/driver_control.c` enumerates
  all A2DP-related Windows services via the SCM. Per-service
  Start/Stop buttons in a new "A2DP Stacks" panel under the
  device list. Stack switch worker stops the wrong-stack
  services, starts the right ones, then sync-reconnects each
  device. One-click "Use Microsoft" / "Use Alternative" buttons.
  Process-elevation detection via TokenElevation; controls grey
  out and tooltip-explain when not admin.
- **System tray icon** — Shell_NotifyIcon in the notification
  area with right-click menu (Show / Hide window, per-device
  Reconnect / Disable HFP, Switch stack MS / Alt, Quit). Tray
  callback messages routed through the main WndProc. Minimize
  button hides to tray. Toast notifications via NIM_MODIFY +
  NIF_INFO fired by auto-heal on real recovery and on give-up.
- **Two binaries built from one .obj set**: `OpenA2DP.exe`
  (`/SUBSYSTEM:WINDOWS`, `wWinMain`) for clean GUI launch with
  no console flash, `OpenA2DP-cli.exe` (`/SUBSYSTEM:CONSOLE`,
  `wmain`) for CLI use where cmd.exe waits properly. Both
  single-binary attempts (WINDOWS alone, CONSOLE-with-FreeConsole)
  failed differently before settling on this split.
- **More CLI commands**: `--list-devices`, `--list-stacks`,
  `--switch-stack ms|alt`, `--start-service`, `--stop-service`.
- **In-memory activity counters** (`core/stats.c`) for
  reconnects, auto-heal triggers/recoveries/failures, HFP
  watchdog fires, stack switches. Footer in the device list.
- **Persistent connection history** (`core/history.c`) — every
  connect/disconnect appended to
  `%APPDATA%\OpenA2DP\<addr>.history`. Status panel shows the
  most recent 12 events for the selected device. Auto-prunes
  oldest half when over 200 lines.
- **Window state persistence** — size + position via
  `GetWindowPlacement`, restored on next launch.
- **Status panel additions**: Bluetooth address, matched WASAPI
  endpoint friendly name, active stack inference label.
- **Battery level** read from `DEVPKEY_Bluetooth_Battery` via
  SetupAPI on the background device probe (when the device
  exposes it to the BT stack).

## [0.2.0] — 2026-04-09

### Added

- **Auto-heal** — per-device opt-in watchdog that detects the
  Windows 11 "connected but no audio" bug and automatically
  cycles AudioSink to recover. Worker thread, single-slot,
  capped at 3 attempts.
- **HFP watchdog** — per-device opt-in periodic re-disable of
  Handsfree to prevent Windows from falling back to narrowband
  mono SCO.
- **CLI mode** — `--reconnect`, `--disable-hfp`, `--enable-a2dp`
  for Task Scheduler / login script use.
- **Honest status panel** — dropped the hard-coded SBC / bitpool
  / codec lies that the v0.1 implementation was painting on
  every connected device. Status fields stay at 0 / UNKNOWN
  unless something actually measured them. Yellow warning when
  connected with no WASAPI endpoint visible (the auto-heal
  trigger condition).
- **Driver detect** — read-only SCM scan at startup logs every
  service or kernel driver whose name contains "a2dp", so the
  user can see which Bluetooth audio stack is in play.

### Changed

- **WASAPI endpoint matcher** falls back to friendly-name match
  when address-based matching misses (Alternative A2DP Driver
  endpoints don't embed the BT address in their device IDs).
  Diagnostic dump of all enumerated render endpoints when the
  matcher comes up empty.

## [0.1.0] — 2026-04-08

Initial release. Repo skeleton, core types and config, Win32 +
D3D11 + cimgui shell, device enumeration via Windows Bluetooth
APIs, status panel with real WASAPI mix-format data, in-memory
ring-buffer logging panel with severity filters, async
reconnect/reset actions, INI profile persistence with
auto-save, and the `docs/driver-evaluation.md` write-up
deciding to stay in user-mode (no KMDF driver).

[Unreleased]: https://github.com/birdybro/OpenA2DP/compare/v0.5.0...HEAD
[0.5.0]: https://github.com/birdybro/OpenA2DP/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/birdybro/OpenA2DP/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/birdybro/OpenA2DP/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/birdybro/OpenA2DP/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/birdybro/OpenA2DP/releases/tag/v0.1.0
