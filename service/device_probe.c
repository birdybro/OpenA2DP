/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * device_probe.c - Background worker that fills "slow" device fields.
 *
 * What goes here is exactly the stuff we are NOT allowed to call from
 * the UI thread:
 *
 *   - BluetoothEnumerateInstalledServices  (notorious for blocking
 *     for seconds, killed the UI when called inline — see commit
 *     1af9b66 for the post-mortem).
 *
 *   - SetupAPI device property reads for DEVPKEY_Bluetooth_Battery,
 *     which walks the device tree and isn't always cheap.
 *
 * Single-slot worker.  Caller (main loop) starts it once at startup
 * and after every rescan; the worker writes results back into the
 * device list atomically (single-int writes are atomic on x86/x64
 * for naturally-aligned ints, which is what we have).
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bluetoothapis.h>
#include <bthdef.h>
#include <initguid.h>
#include <setupapi.h>
#include <devpkey.h>

#include "oa2dp_device_probe.h"
#include "oa2dp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── known service GUIDs (also defined in actions.c / device_enum.c) */

static const GUID GUID_PROBE_AudioSink = {
    0x0000110B, 0x0000, 0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}
};
static const GUID GUID_PROBE_Handsfree = {
    0x0000111E, 0x0000, 0x1000,
    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}
};

/* DEVPKEY_Bluetooth_DeviceAddress and DEVPKEY_Bluetooth_Battery —
 * defined in newer SDKs but we declare them locally so the build
 * works on older toolchains too. */
DEFINE_DEVPROPKEY(OA2DP_DEVPKEY_Bluetooth_DeviceAddress,
    0x2BD67D8B, 0x8BEB, 0x48D5,
    0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A, 1);
DEFINE_DEVPROPKEY(OA2DP_DEVPKEY_Bluetooth_Battery,
    0x104EA319, 0x6EE2, 0x4701,
    0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5, 2);

/* ── busy flag ──────────────────────────────────────────────────────── */

static volatile LONG g_probe_busy = 0;

int oa2dp_device_probe_busy(void)
{
    return (int)g_probe_busy;
}

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

/* Read installed-services flags via BluetoothEnumerateInstalledServices.
 * Returns 0 if successful (regardless of whether anything was found). */
static int probe_installed_services(BLUETOOTH_DEVICE_INFO *info,
                                    int *audio_sink, int *handsfree)
{
    *audio_sink = 0;
    *handsfree  = 0;

    DWORD num = 0;
    DWORD result = BluetoothEnumerateInstalledServices(NULL, info, &num, NULL);
    if (num == 0) return 0;
    if (result != ERROR_SUCCESS && result != ERROR_MORE_DATA) return -1;

    GUID *guids = (GUID *)malloc(sizeof(GUID) * num);
    if (!guids) return -1;

    result = BluetoothEnumerateInstalledServices(NULL, info, &num, guids);
    if (result == ERROR_SUCCESS) {
        for (DWORD i = 0; i < num; i++) {
            if (memcmp(&guids[i], &GUID_PROBE_AudioSink, sizeof(GUID)) == 0)
                *audio_sink = 1;
            else if (memcmp(&guids[i], &GUID_PROBE_Handsfree, sizeof(GUID)) == 0)
                *handsfree = 1;
        }
    }
    free(guids);
    return 0;
}

/* Walk the BT device tree via SetupAPI looking for an entry whose
 * DEVPKEY_Bluetooth_DeviceAddress matches our address (as a 64-bit
 * int).  If found, read DEVPKEY_Bluetooth_Battery and return the
 * percentage.  Returns -1 on any failure (treated as "no battery
 * info available"). */
