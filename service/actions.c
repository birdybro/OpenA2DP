/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * actions.c - Reconnect / reset via Windows Bluetooth APIs
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bluetoothapis.h>
#include <bthdef.h>
#include <initguid.h>
#include <mmsystem.h>

#include "oa2dp_actions.h"
#include "oa2dp_log.h"
#include "oa2dp_stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Bluetooth service GUIDs ────────────────────────────────────────── */

/* A2DP Audio Sink: {0000110B-0000-1000-8000-00805F9B34FB} */
DEFINE_GUID(GUID_AudioSink,
    0x0000110B, 0x0000, 0x1000,
    0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB);

/* A2DP Audio Source: {0000110A-0000-1000-8000-00805F9B34FB} */
DEFINE_GUID(GUID_AudioSource,
    0x0000110A, 0x0000, 0x1000,
    0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB);

/* Handsfree: {0000111E-0000-1000-8000-00805F9B34FB} */
DEFINE_GUID(GUID_Handsfree,
    0x0000111E, 0x0000, 0x1000,
    0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB);

/* ── helpers ────────────────────────────────────────────────────────── */

static int parse_bt_address(const char *device_id, BLUETOOTH_ADDRESS *addr)
{
    unsigned int b[6];
    if (sscanf(device_id, "%02X:%02X:%02X:%02X:%02X:%02X",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
        return -1;
    addr->rgBytes[5] = (BYTE)b[0];
    addr->rgBytes[4] = (BYTE)b[1];
    addr->rgBytes[3] = (BYTE)b[2];
    addr->rgBytes[2] = (BYTE)b[3];
    addr->rgBytes[1] = (BYTE)b[4];
    addr->rgBytes[0] = (BYTE)b[5];
    return 0;
}

static int get_device_info(const char *device_id, BLUETOOTH_DEVICE_INFO *info)
{
    memset(info, 0, sizeof(*info));
    info->dwSize = sizeof(*info);

    if (parse_bt_address(device_id, &info->Address) != 0) {
        oa2dp_log(OA2DP_LOG_ERROR, "action: invalid device ID '%s'", device_id);
        return -1;
    }

    DWORD result = BluetoothGetDeviceInfo(NULL, info);
    if (result != ERROR_SUCCESS) {
        oa2dp_log(OA2DP_LOG_ERROR, "action: BluetoothGetDeviceInfo failed (err=%lu)",
                  result);
        return -1;
    }
    return 0;
}

#define TOGGLE_RETRY_COUNT    3
#define TOGGLE_RETRY_DELAY_MS 1000
#define TOGGLE_GAP_MS         1500

/*
 * Toggle a Bluetooth service off then back on.
 * Treats ERROR_NOT_FOUND / ERROR_SERVICE_DOES_NOT_EXIST on disable as
 * "already off" and still attempts the enable.  Retries the enable if
 * it fails, since the stack sometimes needs more time after a disable.
 * Returns 0 on success, -1 if enable ultimately fails.
 */
static int toggle_service(BLUETOOTH_DEVICE_INFO *info, const GUID *service,
                          const char *service_name, const char *device_name)
{
    DWORD result;

    /* ── Disable ────────────────────────────────────────────────── */
    oa2dp_log(OA2DP_LOG_INFO, "action: disabling %s on '%s'",
              service_name, device_name);

    result = BluetoothSetServiceState(NULL, info, service,
                                      BLUETOOTH_SERVICE_DISABLE);
    if (result == ERROR_SUCCESS) {
        oa2dp_log(OA2DP_LOG_DEBUG, "action: %s disabled", service_name);
    } else if (result == ERROR_SERVICE_DOES_NOT_EXIST ||
               result == ERROR_NOT_FOUND) {
        /* Service wasn't active — still try to enable it. */
        oa2dp_log(OA2DP_LOG_DEBUG,
                  "action: %s not active (err=%lu), will try enable anyway",
                  service_name, result);
    } else {
        oa2dp_log(OA2DP_LOG_WARN, "action: disable %s returned err=%lu, trying enable anyway",
                  service_name, result);
    }

    /* Let the Bluetooth stack settle before re-enabling. */
    Sleep(TOGGLE_GAP_MS);

    /* ── Enable (with retry) ────────────────────────────────────── */
    for (int attempt = 1; attempt <= TOGGLE_RETRY_COUNT; attempt++) {
        oa2dp_log(OA2DP_LOG_INFO, "action: enabling %s on '%s' (attempt %d/%d)",
                  service_name, device_name, attempt, TOGGLE_RETRY_COUNT);

        result = BluetoothSetServiceState(NULL, info, service,
                                          BLUETOOTH_SERVICE_ENABLE);
        if (result == ERROR_SUCCESS) {
            oa2dp_log(OA2DP_LOG_INFO, "action: %s enabled", service_name);
            return 0;
        }

        oa2dp_log(OA2DP_LOG_WARN, "action: enable %s attempt %d failed (err=%lu)",
                  service_name, attempt, result);

        if (attempt < TOGGLE_RETRY_COUNT)
            Sleep(TOGGLE_RETRY_DELAY_MS);
    }

    oa2dp_log(OA2DP_LOG_ERROR,
              "action: enable %s failed after %d attempts on '%s'",
              service_name, TOGGLE_RETRY_COUNT, device_name);
    return -1;
}

/* ── public API ─────────────────────────────────────────────────────── */

int oa2dp_action_reconnect(const char *device_id)
{
    BLUETOOTH_DEVICE_INFO info;
    if (get_device_info(device_id, &info) != 0)
        return -1;

    char name[256];
    WideCharToMultiByte(CP_UTF8, 0, info.szName, -1,
                        name, sizeof(name), NULL, NULL);

    oa2dp_log(OA2DP_LOG_INFO, "action: reconnecting '%s'", name);

    int rc = toggle_service(&info, &GUID_AudioSink, "AudioSink", name);

    if (rc == 0) {
        oa2dp_log(OA2DP_LOG_INFO, "action: reconnect completed for '%s'", name);
        oa2dp_stats_inc_reconnect();
    } else {
        oa2dp_log(OA2DP_LOG_ERROR, "action: reconnect failed for '%s'", name);
    }

    return rc;
}

int oa2dp_action_reset(const char *device_id)
{
    BLUETOOTH_DEVICE_INFO info;
    if (get_device_info(device_id, &info) != 0)
        return -1;

    char name[256];
    WideCharToMultiByte(CP_UTF8, 0, info.szName, -1,
                        name, sizeof(name), NULL, NULL);

    oa2dp_log(OA2DP_LOG_INFO, "action: resetting '%s' (cycling all audio services)",
              name);

    int failures = 0;

    /* Cycle Handsfree (HFP) first. */
    if (toggle_service(&info, &GUID_Handsfree, "Handsfree", name) != 0)
        failures++;

    /* Cycle AudioSink (A2DP playback) last so it's the final active profile. */
    if (toggle_service(&info, &GUID_AudioSink, "AudioSink", name) != 0)
        failures++;

    if (failures == 0)
        oa2dp_log(OA2DP_LOG_INFO, "action: reset completed for '%s'", name);
    else
        oa2dp_log(OA2DP_LOG_WARN, "action: reset partially failed for '%s' (%d service(s))",
                  name, failures);

    return (failures > 0) ? -1 : 0;
}

/* ── single-service enable/disable ──────────────────────────────────── */

static int set_service(const char *device_id, const GUID *service,
                       const char *service_name, int enable)
{
    BLUETOOTH_DEVICE_INFO info;
    if (get_device_info(device_id, &info) != 0)
        return -1;

    char name[256];
    WideCharToMultiByte(CP_UTF8, 0, info.szName, -1,
                        name, sizeof(name), NULL, NULL);

    DWORD flag = enable ? BLUETOOTH_SERVICE_ENABLE : BLUETOOTH_SERVICE_DISABLE;
    const char *verb = enable ? "enabling" : "disabling";

    oa2dp_log(OA2DP_LOG_INFO, "action: %s %s on '%s'", verb, service_name, name);

    DWORD result = BluetoothSetServiceState(NULL, &info, service, flag);
    if (result != ERROR_SUCCESS &&
        !(result == ERROR_NOT_FOUND && !enable) &&
        !(result == ERROR_SERVICE_DOES_NOT_EXIST && !enable)) {
        oa2dp_log(OA2DP_LOG_ERROR, "action: %s %s failed (err=%lu)",
                  verb, service_name, result);
        return -1;
    }

    oa2dp_log(OA2DP_LOG_INFO, "action: %s %s on '%s'",
              enable ? "enabled" : "disabled", service_name, name);
    return 0;
}

/* ── async wrappers ─────────────────────────────────────────────────── */

static volatile LONG g_busy = 0;

enum { ACT_RECONNECT, ACT_RESET, ACT_SET_AUDIOSINK, ACT_SET_HANDSFREE };

typedef struct {
    char device_id[256];
    int  action;
    int  enable;     /* for ACT_SET_* */
} ActionThreadParam;

static DWORD WINAPI action_thread(LPVOID param)
{
    ActionThreadParam *p = (ActionThreadParam *)param;

    switch (p->action) {
    case ACT_RECONNECT:      oa2dp_action_reconnect(p->device_id); break;
    case ACT_RESET:          oa2dp_action_reset(p->device_id);     break;
    case ACT_SET_AUDIOSINK:  set_service(p->device_id, &GUID_AudioSink,
                                         "AudioSink", p->enable);  break;
    case ACT_SET_HANDSFREE:  set_service(p->device_id, &GUID_Handsfree,
                                         "Handsfree", p->enable);  break;
    }

    free(p);
    InterlockedExchange(&g_busy, 0);
    return 0;
}

static int launch_async(const char *device_id, int action, int enable)
{
    if (InterlockedCompareExchange(&g_busy, 1, 0) != 0) {
        oa2dp_log(OA2DP_LOG_WARN, "action: another action is already running");
        return -1;
    }

    ActionThreadParam *p = (ActionThreadParam *)malloc(sizeof(*p));
    if (!p) {
        InterlockedExchange(&g_busy, 0);
        return -1;
    }
    snprintf(p->device_id, sizeof(p->device_id), "%s", device_id);
    p->action = action;
    p->enable = enable;

    HANDLE h = CreateThread(NULL, 0, action_thread, p, 0, NULL);
    if (!h) {
        oa2dp_log(OA2DP_LOG_ERROR, "action: CreateThread failed (err=%lu)",
                  GetLastError());
        free(p);
        InterlockedExchange(&g_busy, 0);
        return -1;
    }
    CloseHandle(h);
    return 0;
}

int oa2dp_action_busy(void)
{
    return (int)g_busy;
}

int oa2dp_action_reconnect_async(const char *device_id)
{
    return launch_async(device_id, ACT_RECONNECT, 0);
}

int oa2dp_action_reset_async(const char *device_id)
{
    return launch_async(device_id, ACT_RESET, 0);
}

int oa2dp_action_set_audiosink_async(const char *device_id, int enable)
{
    return launch_async(device_id, ACT_SET_AUDIOSINK, enable);
}

int oa2dp_action_set_handsfree_async(const char *device_id, int enable)
{
    return launch_async(device_id, ACT_SET_HANDSFREE, enable);
}

/* ── Test sound ─────────────────────────────────────────────────────
 *
 * Plays %WINDIR%\Media\tada.wav through the default audio endpoint
 * via PlaySound's SND_FILENAME mode.  ASYNC so the call returns
 * immediately, NODEFAULT so PlaySound doesn't fall back to the
 * generic system beep if the file is missing for some reason.
 * tada.wav has shipped with every Windows release since 95 and is
 * still present on Win11 for backward compat.
 */
void oa2dp_action_play_test_sound(void)
{
    char path[MAX_PATH];
    UINT n = GetWindowsDirectoryA(path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 16) {
        oa2dp_log(OA2DP_LOG_WARN, "test sound: GetWindowsDirectoryA failed");
        return;
    }
    snprintf(path + n, MAX_PATH - n, "\\Media\\tada.wav");

    if (!PlaySoundA(path, NULL,
                    SND_FILENAME | SND_ASYNC | SND_NODEFAULT)) {
        oa2dp_log(OA2DP_LOG_WARN,
                  "test sound: PlaySound('%s') failed", path);
        return;
    }
    oa2dp_log(OA2DP_LOG_INFO, "test sound: playing '%s'", path);
}
