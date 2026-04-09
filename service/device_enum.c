/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * device_enum.c - Bluetooth device enumeration via Windows Bluetooth APIs
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bluetoothapis.h>
#include <bthdef.h>
#include <dbt.h>
#include <initguid.h>

#include "oa2dp_device.h"
#include "oa2dp_config.h"
#include "oa2dp_audio_status.h"
#include "oa2dp_auto_heal.h"
#include "oa2dp_log.h"

#include <stdio.h>
#include <string.h>

/* Device interface GUID for Bluetooth port (for RegisterDeviceNotification). */
DEFINE_GUID(OA2DP_GUID_BTHPORT,
    0x850302a, 0xb344, 0x4fda,
    0x9b, 0xe9, 0x90, 0x57, 0x6b, 0x8d, 0x46, 0xf0);

/* A2DP Audio Sink and Handsfree service GUIDs (also defined in actions.c —
 * keeping a local copy here so device_enum doesn't depend on actions.c). */
static const GUID GUID_OA2DP_AudioSink = {
    0x0000110B, 0x0000, 0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}
};
static const GUID GUID_OA2DP_Handsfree = {
    0x0000111E, 0x0000, 0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}
};

static HDEVNOTIFY g_notify_handle = NULL;

/* ── helpers ────────────────────────────────────────────────────────── */

/* Check if a Bluetooth Class of Device indicates audio capability.
 * A2DP devices typically have:
 *   - Major class = Audio/Video (0x04), OR
 *   - Service class includes Audio bit (0x0100)
 */
static int is_audio_device(ULONG cod)
{
    ULONG major   = GET_COD_MAJOR(cod);
    ULONG service = GET_COD_SERVICE(cod);

    if (major == COD_MAJOR_AUDIO)
        return 1;
    if (service & COD_SERVICE_AUDIO)
        return 1;
    return 0;
}

/* Format a Bluetooth address as "XX:XX:XX:XX:XX:XX". */
static void format_bt_address(BLUETOOTH_ADDRESS addr, char *buf, int buf_size)
{
    BYTE *b = addr.rgBytes;
    snprintf(buf, buf_size, "%02X:%02X:%02X:%02X:%02X:%02X",
             b[5], b[4], b[3], b[2], b[1], b[0]);
}

/* Convert wide string to UTF-8 into a fixed buffer. */
static void wide_to_utf8(const WCHAR *src, char *dst, int dst_size)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dst_size, NULL, NULL);
    if (len <= 0 && dst_size > 0)
        dst[0] = '\0';
}

/*
 * Query which Bluetooth services are installed on a device record and
 * set status->audio_sink_installed / status->handsfree_installed.
 *
 * "Installed" here means registered against the device — i.e. the bit
 * BluetoothSetServiceState reads.  It doesn't tell us which one is the
 * active audio route; Windows doesn't expose that in user mode.
 */
static void query_installed_services(BLUETOOTH_DEVICE_INFO *info,
                                     OA2DP_DeviceStatus *stat)
{
    stat->audio_sink_installed = 0;
    stat->handsfree_installed  = 0;

    DWORD num_services = 0;
    DWORD result = BluetoothEnumerateInstalledServices(NULL, info,
                                                       &num_services, NULL);
    if (num_services == 0)
        return;
    if (result != ERROR_SUCCESS && result != ERROR_MORE_DATA)
        return;

    GUID *guids = (GUID *)malloc(sizeof(GUID) * num_services);
    if (!guids) return;

    result = BluetoothEnumerateInstalledServices(NULL, info,
                                                 &num_services, guids);
    if (result == ERROR_SUCCESS) {
        for (DWORD i = 0; i < num_services; i++) {
            if (memcmp(&guids[i], &GUID_OA2DP_AudioSink, sizeof(GUID)) == 0)
                stat->audio_sink_installed = 1;
            else if (memcmp(&guids[i], &GUID_OA2DP_Handsfree, sizeof(GUID)) == 0)
                stat->handsfree_installed = 1;
        }
    }

    free(guids);
}

/* ── scan ───────────────────────────────────────────────────────────── */

