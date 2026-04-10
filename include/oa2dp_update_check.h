/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_update_check.h - GitHub Releases version check
 *
 * Once-per-session GET to api.github.com that compares the latest
 * release tag against this binary's embedded OA2DP_VER_STRING and
 * fires a tray notification when a newer release is available.
 *
 * Opt-in: only runs when the user has enabled the Auto Update Check
 * setting (persisted in window.ini).  Network IO is silent failure
 * — if the user is offline, no warning is generated.
 */

#ifndef OA2DP_UPDATE_CHECK_H
#define OA2DP_UPDATE_CHECK_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Spawn a one-shot worker thread to query the GitHub releases API.
 * Idempotent: only runs once per process lifetime — repeat calls
 * are no-ops.  Returns 0 on dispatch, -1 if a check is already in
 * flight or has already run this session.
 */
int oa2dp_update_check_async(void);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_UPDATE_CHECK_H */
