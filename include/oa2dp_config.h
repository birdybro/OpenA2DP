/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_config.h - Profile config load/save/validation API
 */

#ifndef OA2DP_CONFIG_H
#define OA2DP_CONFIG_H

#include "oa2dp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Ensure the config directory (%APPDATA%\OpenA2DP\) exists.
 * Returns 0 on success, -1 on failure.
 */
int oa2dp_config_init(void);

/*
 * Build the full file path for a device profile.
 * Writes to 'buf' (up to buf_size bytes).  The file is named by
 * the device's BT address with colons replaced by underscores.
 * Returns 0 on success, -1 on failure.
 */
int oa2dp_config_path_for_device(const char *device_id,
                                 char *buf, int buf_size);

/* Fill a profile with safe defaults. */
void oa2dp_profile_defaults(OA2DP_DeviceProfile *p);

/* Clamp / correct any out-of-range values in place. */
void oa2dp_profile_validate(OA2DP_DeviceProfile *p);

/*
 * Save a profile to a human-readable INI-style file.
 * Returns 0 on success, -1 on failure.
 * 'path' is the full file path (caller decides directory layout).
 */
int oa2dp_profile_save(const char *path, const OA2DP_DeviceProfile *p);

/*
 * Load a profile from an INI-style file.
 * Unknown keys are silently skipped.  Missing keys keep their
 * default values (caller should call oa2dp_profile_defaults first).
 * Returns 0 on success, -1 on failure.
 */
int oa2dp_profile_load(const char *path, OA2DP_DeviceProfile *p);

/*
 * Window position / size persistence.  Stored as a tiny INI file in
 * the config directory so the next launch can restore the window
 * exactly where the user left it.
 *
 * Save returns 0 on success.  Load returns 0 if the file existed and
 * was parsed, -1 otherwise — in which case the caller should fall
 * back to its built-in defaults.
 */
int oa2dp_window_state_save(int x, int y, int w, int h,
                            int advanced_mode, int update_check_enabled,
                            int tray_notifications_enabled);
int oa2dp_window_state_load(int *x, int *y, int *w, int *h,
                            int *advanced_mode, int *update_check_enabled,
                            int *tray_notifications_enabled);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_CONFIG_H */
