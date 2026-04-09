# OpenA2DP

A minimal Windows Bluetooth A2DP control tool. Manage your Bluetooth stereo audio devices with per-device profiles, live connection status, and diagnostic logging.

![License](https://img.shields.io/badge/license-GPL--3.0-blue)

## Features

- **Device discovery** -- Lists paired Bluetooth audio devices with live connection status
- **Per-device profiles** -- Configure preferred codec, sample rates, channel modes, and SBC parameters per device
- **Audio endpoint status** -- Shows real sample rate, bit depth, and channel count from the Windows audio stack
- **Reconnect / Reset** -- Toggle A2DP AudioSink and Handsfree services to fix connection issues
- **Manual service control** -- Enable/disable AudioSink (A2DP) and Handsfree (HFP) individually to prevent unwanted profile switching
- **Auto-Heal** -- Optional per-device watchdog that detects the Windows 11 "connected but no audio" bug and automatically cycles AudioSink to recover
- **HFP Watchdog** -- Optional per-device watchdog that periodically re-disables Handsfree (HFP) so Windows can't fall back to narrowband mono SCO
- **CLI mode** -- Headless command-line entry points (`--reconnect`, `--disable-hfp`, `--enable-a2dp`) for Task Scheduler or login scripts
- **Driver detection** -- Logs all A2DP-related services found in the Windows SCM at startup so you can see whether you're on the Microsoft stack or a third-party one
- **Diagnostic logging** -- Color-coded severity levels, filterable, with auto-scroll
- **Persistent settings** -- Profiles auto-save to `%APPDATA%\OpenA2DP\` and reload on startup

## Screenshot

*Coming soon*

## Building

### Requirements

- Windows 10/11
- Visual Studio 2022+ with **Desktop development with C++** workload
- Windows SDK

### Build

```
scripts\build.bat
```

Output: `build\OpenA2DP.exe`

### Run Tests

```
tests\build_and_test.bat
```

## Usage

1. Pair your Bluetooth audio device(s) in Windows Settings
2. Run `OpenA2DP.exe`
3. Select a device from the left panel
4. Adjust codec and SBC settings as desired (settings auto-save)
5. Use **Reconnect** to re-establish the A2DP connection, or **Reset** to cycle all audio services
6. Use the **Services** section to manually enable/disable AudioSink or Handsfree
7. Optionally enable **Auto-Heal** to automatically recover from connect-but-no-audio failures on next connect
8. Optionally enable **HFP Watchdog** to keep Handsfree disabled (recommended for headphones-only use)

### Command-line use

OpenA2DP can also run headlessly for scripting and Task Scheduler use:

```
OpenA2DP.exe --reconnect AA:BB:CC:DD:EE:FF
OpenA2DP.exe --disable-hfp AA:BB:CC:DD:EE:FF
OpenA2DP.exe --enable-a2dp AA:BB:CC:DD:EE:FF
OpenA2DP.exe --help
```

When launched from a terminal, output goes to that terminal. When launched from Task Scheduler or Explorer with no parent console, the action runs silently and the process exits.

## Architecture

```
app/        UI (Win32 + D3D11 + cimgui), window management
core/       Types, config (INI), validation, logging (ring buffer)
service/    Bluetooth device enumeration, audio status, reconnect/reset, auto-heal
include/    Shared C headers
third_party/  cimgui (Dear ImGui C bindings)
```

Written in C with minimal C++ only where required (COM APIs, ImGui backends). See [CLAUDE.md](CLAUDE.md) for detailed architecture notes.

## How It Works

- **Device scan**: Uses `BluetoothFindFirstDevice` / `BluetoothGetDeviceInfo` to enumerate paired Bluetooth audio devices, filtered by Class of Device
- **Audio status**: Queries the Windows MMDevice API to match Bluetooth devices to their audio endpoints and read the actual mix format
- **Reconnect**: Toggles the A2DP AudioSink service off then back on via `BluetoothSetServiceState`, with retry logic
- **Reset**: Cycles both Handsfree and AudioSink services, re-establishing AudioSink last so A2DP takes priority
- **Auto-Heal**: When opted in per device, watches for connect transitions and probes WASAPI for a render endpoint after a short settle. If the endpoint is missing it runs a reconnect cycle, retrying up to a hard cap. Skips when a manual action is in flight to avoid races. See [docs/driver-evaluation.md](docs/driver-evaluation.md) for the design rationale.
- **Persistence**: Per-device INI files in `%APPDATA%\OpenA2DP\`, dirty-detected and auto-saved every few seconds

## Known Limitations

- **Codec detection**: Windows does not expose A2DP codec negotiation parameters (active codec, bitpool, subbands, allocation method) in user mode. The status panel only shows fields that come from real WASAPI measurements; codec-internal fields are intentionally omitted rather than fabricated. See [docs/driver-evaluation.md](docs/driver-evaluation.md) for details.
- **Bluetooth stack dependency**: Service toggle behavior depends on the Windows Bluetooth driver stack. Some devices or drivers may not respond to `BluetoothSetServiceState` as expected.

## License

GPL-3.0. See [LICENSE](LICENSE).
