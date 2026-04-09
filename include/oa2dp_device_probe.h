/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_device_probe.h - Background slow-Bluetooth-API worker.
 *
 * Some Bluetooth queries (BluetoothEnumerateInstalledServices and the
 * SetupAPI battery property read) can block the calling thread for
 * seconds.  Calling them from the UI thread or the 2-second status
 * refresh path makes the window go Not Responding immediately.
 *
 * This module owns a single background worker that walks the device
 * list off-thread, fills the slow fields (audio_sink_installed,
 * handsfree_installed, battery_pct) and writes them back atomically.
 * The UI just reads whatever's there at draw time.
 *
 * Single-slot — only one probe runs at a time.  Triggered manually
 * after the initial scan and after every device-change rescan.
 */

#ifndef OA2DP_DEVICE_PROBE_H
#define OA2DP_DEVICE_PROBE_H

#include "oa2dp_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Kick off a background pass over the given device list.  Safe to
 * call from the UI thread.  No-op if a probe is already running.
 *
 * The list pointer must remain valid for the lifetime of the worker.
 * In OpenA2DP it lives in g_ui which is static for the process.
 */
int  oa2dp_device_probe_start(OA2DP_DeviceList *list);

/* Returns 1 if a probe is currently running. */
int  oa2dp_device_probe_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_DEVICE_PROBE_H */
