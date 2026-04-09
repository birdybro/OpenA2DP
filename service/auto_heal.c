/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * auto_heal.c - Recover from connected-but-no-audio Windows 11 bug.
 *
 * When a paired Bluetooth audio device transitions to connected the
 * Microsoft Bluetooth stack sometimes fails to expose a WASAPI render
 * endpoint, leaving the device in a connected-but-silent state.  The
 * documented user workaround is to disable then re-enable the AudioSink
 * service, which is exactly what oa2dp_action_reconnect already does.
 *
 * This module watches for the bug and runs that workaround
 * automatically, with a small retry budget so the loop can't run away.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "oa2dp_auto_heal.h"
#include "oa2dp_actions.h"
#include "oa2dp_audio_status.h"
#include "oa2dp_log.h"
#include "oa2dp_stats.h"
#include "oa2dp_tray.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── tunables ───────────────────────────────────────────────────────── */

#define HEAL_INITIAL_SETTLE_MS 2000   /* let stack create endpoint naturally */
#define HEAL_RETRY_GAP_MS      3000   /* between heal attempts */
#define HEAL_MAX_ATTEMPTS      3      /* hard cap */

/* ── single-slot busy flag ──────────────────────────────────────────── */

static volatile LONG g_heal_busy = 0;

int oa2dp_auto_heal_busy(void)
{
    return (int)g_heal_busy;
}

/* ── worker ─────────────────────────────────────────────────────────── */

typedef struct {
    char device_id[256];
    char display_name[128];
} HealParam;

/*
 * Probe the audio endpoint for this device.  Returns 1 if a WASAPI
 * render endpoint exists, 0 otherwise.  Uses a throwaway status struct
 * so we don't trample the live UI status.
 */
static int has_endpoint(const char *device_id, const char *display_name)
{
    OA2DP_DeviceStatus probe;
    memset(&probe, 0, sizeof(probe));
    return (oa2dp_audio_status_query(device_id, display_name, &probe) == 0)
               ? 1 : 0;
}

static DWORD WINAPI heal_thread(LPVOID param)
{
    HealParam *p = (HealParam *)param;

    oa2dp_log(OA2DP_LOG_INFO, "auto-heal: starting check for %s", p->device_id);
    oa2dp_stats_inc_heal_trigger();

    /* Give Windows a chance to bring up the endpoint on its own. */
    Sleep(HEAL_INITIAL_SETTLE_MS);

    int healed = 0;
    for (int attempt = 1; attempt <= HEAL_MAX_ATTEMPTS; attempt++) {
        if (has_endpoint(p->device_id, p->display_name)) {
            if (attempt == 1) {
                oa2dp_log(OA2DP_LOG_INFO,
                          "auto-heal: endpoint present for %s, no action needed",
                          p->device_id);
            } else {
                oa2dp_log(OA2DP_LOG_INFO,
                          "auto-heal: endpoint recovered for %s after %d attempt(s)",
                          p->device_id, attempt - 1);
                /* Real recovery — the bug fired and we fixed it.
                 * Healthy connects (attempt == 1) don't notify since
                 * they'd be too noisy. */
                oa2dp_stats_inc_heal_recovery();
                char body[256];
                snprintf(body, sizeof(body),
                         "Recovered audio endpoint for %s after %d attempt(s).",
                         p->display_name[0] ? p->display_name : p->device_id,
                         attempt - 1);
                oa2dp_tray_notify("OpenA2DP auto-heal", body);
            }
            healed = 1;
            break;
        }

        /* If the user kicked off a manual action, stand down for this
         * attempt — they're already dealing with the device. */
        if (oa2dp_action_busy()) {
            oa2dp_log(OA2DP_LOG_INFO,
                      "auto-heal: %s manual action in progress, skipping attempt %d/%d",
                      p->device_id, attempt, HEAL_MAX_ATTEMPTS);
            Sleep(HEAL_RETRY_GAP_MS);
            continue;
        }

        oa2dp_log(OA2DP_LOG_WARN,
                  "auto-heal: %s connected but no audio endpoint (attempt %d/%d), reconnecting",
                  p->device_id, attempt, HEAL_MAX_ATTEMPTS);

        /* Synchronous reconnect — we're already on a background thread.
         * Note: this bypasses the actions g_busy slot, so we explicitly
         * check it above to avoid racing a manual button click. */
        oa2dp_action_reconnect(p->device_id);

        /* Let the endpoint materialize before re-checking. */
        Sleep(HEAL_RETRY_GAP_MS);
    }

    if (!healed) {
        oa2dp_log(OA2DP_LOG_ERROR,
                  "auto-heal: gave up on %s after %d attempt(s) — endpoint never appeared",
                  p->device_id, HEAL_MAX_ATTEMPTS);
        oa2dp_stats_inc_heal_failure();
        char body[256];
        snprintf(body, sizeof(body),
                 "Could not recover audio for %s after %d attempt(s). "
                 "Try Reconnect manually.",
                 p->display_name[0] ? p->display_name : p->device_id,
                 HEAL_MAX_ATTEMPTS);
        oa2dp_tray_notify("OpenA2DP auto-heal failed", body);
    }

    free(p);
    InterlockedExchange(&g_heal_busy, 0);
    return 0;
}

/* ── public API ─────────────────────────────────────────────────────── */

int oa2dp_auto_heal_trigger(const char *device_id, const char *display_name)
{
    if (!device_id || !device_id[0])
        return -1;

    if (InterlockedCompareExchange(&g_heal_busy, 1, 0) != 0) {
        oa2dp_log(OA2DP_LOG_DEBUG,
                  "auto-heal: skipped for %s (already running)", device_id);
        return -1;
    }

    HealParam *p = (HealParam *)malloc(sizeof(*p));
    if (!p) {
        InterlockedExchange(&g_heal_busy, 0);
        return -1;
    }
    snprintf(p->device_id, sizeof(p->device_id), "%s", device_id);
    snprintf(p->display_name, sizeof(p->display_name), "%s",
             display_name ? display_name : "");

    HANDLE h = CreateThread(NULL, 0, heal_thread, p, 0, NULL);
    if (!h) {
        oa2dp_log(OA2DP_LOG_ERROR,
                  "auto-heal: CreateThread failed (err=%lu)", GetLastError());
        free(p);
        InterlockedExchange(&g_heal_busy, 0);
        return -1;
    }
    CloseHandle(h);

    oa2dp_log(OA2DP_LOG_INFO, "auto-heal: queued check for %s", device_id);
    return 0;
}
