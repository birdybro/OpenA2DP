# OpenA2DP Agent Guide

## Goal
Build **OpenA2DP**, a minimal Windows Bluetooth A2DP control tool.

Use it to:
- show Bluetooth A2DP devices
- save per-device settings
- show current audio status
- reconnect/reset a device
- prefer stability, especially for SBC-focused setups

## Core rules
- Use **C** for first-party code.
- Use **cimgui** for the UI.
- Use **Win32 + D3D11**.
- Keep the design small and flat.
- Use plain structs and functions.
- Avoid heavy abstractions.
- Start in **user mode first**.
- Only add a driver after proving user mode is not enough.

## Stack
- App: C, Win32, D3D11, cimgui
- Core: C
- Service: C
- Driver: KMDF in C only if needed later
- Build: Visual Studio, Windows SDK, WDK, MSBuild

## Linux reference rule
Use Linux Bluetooth audio behavior only as a **concept reference**.

Good references:
- codec policy
- reconnect/profile switching ideas
- per-device quirks
- sample-rate/channel defaults
- diagnostic visibility

Do not port Linux code directly.
Do not copy Linux architecture into Windows.

## Version 0.1 scope
Build a working user-mode prototype that can:
- list Bluetooth stereo audio devices
- select a device
- save per-device profiles
- show current status where detectable
- reconnect/reset the device
- show a live log

## UI target
Keep the UI like a small control panel.

### Left panel
- device list

### Main panel
- codec type
- mono/stereo allowed
- allowed sample rates
- stereo mode
- block size
- allocation method
- subbands
- bitpool override
- bitpool slider
- adaptive bitpool reduction
- reconnect/reset button

### Status panel
- connected state
- active codec
- sample rate
- bit depth if detectable
- channels
- stereo mode
- block size if applicable
- allocation method if applicable
- subbands if applicable
- bitpool if applicable
- estimated bitrate if available

## Repo layout
```text
OpenA2DP/
  app/
  core/
  service/
  driver/
  include/
  third_party/
  docs/
  scripts/
  tests/
```

## Module roles
### app/
UI, windowing, rendering, panels, log view.

### core/
Enums, structs, config, validation, quirks, serialization.

### service/
Device enumeration, notifications, runtime status, reconnect/reset.

### driver/
Optional. Only after proving need.

## Data model style
Use plain C structs.

```c
typedef enum OA2DP_CODEC_TYPE {
    OA2DP_CODEC_UNKNOWN = 0,
    OA2DP_CODEC_SBC,
    OA2DP_CODEC_AAC
} OA2DP_CODEC_TYPE;

typedef struct OA2DP_DeviceProfile {
    char device_id[256];
    char display_name[128];
    OA2DP_CODEC_TYPE preferred_codec;
    int allow_mono;
    int allow_stereo;
    int allow_16khz;
    int allow_32khz;
    int allow_44_1khz;
    int allow_48khz;
    int stereo_mode;
    int block_size;
    int allocation_method;
    int subbands;
    int override_bitpool;
    int bitpool;
    int auto_reduce_bitpool;
} OA2DP_DeviceProfile;
```

## Config rules
- one profile per device
- human-readable config
- safe defaults
- reject or clamp invalid values
- unknown fields must not crash loading

## Logging rules
Always log:
- startup/shutdown
- device changes
- profile load/save
- reconnect/reset actions
- API failures

Use:
- timestamps
- severity levels
- in-memory ring buffer
- optional file logging later

## Driver rule
Before writing a driver, first prove:
- what can be done in user mode
- what status can actually be read
- what settings can actually be influenced
- what gap remains

If a driver is needed:
- keep it small
- keep it in C
- use KMDF
- expose a narrow interface to the service

## Build rules
Set up:
- x64 first
- Debug and Release
- warnings enabled
- pinned third-party dependencies
- reproducible local build steps

## Implementation order
1. repo skeleton
2. core structs and config
3. Win32 + D3D11 + cimgui shell
4. static mock UI
5. device enumeration
6. status panel
7. logging panel
8. reconnect/reset action
9. persistence
10. evaluate driver need

## Avoid
- no Electron
- no web UI
- no plugin system
- no speculative kernel work
- no large framework design

## Success for v0.1
OpenA2DP v0.1 succeeds if it can:
- launch reliably
- list Bluetooth stereo devices
- save a device profile
- show useful status
- reconnect/reset a device
- log useful diagnostics
- stay small and mostly C-based

