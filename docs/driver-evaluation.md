# Driver Evaluation (v0.1, step 10)

This document records what OpenA2DP can and cannot do from user-mode on Windows,
and decides whether a kernel-mode driver (KMDF) is warranted for v0.2.

## Motivation

The Microsoft Bluetooth stack and the third-party Alternative A2DP Driver
(bluetoothgoodies.com) both have rough edges in real use:

- The Microsoft stack will silently route audio through HFP (Hands-Free Profile)
  instead of A2DP, dropping quality to narrowband mono SCO. Once it gets "stuck"
  in HFP, there is no first-class UI to force it back to A2DP — users have to
  toggle the device, disable Hands-free Telephony in Control Panel, or unpair
  and re-pair.
- The Alternative A2DP Driver fixes some Microsoft-stack connection bugs and
  exposes codec selection (aptX, etc.), but it occasionally exhibits the same
  HFP-stuck behavior on its own.
- Neither stack exposes a stable API for inspecting or controlling the
  AVDTP-negotiated codec, bitpool, or capabilities at runtime.

OpenA2DP exists to give the user direct, scriptable control over which Bluetooth
profiles are active per device, so HFP can be kept off when it isn't wanted and
A2DP can be cycled without GUI gymnastics.

## What user-mode CAN do (and OpenA2DP already does)

All of the following are implemented in `service/` using public Win32 APIs:

