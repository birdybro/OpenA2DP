# OpenA2DP Security Analysis

Practical security review of the OpenA2DP codebase at v0.7.0. This focuses on
real-world exploitability given the threat model: a local desktop application
running under a standard Windows user account.

## Threat Model

OpenA2DP is a single-user desktop tool. Its attack surface is:

1. **Local config files** in `%APPDATA%\OpenA2DP\` (INI profiles, history)
2. **Windows registry reads** (Alt A2DP Driver parameters under HKLM)
3. **One outbound HTTPS request** (GitHub API update check)
4. **Bluetooth and audio device APIs** (system-provided, kernel-mediated)
5. **CLI arguments** (when invoked from cmd.exe / scripts)
6. **System-wide keyboard hook** (media key observation)

There is no network listener, no IPC server, no shared memory, and no file
watch on directories other processes write to. The realistic attacker is
another process on the same machine or a compromised config file.

---

## Findings

### 1. Log Ring Buffer — No Thread Synchronization

**Severity: Low (data race, no memory corruption)**
**Location:** `core/log.c:22-37`

`oa2dp_log()` is called from the UI thread and at least five background worker
threads (auto-heal, driver control, update check, device probe, audio
visualizer). It writes to a shared `g_log` struct with no lock, no atomic, and
no memory barrier:

```c
OA2DP_LogEntry *entry = &g_log.entries[g_log.head];
// ... fill entry ...
g_log.head = (g_log.head + 1) % OA2DP_LOG_RING_SIZE;
if (g_log.count < OA2DP_LOG_RING_SIZE)
    g_log.count++;
