/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_driver_detect.h - Read-only detection of installed A2DP-related
 * Bluetooth drivers and services.
 *
 * Used to surface what stack the user has installed (Microsoft, the
 * Alternative A2DP Driver from bluetoothgoodies.com, or both) so it
 * shows up in the log at startup.  Pure observation — does not control
 * anything.
 */

#ifndef OA2DP_DRIVER_DETECT_H
#define OA2DP_DRIVER_DETECT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Enumerate Win32 services and kernel drivers, find any whose service
 * name or display name contains "a2dp" (case-insensitive), and log
 * each match at INFO level.  Logs a single summary line at WARN if
 * none are found (which would be unusual on a working Bluetooth
 * machine).
 *
 * Returns the number of matches found, or -1 on error.
 */
int oa2dp_driver_detect_log(void);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_DRIVER_DETECT_H */
