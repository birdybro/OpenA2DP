/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_device.h - Device enumeration and notification API
 */

#ifndef OA2DP_DEVICE_H
#define OA2DP_DEVICE_H

#include "oa2dp_types.h"

#define OA2DP_MAX_DEVICES 16

/* Result of a device scan. */
typedef struct OA2DP_DeviceList {
    OA2DP_DeviceProfile profiles[OA2DP_MAX_DEVICES];
    OA2DP_DeviceStatus  statuses[OA2DP_MAX_DEVICES];
    int                 count;
} OA2DP_DeviceList;

/*
 * Scan for paired Bluetooth audio devices.
 * Fills 'list' with up to OA2DP_MAX_DEVICES entries.
 * Returns the number of devices found, or -1 on error.
 */
int oa2dp_device_scan(OA2DP_DeviceList *list);

/*
 * Refresh connection status for all devices in the list.
 * Call periodically (e.g. once per second) to keep statuses current.
 * Returns 0 on success, -1 on error.
 */
int oa2dp_device_refresh_status(OA2DP_DeviceList *list);

/*
 * Register the window to receive WM_DEVICECHANGE messages
 * when Bluetooth devices arrive or depart.
 * Returns 0 on success, -1 on failure.
 */
int oa2dp_device_register_notify(void *hwnd);

/* Unregister device change notifications. */
void oa2dp_device_unregister_notify(void);

#endif /* OA2DP_DEVICE_H */
