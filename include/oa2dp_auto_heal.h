/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_auto_heal.h - Auto-heal a connect-but-no-audio bluetooth device
 *
 * When a Bluetooth audio device transitions to connected, Windows 11
 * occasionally fails to bind the AudioSink service to a WASAPI endpoint:
 * the device shows as Connected but no audio comes out.  Auto-heal
 * detects this and cycles AudioSink to force a re-bind.
 */

#ifndef OA2DP_AUTO_HEAL_H
#define OA2DP_AUTO_HEAL_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Trigger an auto-heal check for the given device on a background thread.
 * Safe to call from the main UI thread.  No-op if a heal is already
 * running for any device (single-slot, like the actions API).
 *
 * The worker waits a settle period for the audio endpoint to appear
 * naturally, then if it's still missing cycles AudioSink up to a
 * fixed number of attempts.
 *
 * device_id: BT address string "XX:XX:XX:XX:XX:XX"
 * Returns 0 if the worker was launched, -1 if busy or on error.
 */
int oa2dp_auto_heal_trigger(const char *device_id);

/* Returns 1 if an auto-heal worker is currently running. */
int oa2dp_auto_heal_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_AUTO_HEAL_H */