int oa2dp_device_scan(OA2DP_DeviceList *list)
{
    if (!list) return -1;
    list->count = 0;

    BLUETOOTH_DEVICE_SEARCH_PARAMS search_params;
    memset(&search_params, 0, sizeof(search_params));
    search_params.dwSize              = sizeof(search_params);
    search_params.fReturnAuthenticated = TRUE;
    search_params.fReturnRemembered    = TRUE;
    search_params.fReturnConnected     = TRUE;
    search_params.fReturnUnknown       = FALSE;
    search_params.fIssueInquiry        = FALSE;
    search_params.cTimeoutMultiplier   = 0;
    search_params.hRadio               = NULL;

    BLUETOOTH_DEVICE_INFO device_info;
    memset(&device_info, 0, sizeof(device_info));
    device_info.dwSize = sizeof(device_info);

    HBLUETOOTH_DEVICE_FIND hFind =
        BluetoothFindFirstDevice(&search_params, &device_info);

    if (hFind == NULL) {
        DWORD err = GetLastError();
        if (err == ERROR_NO_MORE_ITEMS) {
            oa2dp_log(OA2DP_LOG_INFO, "device scan: no paired Bluetooth devices found");
            return 0;
        }
        oa2dp_log(OA2DP_LOG_ERROR, "device scan: BluetoothFindFirstDevice failed (err=%lu)", err);
        return -1;
    }

    do {
        if (list->count >= OA2DP_MAX_DEVICES) {
            oa2dp_log(OA2DP_LOG_WARN, "device scan: max device limit reached (%d)",
                      OA2DP_MAX_DEVICES);
            break;
        }

        /* Filter: only audio-capable devices. */
        if (!is_audio_device(device_info.ulClassofDevice))
            continue;

        int idx = list->count;
        OA2DP_DeviceProfile *prof = &list->profiles[idx];
        OA2DP_DeviceStatus  *stat = &list->statuses[idx];

        /* Start with safe defaults. */
        oa2dp_profile_defaults(prof);
        memset(stat, 0, sizeof(*stat));

        /* Device ID (Bluetooth address). */
        format_bt_address(device_info.Address,
                          prof->device_id, sizeof(prof->device_id));
        snprintf(stat->device_id, sizeof(stat->device_id), "%s", prof->device_id);

        /* Display name. */
        wide_to_utf8(device_info.szName,
                     prof->display_name, sizeof(prof->display_name));

        /* Try to load a saved profile (user settings). */
        {
            char path[MAX_PATH];
            if (oa2dp_config_path_for_device(prof->device_id, path, sizeof(path)) == 0) {
                char saved_id[256];
                char saved_name[128];
                snprintf(saved_id, sizeof(saved_id), "%s", prof->device_id);
                snprintf(saved_name, sizeof(saved_name), "%s", prof->display_name);

                if (oa2dp_profile_load(path, prof) == 0) {
                    oa2dp_log(OA2DP_LOG_INFO, "device scan: loaded saved profile for '%s'",
                              saved_name);
                }
                /* Always keep the live device_id and display_name from the scan. */
                snprintf(prof->device_id, sizeof(prof->device_id), "%s", saved_id);
                snprintf(prof->display_name, sizeof(prof->display_name), "%s", saved_name);
            }
        }

        /* Connection state. */
        stat->connection = device_info.fConnected
                               ? OA2DP_CONN_CONNECTED
                               : OA2DP_CONN_DISCONNECTED;

        /* Per-device service registration flags (works for paired
         * devices regardless of connection state). */
        query_installed_services(&device_info, stat);

        /* For connected devices, query the audio endpoint for real data.
         * Codec/bitpool/stereo-mode etc. are AVDTP-internal and not
         * exposed by any user-mode Windows API, so we leave them at
         * UNKNOWN/0 rather than fabricating values.  See
         * docs/driver-evaluation.md for the gory details. */
        if (stat->connection == OA2DP_CONN_CONNECTED) {
            stat->active_codec = OA2DP_CODEC_UNKNOWN;
            /* WASAPI gives us the only fields we can honestly populate.
             * Pass display_name so the matcher can fall back to friendly
             * name when the endpoint ID doesn't embed the BT address
             * (Alternative A2DP Driver does this). */
            (void)oa2dp_audio_status_query(prof->device_id,
                                           prof->display_name, stat);
        }

        oa2dp_log(OA2DP_LOG_INFO, "device scan: [%d] %s (%s) - %s",
                  idx, prof->display_name, prof->device_id,
                  stat->connection == OA2DP_CONN_CONNECTED
                      ? "connected" : "disconnected");

        list->count++;

    } while (BluetoothFindNextDevice(hFind, &device_info));

    BluetoothFindDeviceClose(hFind);

    oa2dp_log(OA2DP_LOG_INFO, "device scan: found %d audio device(s)", list->count);
    return list->count;
}

