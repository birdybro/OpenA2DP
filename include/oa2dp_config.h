/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_config.h - Profile config load/save/validation API
 */

#ifndef OA2DP_CONFIG_H
#define OA2DP_CONFIG_H

#include "oa2dp_types.h"

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

#endif /* OA2DP_CONFIG_H */
