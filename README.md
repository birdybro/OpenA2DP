# OpenA2DP

![OpenA2DP main window](screenshot.png)

A minimal **Windows-only** Bluetooth A2DP control tool. Reconnect, reset, and (optionally) tune the codec settings of your Bluetooth audio devices.

[![Build](https://github.com/birdybro/OpenA2DP/actions/workflows/build.yml/badge.svg)](https://github.com/birdybro/OpenA2DP/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/birdybro/OpenA2DP)](https://github.com/birdybro/OpenA2DP/releases/latest)
![License](https://img.shields.io/badge/license-GPL--3.0-blue)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)

> **Windows only.** Built directly on Win32 Bluetooth APIs, MMDevice/WASAPI, and D3D11. Will not run under WINE and is not a candidate for cross-platform porting. On Linux, use BlueZ + PipeWire.

## Download

Grab the latest [release zip](https://github.com/birdybro/OpenA2DP/releases/latest) — contains `OpenA2DP.exe` (GUI) and `OpenA2DP-cli.exe` (headless CLI). No installer, fully portable, no VC++ Redistributable required.

Bleeding-edge builds: [Actions tab](https://github.com/birdybro/OpenA2DP/actions/workflows/build.yml) → latest run → Artifacts → `OpenA2DP-windows-x64`.

## Features

- **Reconnect / Reset** — the 99% workflow: fix a Bluetooth audio device that connected but isn't playing.
- **Audio visualizer** — live WASAPI-loopback spectrum bars in the bottom-left.
- **Auto-Heal** — optional per-device watchdog for the Windows 11 connect-but-silent bug, with toast notifications.
- **HFP Watchdog** — optional periodic Handsfree-disable so Windows can't fall back to narrowband mono SCO.
- **System tray** — right-click menu for Reconnect / Disable HFP / Switch Stack / Show / Quit. Minimizes to tray.
- **CLI mode** — `OpenA2DP-cli.exe` for Task Scheduler / scripts.
- **Approachable by default** — first launch only shows the basics. The **Advanced Mode** checkbox in the top-right unlocks everything below.

### Advanced Mode unlocks

- **Live codec settings via [Alternative A2DP Driver](https://www.bluetoothgoodies.com/)** — read+write SBC/AAC parameters in the driver's per-device registry config (codec, sample rates, channel mode, block size, allocation, subbands, AAC bitrate, ABR). Bitpool slider clamped to the device's reported max with an admin-only override.
- **A2DP stack control** — start/stop A2DP services from the Windows SCM, or one-click switch the entire active stack (Microsoft ↔ AltA2DP). Needs admin. The "Use AltA2DP" button auto-disables (with a hint pointing at bluetoothgoodies.com) if the Alternative A2DP Driver isn't installed. When both stacks happen to be loaded simultaneously — common because Windows refuses to unload the BthA2dp kernel driver at runtime — the active-stack indicator correctly reports "Alternative A2DP Driver (Microsoft also loaded)" since AltA2DP's user-mode service intercepts WASAPI before BthA2dp can route audio.
- **Status panel** — sample rate, bit depth, channels, codec bitrate, audio latency, matched WASAPI endpoint, battery, and a Device Capabilities subsection from the driver's `Capability\<addr>` registry subtree.
- **Bluetooth remote-event tracking** — logs play/pause/next/prev/volume from your headphones via three observation surfaces (low-level keyboard hook, WASAPI default-endpoint volume polling, WinRT SMTC poller).
- **Per-device manual service toggles** for AudioSink and Handsfree.
- **Connection history**, activity counters, diagnostic log with severity filters and Copy-to-Clipboard.

See [CHANGELOG.md](CHANGELOG.md) for the full per-version inventory.

## Usage

1. Pair your Bluetooth audio device in Windows Settings.
2. Run `OpenA2DP.exe`, select the device on the left.
3. Click **Reconnect** if audio is broken, or **Reset** to cycle all audio services.
4. Optionally enable Auto-Heal (Advanced Mode → Watchdogs).
5. Right-click the tray icon for quick actions without opening the window.
6. To switch A2DP stacks, run **as Administrator** and use the Use Microsoft / Use AltA2DP buttons in Advanced Mode.

### CLI

```
OpenA2DP-cli.exe --reconnect AA:BB:CC:DD:EE:FF
OpenA2DP-cli.exe --list-devices
OpenA2DP-cli.exe --switch-stack ms      # needs admin
OpenA2DP-cli.exe --help
```

Full command list: `--reconnect`, `--disable-hfp`, `--enable-a2dp`, `--list-devices`, `--list-stacks`, `--switch-stack`, `--start-service`, `--stop-service`, `--show-codec-config`, `--set-codec`, `--set-bitpool`, `--set-aac-bitrate`, `--probe-registry`. Service-control commands need admin.

## Building

Requires Visual Studio 2022+ with **Desktop development with C++** + Windows SDK + Git.

```
git clone --recurse-submodules https://github.com/birdybro/OpenA2DP.git
cd OpenA2DP
scripts\build.bat
```

Outputs `build\OpenA2DP.exe` (GUI, `/SUBSYSTEM:WINDOWS`) and `build\OpenA2DP-cli.exe` (CLI, `/SUBSYSTEM:CONSOLE`).

To cut a release: bump `OA2DP_VER_*` in both `scripts/version_*.rc`, add a CHANGELOG entry, then `git tag v0.X.Y && git push --tags`. CI builds, zips, and attaches to a new GitHub Release automatically.

Tests: `tests\build_and_test.bat`.

See [CLAUDE.md](CLAUDE.md) for architecture notes.

## Known Limitations

- **Codec detection on the Microsoft stack**: Windows doesn't expose A2DP codec negotiation parameters in user mode, so without the Alternative A2DP Driver the status panel can only show what WASAPI measures.
- **Battery**: only shown if the device exposes `DEVPKEY_Bluetooth_Battery` to Windows. Many headphones don't.
- **Service control needs admin**. Start/Stop and Switch Stack are greyed out otherwise.
- **Tested stacks**: Microsoft stock and Alternative A2DP Driver. Other third-party stacks should work but aren't tested.

## License

GPL-3.0. See [LICENSE](LICENSE).

## Credits

Application icon by [Ramy W.](https://www.flaticon.com/authors/ramy-w) on Flaticon.