static int probe_battery(BLUETOOTH_ADDRESS *addr)
{
    /* The Bluetooth class GUID for paired BT devices. */
    static const GUID GUID_BTH_CLASS = {
        0xe0cbf06c, 0xcd8b, 0x4647,
        {0xbb, 0x8a, 0x26, 0x3b, 0x43, 0xf0, 0xf9, 0x74}
    };

    HDEVINFO h = SetupDiGetClassDevsW(&GUID_BTH_CLASS, NULL, NULL,
                                       DIGCF_PRESENT);
    if (h == INVALID_HANDLE_VALUE) return -1;

    /* The address from BLUETOOTH_ADDRESS is stored little-endian; the
     * DevProp returns it as a uint64. */
    UINT64 want_addr = 0;
    for (int i = 0; i < 6; i++)
        want_addr |= (UINT64)addr->rgBytes[i] << (i * 8);

    int battery = -1;

    SP_DEVINFO_DATA info;
    info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(h, i, &info); i++) {
        DEVPROPTYPE type;
        UINT64 dev_addr = 0;
        DWORD bytes = 0;
        if (!SetupDiGetDevicePropertyW(h, &info,
                &OA2DP_DEVPKEY_Bluetooth_DeviceAddress,
                &type, (PBYTE)&dev_addr, sizeof(dev_addr), &bytes, 0))
            continue;
        if (dev_addr != want_addr)
            continue;

        BYTE bat = 0;
        if (SetupDiGetDevicePropertyW(h, &info,
                &OA2DP_DEVPKEY_Bluetooth_Battery,
                &type, &bat, sizeof(bat), &bytes, 0)) {
            battery = (int)bat;
        }
        break;
    }

    SetupDiDestroyDeviceInfoList(h);
    return battery;
}

/* ── worker ─────────────────────────────────────────────────────────── */

typedef struct {
    OA2DP_DeviceList *list;
} ProbeParam;

static DWORD WINAPI probe_thread(LPVOID param)
{
    ProbeParam *p = (ProbeParam *)param;
    OA2DP_DeviceList *list = p->list;

    int snapshot_count = list->count;
    oa2dp_log(OA2DP_LOG_DEBUG,
              "device probe: starting pass over %d device(s)", snapshot_count);

    for (int i = 0; i < snapshot_count && i < list->count; i++) {
        OA2DP_DeviceProfile *prof = &list->profiles[i];
        OA2DP_DeviceStatus  *stat = &list->statuses[i];

        BLUETOOTH_DEVICE_INFO info;
        memset(&info, 0, sizeof(info));
        info.dwSize = sizeof(info);
        if (parse_bt_address(prof->device_id, &info.Address) != 0)
            continue;

        if (BluetoothGetDeviceInfo(NULL, &info) != ERROR_SUCCESS)
            continue;

        int audio_sink = 0, handsfree = 0;
        if (probe_installed_services(&info, &audio_sink, &handsfree) == 0) {
            stat->audio_sink_installed = audio_sink;
            stat->handsfree_installed  = handsfree;
        }

        int batt = probe_battery(&info.Address);
        if (batt >= 0)
            stat->battery_pct = batt;
        else
            stat->battery_pct = -2;  /* sentinel: probed, not available */

        oa2dp_log(OA2DP_LOG_DEBUG,
                  "device probe: %s -> sink=%d hfp=%d batt=%d",
                  prof->device_id, audio_sink, handsfree, batt);
    }

    free(p);
    InterlockedExchange(&g_probe_busy, 0);
    oa2dp_log(OA2DP_LOG_DEBUG, "device probe: done");
    return 0;
}

int oa2dp_device_probe_start(OA2DP_DeviceList *list)
{
    if (!list) return -1;

    if (InterlockedCompareExchange(&g_probe_busy, 1, 0) != 0)
        return -1;

    ProbeParam *p = (ProbeParam *)malloc(sizeof(*p));
    if (!p) {
        InterlockedExchange(&g_probe_busy, 0);
        return -1;
    }
    p->list = list;

    HANDLE h = CreateThread(NULL, 0, probe_thread, p, 0, NULL);
    if (!h) {
        free(p);
        InterlockedExchange(&g_probe_busy, 0);
        return -1;
    }
    CloseHandle(h);
    return 0;
}