/* ── refresh status ─────────────────────────────────────────────────── */

int oa2dp_device_refresh_status(OA2DP_DeviceList *list)
{
    if (!list) return -1;

    for (int i = 0; i < list->count; i++) {
        OA2DP_DeviceProfile *prof = &list->profiles[i];
        OA2DP_DeviceStatus  *stat = &list->statuses[i];

        /* Parse the address back from the stored string. */
        BLUETOOTH_DEVICE_INFO info;
        memset(&info, 0, sizeof(info));
        info.dwSize = sizeof(info);

        unsigned int b[6];
        if (sscanf(prof->device_id, "%02X:%02X:%02X:%02X:%02X:%02X",
                   &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
            info.Address.rgBytes[5] = (BYTE)b[0];
            info.Address.rgBytes[4] = (BYTE)b[1];
            info.Address.rgBytes[3] = (BYTE)b[2];
            info.Address.rgBytes[2] = (BYTE)b[3];
            info.Address.rgBytes[1] = (BYTE)b[4];
            info.Address.rgBytes[0] = (BYTE)b[5];
        } else {
            continue;
        }

        DWORD result = BluetoothGetDeviceInfo(NULL, &info);
        if (result == ERROR_SUCCESS) {
            OA2DP_ConnState prev = stat->connection;
            stat->connection = info.fConnected
                                   ? OA2DP_CONN_CONNECTED
                                   : OA2DP_CONN_DISCONNECTED;

            /* Refresh installed-service flags every poll — they can
             * change when something else (or our own service buttons)
             * toggles BluetoothSetServiceState. */
            query_installed_services(&info, stat);

            if (stat->connection != prev) {
                oa2dp_log(OA2DP_LOG_INFO, "status: %s is now %s",
                          prof->display_name,
                          stat->connection == OA2DP_CONN_CONNECTED
                              ? "connected" : "disconnected");

                if (stat->connection == OA2DP_CONN_CONNECTED) {
                    /* See note in oa2dp_device_scan: codec / SBC fields
                     * are not knowable from user-mode, only WASAPI mix
                     * format is real. */
                    stat->active_codec = OA2DP_CODEC_UNKNOWN;
                    stat->sample_rate = 0;
                    stat->bit_depth   = 0;
                    stat->channels    = 0;
                    stat->estimated_bitrate_kbps = 0;
                    (void)oa2dp_audio_status_query(prof->device_id,
                                                   prof->display_name, stat);

                    /* If the user opted into auto-heal for this device,
                     * kick off a check on a background thread.  The worker
                     * waits a settle period and only acts if WASAPI still
                     * has no endpoint by then. */
                    if (prof->auto_heal_enabled)
                        oa2dp_auto_heal_trigger(prof->device_id,
                                                prof->display_name);
                }
            }
        }
    }
    return 0;
}

/* ── device change notifications ────────────────────────────────────── */

int oa2dp_device_register_notify(void *hwnd)
{
    DEV_BROADCAST_DEVICEINTERFACE filter;
    memset(&filter, 0, sizeof(filter));
    filter.dbcc_size       = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid  = OA2DP_GUID_BTHPORT;

    g_notify_handle = RegisterDeviceNotificationW(
        (HANDLE)hwnd, &filter,
        DEVICE_NOTIFY_WINDOW_HANDLE);

    if (!g_notify_handle) {
        oa2dp_log(OA2DP_LOG_ERROR,
                  "device notify: RegisterDeviceNotification failed (err=%lu)",
                  GetLastError());
        return -1;
    }

    oa2dp_log(OA2DP_LOG_INFO, "device notify: registered for Bluetooth changes");
    return 0;
}

void oa2dp_device_unregister_notify(void)
{
    if (g_notify_handle) {
        UnregisterDeviceNotification(g_notify_handle);
        g_notify_handle = NULL;
        oa2dp_log(OA2DP_LOG_INFO, "device notify: unregistered");
    }
}
