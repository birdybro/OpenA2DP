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

/* Initialize COM and the audio status subsystem.
 * Call once at startup. Returns 0 on success. */
int oa2dp_audio_status_init(void);

/* Shut down the audio status subsystem.
 * Call once at shutdown. */
void oa2dp_audio_status_shutdown(void);

/*
 * Query the audio endpoint associated with a Bluetooth device and
 * fill in detectable fields of the status struct:
 *   - sample_rate, bit_depth, channels
 *
 * device_id is the BT address string "XX:XX:XX:XX:XX:XX".
 * Returns 0 if an endpoint was found and queried, -1 otherwise.
 *
 * Fields that cannot be detected from user mode (codec, bitpool,
 * stereo mode, subbands, allocation method) are left unchanged.
 */
int oa2dp_audio_status_query(const char *device_id,
                             OA2DP_DeviceStatus *status);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_AUDIO_STATUS_H */
