/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * remote_events.cpp - Observe Bluetooth remote-control events
 * (tap, double-tap, volume swipe, etc.) and emit them into the
 * standard log buffer for debugging.
 *
 * Two surfaces (see header for the "why" of each):
 *   1. WH_KEYBOARD_LL hook for media virtual keys.
 *   2. Polled IAudioEndpointVolume on the default render endpoint.
 *
 * Both run on the main thread.  The keyboard hook callback is
 * invoked from the main thread's message pump, so calling
 * oa2dp_log from inside it is safe (no cross-thread races against
 * the log ring buffer).
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

#include "oa2dp_remote_events.h"
#include "oa2dp_log.h"

#include <math.h>

/* ── module state ───────────────────────────────────────────────────── */

static HHOOK g_kbd_hook = NULL;

/* Volume tracking — initialised on first poll, then incrementally
 * compared to detect deltas.  -1.0f sentinel = not yet sampled. */
static float g_last_volume = -1.0f;
static int   g_last_mute   = -1;

/* ── keyboard hook ──────────────────────────────────────────────────── */

static const char *vk_media_name(DWORD vk)
{
    switch (vk) {
    case VK_MEDIA_PLAY_PAUSE: return "play/pause";
    case VK_MEDIA_STOP:       return "stop";
    case VK_MEDIA_NEXT_TRACK: return "next track";
    case VK_MEDIA_PREV_TRACK: return "prev track";
    case VK_VOLUME_UP:        return "volume up";
    case VK_VOLUME_DOWN:      return "volume down";
    case VK_VOLUME_MUTE:      return "volume mute";
    /* Some headsets emit launch / browser keys for extra buttons. */
    case VK_LAUNCH_MEDIA_SELECT: return "launch media";
    default:                  return NULL;
    }
}

static LRESULT CALLBACK low_level_kbd_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION &&
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        const KBDLLHOOKSTRUCT *kb = (const KBDLLHOOKSTRUCT *)lParam;
        const char *name = vk_media_name(kb->vkCode);
        if (name) {
            oa2dp_log(OA2DP_LOG_INFO, "remote: media key '%s' (vk=0x%02x)",
                      name, (unsigned)kb->vkCode);
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

/* ── init / shutdown ────────────────────────────────────────────────── */

extern "C" int oa2dp_remote_events_init(void *hwnd_void)
{
    (void)hwnd_void;

    /* Low-level keyboard hook is system-wide; runs on the installing
     * thread's message pump (which is the main UI thread for us). */
    g_kbd_hook = SetWindowsHookExW(
        WH_KEYBOARD_LL, low_level_kbd_proc, GetModuleHandleW(NULL), 0);

    if (!g_kbd_hook) {
        oa2dp_log(OA2DP_LOG_WARN,
                  "remote events: SetWindowsHookEx failed (err=%lu) — "
                  "media-key events won't be tracked",
                  GetLastError());
        /* Don't fail init — volume polling still works without the hook. */
    } else {
        oa2dp_log(OA2DP_LOG_INFO, "remote events: keyboard hook installed");
    }
    return 0;
}

extern "C" void oa2dp_remote_events_shutdown(void)
{
    if (g_kbd_hook) {
        UnhookWindowsHookEx(g_kbd_hook);
        g_kbd_hook = NULL;
        oa2dp_log(OA2DP_LOG_DEBUG, "remote events: keyboard hook removed");
    }
}

/* ── volume polling ─────────────────────────────────────────────────── */

extern "C" void oa2dp_remote_events_poll(void)
{
    /* Re-acquire the default render endpoint each call so default-
     * device changes (user plugs in headphones, BT disconnects, etc.)
     * are handled naturally without explicit notification subscription. */
    IMMDeviceEnumerator *enumerator = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void **)&enumerator);
    if (FAILED(hr) || !enumerator)
        return;

    IMMDevice *device = nullptr;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    enumerator->Release();
    if (FAILED(hr) || !device)
        return;

    IAudioEndpointVolume *volctl = nullptr;
    hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL,
                          nullptr, (void **)&volctl);
    if (FAILED(hr) || !volctl) {
        device->Release();
        return;
    }

    float level = 0.0f;
    BOOL  mute  = FALSE;
    volctl->GetMasterVolumeLevelScalar(&level);
    volctl->GetMute(&mute);
    volctl->Release();
    device->Release();

    /* First call: just record the baseline, don't log. */
    if (g_last_volume < 0.0f) {
        g_last_volume = level;
        g_last_mute   = mute ? 1 : 0;
        return;
    }

    /* Detect volume change.  Use a small epsilon (~0.5%) so float
     * round-trip drift doesn't generate spurious "changes". */
    if (fabsf(level - g_last_volume) > 0.005f) {
        int pct_old = (int)(g_last_volume * 100.0f + 0.5f);
        int pct_new = (int)(level         * 100.0f + 0.5f);
        const char *dir = (level > g_last_volume) ? "up" : "down";
        oa2dp_log(OA2DP_LOG_INFO,
                  "remote: volume %s -> %d%% (was %d%%)",
                  dir, pct_new, pct_old);
        g_last_volume = level;
    }

    /* Detect mute toggle. */
    int new_mute = mute ? 1 : 0;
    if (new_mute != g_last_mute) {
        oa2dp_log(OA2DP_LOG_INFO,
                  "remote: %s", new_mute ? "muted" : "unmuted");
        g_last_mute = new_mute;
    }
}