```

Two threads logging concurrently can write to the same slot (torn entry) or
increment `head`/`count` non-atomically. On x86 with natural-aligned ints the
practical effect is garbled log messages, not a crash or exploitable
corruption. A `CRITICAL_SECTION` or `InterlockedIncrement` on `head` would
eliminate it, but this is a cosmetic data race, not a security vulnerability.

### 2. Update Checker — No Certificate Pinning, No HTTP Status Check

**Severity: Low**
**Location:** `service/update_check.c:65-162`

The update check connects to `api.github.com` over HTTPS (WinHTTP validates
the server certificate against the Windows certificate store). This is fine for
the threat model, but two observations:

- **No certificate pinning.** An attacker who can install a root CA on the
  machine (e.g., corporate proxy, malware with admin rights) could MITM the
  request and inject a fake `tag_name`. The consequence is a misleading tray
  notification ("OpenA2DP v99.0.0 available") — there is no auto-download or
  auto-install, so the blast radius is a nuisance, not code execution.

- **No HTTP status code check.** The code reads the response body regardless
  of whether the server returned 200, 404, 403, or 500. A non-200 response
  would simply fail the `strstr(body, "\"tag_name\"")` check and silently
  abort. This is benign but untidy.

- **`tag` from network response used in `snprintf` as `%s` argument** (line
  143), not as the format string itself, so there is no format-string
  injection.

### 3. INI Config Parsing — Robust Against Malicious Files

**Severity: Informational (no issue found)**
**Location:** `core/config.c:210-287`

The INI parser uses `fgets` with a 1024-byte stack buffer and copies values
exclusively through `snprintf(dst, sizeof(dst), "%s", val)`. String fields
are truncated, integer fields go through `atoi()` and are then clamped by
`oa2dp_profile_validate()`. Unknown keys are silently skipped.

A crafted `.ini` file cannot cause a buffer overflow, format-string attack, or
integer overflow that survives validation. The worst case is injecting
out-of-range codec parameters that get clamped to safe defaults.

### 4. History File — 1 MB Slurp Cap, No Symlink Check

**Severity: Low**
**Location:** `core/history.c:66-105`

`prune_oldest_half()` reads the entire history file into a `malloc`'d buffer:

```c
if (size <= 0 || size > 1 * 1024 * 1024) { fclose(f); return; }
char *buf = (char *)malloc((size_t)size + 1);
```

The 1 MB cap prevents a memory-exhaustion attack via an inflated history file.
However, history files are opened by path with no symlink or junction check.
A local attacker who can create a junction at
`%APPDATA%\OpenA2DP\<addr>.history` pointing to a sensitive file could cause
OpenA2DP to read 1 MB of that file into memory — but the content is never
written anywhere except back to the same path (the prune rewrites it). In
practice this would just truncate the target file, which requires the attacker
to already have write access to `%APPDATA%`. Not exploitable in a meaningful
way.

### 5. System-Wide Keyboard Hook (WH_KEYBOARD_LL)

**Severity: Informational (by design, limited scope)**
**Location:** `service/remote_events.cpp:59-71, 81-82`

OpenA2DP installs a low-level keyboard hook on the main thread's message pump.
The callback receives every keystroke system-wide, but:

- Only media virtual keys (`VK_MEDIA_PLAY_PAUSE`, `VK_MEDIA_NEXT_TRACK`,
  `VK_VOLUME_UP`, etc.) are processed. Everything else falls through to
  `CallNextHookEx` without being logged or stored.
- No keystrokes are suppressed or modified.
- The hook is installed and removed cleanly via `SetWindowsHookExW` /
  `UnhookWindowsHookEx`.

This is standard practice for media-control applications (Spotify, Discord,
EarTrumpet all do the same). No keylogger risk. However, security-conscious
users or EDR products may flag `WH_KEYBOARD_LL` as suspicious — a brief
comment in user-facing docs would help.

### 6. Undocumented COM Interface — IPolicyConfig

**Severity: Informational (inherent risk, well-managed)**
**Location:** `service/audio_status.cpp:29-60, 457-482`

The `IPolicyConfig` COM interface is undocumented by Microsoft. The vtable
layout is manually defined. If Microsoft changes the vtable order in a future
Windows release, calling `SetDefaultEndpoint` (slot 10) would invoke a
different method, potentially corrupting audio settings.

This is a known, accepted risk shared with every audio-switcher tool on
Windows (SoundSwitch, EarTrumpet, NirCmd). The vtable has been stable since
Windows 7. The code comments are explicit about not reordering it.

### 7. Registry Writes — Bitpool Clamping

**Severity: Informational (defense in depth)**
**Location:** `service/altdriver_config.c`

`oa2dp_altdriver_write_next()` clamps the SBC bitpool to the device's
`Capability.SbcMaximumBitpool` unless the user explicitly sets an override
flag. This prevents writing a value that could damage certain Bluetooth chips.
The override is opt-in per device and requires admin elevation. Good design.

### 8. CLI Argument Handling

**Severity: Informational (no issue found)**
**Location:** `app/cli.c`

CLI arguments are dispatched through a fixed command table. Bluetooth
addresses are validated by `valid_bt_address()` (strict `XX:XX:XX:XX:XX:XX`
format check). Service-control and codec-set commands check
`oa2dp_process_is_elevated()` and refuse with a clear error when not admin.
Integer arguments use `_wtoi()` and are range-clamped. No injection vectors.

### 9. COM Apartment Threading

**Severity: Informational (correctly handled)**
**Location:** `service/audio_status.cpp:62-77`

Earlier versions cached a single `IMMDeviceEnumerator` from the main thread
and reused it from workers — undefined cross-apartment access. The current
design creates a fresh enumerator per call. Worker threads initialize their own
COM apartment via `oa2dp_audio_status_thread_init()`. The audio visualizer
thread initializes its own apartment directly. No cross-apartment violations
remain.

### 10. DLL Search Order

**Severity: Informational**

OpenA2DP links statically (`/MT`) against the CRT and does not call
`LoadLibrary` in its own code. The third-party ImGui backend calls
`LoadLibraryA("user32.dll")` and `LoadLibraryA("shcore.dll")` with bare names,
but these are KnownDLLs on Windows 10+ and are immune to search-order
hijacking. No DLL planting risk.

### 11. Config Directory Permissions

**Severity: Informational**
**Location:** `core/config.c:36`

`CreateDirectoryA(g_config_dir, NULL)` creates the config directory with a
NULL security descriptor, inheriting the parent's ACL. Under `%APPDATA%` this
means only the current user and SYSTEM have access. Appropriate for a
single-user tool. No world-writable directory risk.

### 12. PlaySound Path Construction

**Severity: Informational (no issue found)**
**Location:** `service/actions.c:323-340`

The test-sound path is built from `GetWindowsDirectoryA()` + a hardcoded
suffix (`\Media\tada.wav`). No user input. Bounds-checked with a 16-byte
margin. `SND_NODEFAULT` prevents fallback behavior. Not injectable.

---

## Summary Table

| # | Finding | Severity | Exploitable? |
|---|---------|----------|--------------|
| 1 | Log buffer data race (no lock) | Low | No — cosmetic garble only |
| 2 | Update check: no cert pin, no status check | Low | No — tray notification only |
| 3 | INI parsing | Info | No — all paths bounds-checked |
| 4 | History file symlink (theoretical) | Low | No — requires existing access |
| 5 | System-wide keyboard hook | Info | No — media keys only |
| 6 | Undocumented IPolicyConfig vtable | Info | No — stable since Win7 |
| 7 | Registry write bitpool clamp | Info | No — defense in depth present |
| 8 | CLI argument handling | Info | No — validated and elevation-gated |
| 9 | COM apartment threading | Info | No — correctly isolated |
| 10 | DLL search order | Info | No — KnownDLLs immune |
| 11 | Config directory ACL | Info | No — inherits %APPDATA% |
| 12 | PlaySound path | Info | No — hardcoded system path |

---

## Overall Assessment

**No critical or high-severity vulnerabilities found.** The codebase
demonstrates consistently good security hygiene for a C/C++ Windows desktop
application:

- Sized string functions (`snprintf`, `_snwprintf_s`) used everywhere; no
  `strcpy`, `sprintf`, `strcat`, or `gets`.
- Input validation at system boundaries (CLI, INI, registry, network) with
  post-load clamping.
- Admin-gated operations checked consistently in both GUI and CLI paths.
- COM threading handled correctly after a documented earlier fix.
- Static CRT linkage eliminates DLL dependency attacks.

The two items worth tracking are the unsynchronized log buffer (#1) — which
could be fixed with a single critical section — and the keyboard hook (#5),
which is correct but may draw EDR attention and would benefit from a brief
explanation in user documentation.
