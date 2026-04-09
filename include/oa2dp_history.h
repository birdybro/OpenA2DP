/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_history.h - Per-device connection history (persistent).
 *
 * Records every connect/disconnect transition for a Bluetooth device
 * to a small text file in the config directory so the user can
 * answer "why did this drop at 2am" questions across runs.
 *
 * Each device gets its own file:
 *   %APPDATA%\OpenA2DP\<address>.history
 * with one event per line in the format:
 *   YYYY-MM-DD HH:MM:SS connected
 *   YYYY-MM-DD HH:MM:SS disconnected
 *
 * The file is capped at OA2DP_HISTORY_MAX_LINES entries — once
 * exceeded, the oldest half is dropped on the next append (so we
 * don't rewrite the file every single time).
 */

#ifndef OA2DP_HISTORY_H
#define OA2DP_HISTORY_H

#ifdef __cplusplus
extern "C" {
#endif

#define OA2DP_HISTORY_MAX_LINES 200

/* Append one event to the device's history file. */
void oa2dp_history_append(const char *device_id, const char *state_label);

/*
 * Load the most recent N entries from a device's history file into
 * the caller's buffer.  Each entry occupies one slot in `out` (a
 * line ~40 chars).  Returns the actual number of entries written.
 *
 * out_capacity is the number of slots, each `entry_size` chars wide.
 */
int oa2dp_history_load(const char *device_id,
                       char *out, int out_capacity, int entry_size);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_HISTORY_H */
