/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_audio_status.h - Query audio endpoint status for BT devices
 */

#ifndef OA2DP_AUDIO_STATUS_H
#define OA2DP_AUDIO_STATUS_H

#include "oa2dp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize COM on the main thread.  Call once at startup. */
int oa2dp_audio_status_init(void);

/* Shut down COM on the main thread. */
void oa2dp_audio_status_shutdown(void);

/* Per-thread COM init / shutdown for worker threads that want to call
 * oa2dp_audio_status_query.  Each call to _thread_init must be paired
 * with a _thread_shutdown on the same thread.  Failing to call these
 * leaves the worker thread in an uninitialised apartment, in which
 * case audio_status_query will fail with -1 (no crash, just no data). */
int  oa2dp_audio_status_thread_init(void);
void oa2dp_audio_status_thread_shutdown(void);

/*
 * Query the audio endpoint associated with a Bluetooth device and
 * fill in detectable fields of the status struct:
 *   - sample_rate, bit_depth, channels, estimated_bitrate_kbps
 *
 * device_id is the BT address string "XX:XX:XX:XX:XX:XX".
 * display_name is the device's friendly name from BluetoothGetDeviceInfo
 *   (e.g. "Pixel Buds Pro 2"); may be NULL.
 *
 * Matching strategy:
 *   1. Look for the BT address (lowercase, no separators) inside the
 *      WASAPI endpoint device ID — works for the Microsoft stack.
 *   2. Fall back to a substring match of display_name against the
 *      endpoint's PKEY_Device_FriendlyName — works for endpoints that
 *      don't embed the BT address in their ID, including the
 *      Alternative A2DP Driver.
 *
 * Returns 0 if an endpoint was found and queried, -1 otherwise.
 * On a -1 return, dumps the friendly names of all enumerated render
 * endpoints to the log at INFO level so the user can see what is
 * actually present.
 */
int oa2dp_audio_status_query(const char *device_id,
                             const char *display_name,
                             OA2DP_DeviceStatus *status);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_AUDIO_STATUS_H */
