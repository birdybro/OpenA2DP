/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_hfp_watchdog.h - Periodic re-disable of Handsfree (HFP) for
 * devices where the user only wants A2DP playback.
 *
 * Some Windows updates and audio apps re-enable the Handsfree service
 * on Bluetooth audio devices, which causes Windows to fall back to
 * narrowband mono SCO instead of A2DP.  The watchdog re-disables HFP
 * on a fixed cadence for any device the user has opted in.
 */

#ifndef OA2DP_HFP_WATCHDOG_H
#define OA2DP_HFP_WATCHDOG_H

#include "oa2dp_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Suggested tick interval in milliseconds.  The caller is responsible
 * for invoking oa2dp_hfp_watchdog_tick() at roughly this cadence.
 */
#define OA2DP_HFP_WATCHDOG_INTERVAL_MS 30000

/*
 * Walk the device list and, for any connected device with
 * hfp_watchdog_enabled set in its profile, fire an asynchronous
 * Handsfree-disable.  Safe to call from the UI thread; the actual
 * BluetoothSetServiceState calls run on the actions worker thread.
 *
 * Calls are idempotent: if Handsfree is already disabled, the
 * underlying API returns ERROR_NOT_FOUND which actions.c logs at
 * INFO and treats as success.
 */
void oa2dp_hfp_watchdog_tick(const OA2DP_DeviceList *list);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_HFP_WATCHDOG_H */
