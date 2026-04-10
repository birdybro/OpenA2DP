# Changelog

All notable changes to OpenA2DP. Format based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
versioning loosely follows [SemVer](https://semver.org/).

## [Unreleased]

Nothing yet.

## [0.7.0] — 2026-04-09

The "feature buffet" cycle.  Two new one-click audio actions, a
spectrum visualizer that grew three more modes, opt-in
notifications for battery / HFP fallback / new releases, smarter
A2DP stack control, and a single source of truth for the version
number.

### Added

- **Set as Default audio endpoint** button in both action rows.
  Calls the undocumented IPolicyConfig::SetDefaultEndpoint COM
  interface (same one SoundSwitch / EarTrumpet / NirCmd use) for
  all three ERoles, snapping the BT device back to default
  without going through Sound Settings.  Same two-pass endpoint
  matching as oa2dp_audio_status_query.
- **Test Audio** button.  Plays %WINDIR%\Media\tada.wav via
  PlaySoundA(SND_FILENAME | SND_ASYNC) so the user can verify
  the BT device is actually receiving audio without alt-tabbing
  to start a song.  Independent of action_busy.
- **Battery low notifications.**  On every 2-second refresh tick,
  walks the device list and fires a one-shot tray notification
  when a connected device drops to or below 20% battery.  5%
  hysteresis band so a value oscillating around the threshold
  doesn't re-fire.
- **HFP fallback detection.**  Detects when the WASAPI mix format
  collapses to mono at <=16 kHz on a connected device (the
  unambiguous A2DP→HFP/SCO signature) and tray-notifies "HFP
  took over: <device> — Audio dropped to <n> Hz mono — voice
  quality only.  Use Disable HFP or enable HFP Watchdog to fix."
  Hysteresis: clears when the format goes back above HFP.
- **GitHub releases update checker.**  New service/update_check.c
  hits api.github.com/repos/birdybro/OpenA2DP/releases/latest via
  WinHTTP, parses the latest tag with an ad-hoc string match (no
  JSON library), compares against the embedded version, and
  tray-notifies if a newer release exists.  One-shot per process
  via internal guard, network failure is silent.  Opt-in via
  the new "Auto Update Check" header checkbox.
- **Master Tray Notifications toggle.**  oa2dp_tray_notify is
  gated on a static enable flag set via
  oa2dp_tray_notifications_set_enabled.  Default OFF — the user
  opts in via the new "Tray Notifications" checkbox in the top
  header.  Suppresses every balloon (auto-heal, battery, HFP,
  update) but leaves the tray icon and right-click menu fully
  functional.  Persisted in window.ini.
- **Audio visualizer: spectrogram waterfall mode.**  Scrolling
  jet-palette heatmap of the last ~512 frames of band data.
  Same Goertzel data as bars, different render.
- **Audio visualizer: oscilloscope mode.**  Classic phosphor-green
  polyline of the (L+R)/2 mono samples scrolling left-to-right.
- **Audio visualizer: vectorscope mode.**  CRT-style XY scatter of
  L vs R samples rotated 45 degrees so mono content collapses to
  a vertical line, drawn as connected line segments with a
  brightness ramp for the phosphor-trail look.
- **Audio visualizer: peak-hold marker lines on bars.**  Each bar
  tracks a per-band peak that rises instantly and decays over
  ~1.5 seconds, drawn as a 2-px white marker — like every audio
  plugin level meter.
- **Audio visualizer: click anywhere to cycle modes.**  Bars →
  waterfall → oscilloscope → vectorscope → bars.  No buttons.
- **WASAPI loopback recovery.**  The visualizer's capture worker
  is now an outer reconnect loop wrapping the inner capture loop.
  Polls the system default render endpoint ID once per second
  and re-acquires when it changes — without this, after a
  Reconnect cycle the loopback handle would silently sit there
  returning zero packets indefinitely (no WASAPI error fires
  when the default endpoint changes under you, only when the
  bound device is fully removed).
- **Single source of truth for the version number** in
  include/oa2dp_version.h.  Both .rc files now #include it
  instead of redefining the macros locally.  Bumping the version
  is a one-file edit.
- **L/R sample API on the visualizer worker**
  (oa2dp_audio_visualizer_get_samples) feeding the new
  oscilloscope and vectorscope modes.  4096-sample ring buffer
  written under the same critical section as the band updates.

### Changed

- **Default window size** bumped from 1440x900 to 1541x1010.
- **Stack switch skips kernel drivers** in both the stop and
  start loops.  Kernel-mode A2DP drivers (BthA2dp, AltA2DP) refuse
  SERVICE_CONTROL_STOP while Windows audio is using them and
  don't actually need to flip during a switch — only the user-mode
  services do.  Eliminates the spurious 1052 ERROR lines that
  used to appear in the log every time the user clicked Switch.
- **oa2dp_driver_stop downgrades ERROR_INVALID_SERVICE_CONTROL**
  (1052) to an INFO line that explains the kernel driver is in
  use and that's harmless.  Returns 0 instead of -1 so callers
  don't treat it as a failure.
- **Stack inference: when both BthA2dp and AltA2DP are loaded**,
  the active-stack indicator now reads "Alternative A2DP Driver
  (Microsoft also loaded)" instead of "Multiple stacks running".
  AltA2DP's user-mode service intercepts WASAPI before BthA2dp
  can route audio, so it really is the active stack regardless
  of whether the BthA2dp kernel driver is still loaded (which it
  usually is, because Windows refuses to unload it at runtime).
  The stack indicator color now matches "Alternative" first so
  the new label renders green.
- **"Use Alternative" button renamed to "Use AltA2DP"** for
  consistency with the rest of the UI.  The button now also
  detects whether the Alternative A2DP Driver is installed
  (presence of any non-BthA2dp service in the SCM scan) and
  greys out with a tooltip pointing at bluetoothgoodies.com
  when it isn't.

### CI

- Workflow-level env `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24=true` to
  opt all JS-based actions (checkout, upload-artifact, msvc-dev-cmd,
  action-gh-release) into Node 24 ahead of GitHub's 2026-06-02
  forced cutover.

## [0.6.0] — 2026-04-09

The "approachable defaults + eye candy" cycle.  Headline features:
Advanced Mode toggle that hides the power-user surfaces from
first-time users, a Reset Settings escape hatch, a real-time
WASAPI loopback audio visualizer, live window resize, and a
tighter optimization stack on the build.

### Added

- **Advanced Mode toggle** in the top-right of the header.
  Default off.  When off, hides A2DP stack control, codec
  editor, service toggles, watchdogs, capabilities, history,
  and the diagnostic log — leaving only Reconnect / Reset and
  a minimal status readout (connection state, battery, the
  connect-but-silent warning).  Persisted in `window.ini`.
- **Audio visualizer** — WASAPI loopback capture on the system
  default render endpoint, on a background thread with its own
  COM apartment.  Goertzel filter computes 16 log-spaced
  frequency bands (60 Hz – 17 kHz), peak-hold + decay smoothed
  for stable bars.  Renders as colored bars (cyan → green →
  yellow → red gradient by amplitude) in its own child below
  the device list, fills 100% of the child's content region.
  Visible in both simple and advanced modes — works even with
  no headphones connected, since it captures whatever Windows
  is mixing.
- **Reset All Settings to Defaults** button at the bottom of
  the device list.  Confirm modal prevents accidental clicks.
  Resets every device profile to its built-in defaults
  (preserving `device_id` and `display_name`), writes them
  back to disk, and snaps the main window back to its default
  1440×900 rect.  Window snap is deferred to between frames
  so `SetWindowPos` doesn't re-enter the renderer mid-draw.
- **Live window resize** — `WM_SIZE`, `WM_PAINT`, and a
  `WM_TIMER` driven from `WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE`
  all call a shared `render_one_frame` helper, so the window
  contents reflow in real time during a border drag instead
  of staying frozen until the modal resize loop exits.
- **Minimum window size 880 × 232** enforced via
  `WM_GETMINMAXINFO`, so the layout can't get crushed.

### Changed

- **Compiler/linker flags tightened** for smaller, faster
  binaries: `/GL /Gy /MT /Zc:inline` (plus `/GR-` for C++)
  and `/LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO` at link time.
  Whole-program optimization, function-level linking, dead
  code elimination, identical COMDAT folding.  `/MT` keeps
  the binaries fully self-contained — no VC++ Redistributable
  required on the target machine.
- **Confirm modal centering** moved from
  `GetMainViewport().WorkPos` (which drifted to wrong monitors
  under ImGui's multi-viewport mode) to `GetWindowRect` of the
  host HWND, with `ImGuiCond_Always` pivot so the modal
  recenters against its true auto-resized dimensions instead
  of collapsing on the first frame.
- **Window placement** is now saved in `WM_CLOSE` before the
  HWND is destroyed.  Closing via the X button previously
  triggered `DestroyWindow` before the main loop's
  `GetWindowPlacement` call could run, so the window size
  never persisted across sessions.

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

[Unreleased]: https://github.com/birdybro/OpenA2DP/compare/v0.7.0...HEAD
[0.7.0]: https://github.com/birdybro/OpenA2DP/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/birdybro/OpenA2DP/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/birdybro/OpenA2DP/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/birdybro/OpenA2DP/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/birdybro/OpenA2DP/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/birdybro/OpenA2DP/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/birdybro/OpenA2DP/releases/tag/v0.1.0