| Capability | API | Where |
|---|---|---|
| Enumerate paired Bluetooth devices | `BluetoothFindFirstDevice` / `BluetoothFindNextDevice` | `service/device_enum.c:89` |
| Filter to audio-class devices | `GET_COD_MAJOR` / `COD_SERVICE_AUDIO` | `service/device_enum.c:39` |
| Get connection state | `BluetoothGetDeviceInfo` (`fConnected`) | `service/device_enum.c:217` |
| Receive arrival/removal notifications | `RegisterDeviceNotificationW` on `BTHPORT` interface | `service/device_enum.c:260` |
| Enable/disable A2DP Sink per device | `BluetoothSetServiceState(GUID_AudioSink, ENABLE/DISABLE)` | `service/actions.c:96` |
| Enable/disable Handsfree (HFP) per device | `BluetoothSetServiceState(GUID_Handsfree, ENABLE/DISABLE)` | `service/actions.c:213` |
| "Reconnect" by cycling AudioSink | toggle disable→enable with retry | `service/actions.c:153` |
| "Reset" by cycling HFP then A2DP | toggle both, A2DP last | `service/actions.c:179` |
| Query the active audio endpoint format | MMDevice + `IAudioClient::GetMixFormat` (sample rate, bit depth, channels) | `service/audio_status.cpp` |
| Persist per-device profile | INI in `%APPDATA%\OpenA2DP\` | `core/config.c` |

This is enough to address the **main pain point**: HFP getting stuck on. The
user can disable Handsfree on a device and the OS will stop routing calls /
narrowband audio through it, forcing playback to stay on A2DP. The setting
persists across reconnects via `BluetoothSetServiceState`, which writes the
service-enable bits into the OS's per-device record.

## What user-mode CANNOT do

These are the hard limits of the public Win32 surface. Some are honestly
reflected in the code as TODOs or hard-coded defaults; others are simply not
exposed by the OS at all.

### Not exposed by any public API

- **AVDTP codec negotiation inspection.** There is no public way to ask the
  Windows Bluetooth stack "what codec did you actually negotiate with this
  endpoint?" The stack negotiates SBC/aptX/AAC/LDAC internally and does not
  surface the result. `device_enum.c:157-162` and `:231-236` currently
  hard-code `SBC / joint stereo / 8 subbands / bitpool 53` whenever a device is
  connected — those values are placeholders, not measurements.
- **Bitpool / block size / allocation method / subbands.** Same story — these
  are AVDTP-internal and never reach user-mode.
- **Forcing a specific A2DP codec.** The Microsoft stack picks the codec based
  on its built-in priority list. There is no `BluetoothSetCodec` equivalent.
  The Alternative A2DP Driver exposes this through its own control panel, but
  via a private interface, not a documented API.
- **Real-time A2DP stream stats** (actual bitrate, dropped frames, retransmits).
  The current "estimated bitrate" in `device_enum.c:170` is computed from
  sample-rate × bit-depth × channels, not measured.
- **AVRCP transport state** beyond what the system media-control APIs already
  expose to UWP apps.

### Awkward but technically possible from user-mode

- **Per-radio service-class advertisement.** `BluetoothEnableIncomingConnections`
  and friends exist but operate at the radio level, not per device.
- **Pair/unpair flow.** `BluetoothAuthenticateDeviceEx` / `BluetoothRemoveDevice`
  work but the UX is intrusive (system dialogs). Out of scope for v0.1.
- **HFP-blocking that survives reboot.** `BluetoothSetServiceState` is
  persistent for the device, but a Windows update or driver reinstall can reset
  it. A polling/enforcing background mode would help but is not a driver
  problem.

## Would a KMDF driver help?

A kernel-mode driver could in principle:

1. **Filter the Bluetooth profile driver stack** (BthA2dp / BthHfEnum) and
   block HFP service binding before it ever attaches.
2. **Intercept AVDTP signalling** to read or override the negotiated codec
   parameters.
3. **Replace the audio endpoint** with a custom one that enforces a specific
   sample format.

In practice, every one of those is a large project with serious downsides:

- **Filter drivers on the Bluetooth stack are fragile.** The Microsoft stack
  is closed and changes between Windows feature updates. The Alternative A2DP
  Driver is itself a stack replacement and breaks periodically for exactly
  this reason.
- **Driver signing.** A KMDF driver needs an EV cert and attestation signing
  (or test-mode), which is a non-trivial barrier for a GPL hobby project.
- **AVDTP interception duplicates what Alternative A2DP Driver already does.**
  The user is already running that driver to get aptX. Shipping a competing
  stack filter would conflict with it.
- **The 80% pain point is solved without a driver.** Disabling the Handsfree
  service via `BluetoothSetServiceState` is the documented, supported way to
  prevent HFP from being selected. That is exactly what OpenA2DP exposes today
  via `oa2dp_action_set_handsfree_async` (`service/actions.c:305`).

## Decision

**No driver for v0.2.** The KMDF route is high-cost, high-maintenance, and
duplicates functionality the user is already getting from a third-party stack
replacement. OpenA2DP's value is being a focused, scriptable control surface
on top of whichever stack the user has installed (Microsoft or Alternative
A2DP Driver), not a competing stack.

## What to do instead in v0.2

Concrete improvements that stay in user-mode and directly target the
"HFP-stuck" failure mode:

1. **HFP-watchdog mode.** Optional per-device setting: when enabled, OpenA2DP
   periodically checks that the Handsfree service is disabled and re-disables
   it if Windows or another app turned it back on. Pure
   `BluetoothSetServiceState` polling, no driver.
2. **Honest status panel.** Stop hard-coding SBC / bitpool 53 in
   `device_enum.c:157`. Either remove those fields from the live status (only
   show what we can actually measure: sample rate, bit depth, channels,
   connection state) or label them clearly as "profile defaults, not measured."
3. **Auto-reconnect on stale endpoint.** If WASAPI reports the endpoint
   disappeared but `fConnected` is still true, run a reconnect cycle
   automatically.
4. **Detect Alternative A2DP Driver.** If its service / device interface is
   present, surface that in the status panel so the user knows which stack is
   handling each device. (Read-only — do not try to control it.)
5. **CLI / scripting entry point.** A small `OpenA2DP.exe --disable-hfp <addr>`
   command-line mode would let the user wire HFP-blocking into Task Scheduler
   or a login script, which is more reliable than relying on the GUI being
   open.

Items 1, 2, and 5 directly address the original motivation. Items 3 and 4 are
quality-of-life.

## Summary

| Question | Answer |
|---|---|
| Can user-mode disable HFP per device? | **Yes** — `BluetoothSetServiceState` |
| Can user-mode read the negotiated A2DP codec? | **No** — not exposed by any public API |
| Can user-mode set the A2DP codec? | **No** — stack-internal |
| Does OpenA2DP need a driver to solve the "HFP-stuck" problem? | **No** |
| Is a driver justified for v0.2? | **No** — defer indefinitely; revisit only if a concrete capability gap appears that user-mode cannot reach |
