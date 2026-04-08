/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_actions.h - Reconnect / reset actions for Bluetooth devices
 */

#ifndef OA2DP_ACTIONS_H
#define OA2DP_ACTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reconnect a Bluetooth audio device by toggling its A2DP
 * Audio Sink service off then back on.  Runs synchronously.
 *
 * device_id: BT address string "XX:XX:XX:XX:XX:XX"
 * Returns 0 on success, -1 on failure.
 */
int oa2dp_action_reconnect(const char *device_id);

/*
 * Reset a Bluetooth audio device by cycling all audio-related
 * Bluetooth services (AudioSink + Handsfree).  Runs synchronously.
 *
 * device_id: BT address string "XX:XX:XX:XX:XX:XX"
 * Returns 0 on success, -1 on failure.
 */
int oa2dp_action_reset(const char *device_id);

/*
 * Async wrappers — run reconnect/reset on a background thread
 * so the UI stays responsive.  Safe to call from the UI thread.
 *
 * Only one action can run at a time; returns -1 if busy.
 */
int oa2dp_action_reconnect_async(const char *device_id);
int oa2dp_action_reset_async(const char *device_id);

/* Returns 1 if an action is currently running. */
int oa2dp_action_busy(void);

/*
 * Manually enable or disable a specific Bluetooth service.
 * Runs async on a background thread; returns -1 if busy.
 *
 * enable: 1 to enable, 0 to disable.
 */
int oa2dp_action_set_audiosink_async(const char *device_id, int enable);
int oa2dp_action_set_handsfree_async(const char *device_id, int enable);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_ACTIONS_H */
