/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_smtc.h - Observe System Media Transport Controls events.
 *
 * Phase B of remote-event tracking: when a Bluetooth headset
 * sends an AVRCP play/pause/next/prev command on Win10/11, the
 * Bluetooth stack translates it directly into a SMTC command on
 * the active media session — no synthetic keystroke is generated.
 * The only canonical way to observe these events from a third-party
 * app is the WinRT GlobalSystemMediaTransportControlsSessionManager
 * API.  This module wraps that API and polls the current session
 * for state transitions, logging them with the "remote:" prefix.
 *
 * Implemented in C++/WinRT (smtc_observer.cpp).  The .h is plain
 * C-callable so the rest of remote_events.cpp can call into it
 * without dragging cppwinrt into every translation unit.
 */

#ifndef OA2DP_SMTC_H
#define OA2DP_SMTC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the SMTC session manager.  Returns 0 on success, -1
 * on failure (in which case poll/shutdown become no-ops).  Safe to
 * call from the main thread after CoInitializeEx has been done. */
int  oa2dp_smtc_init(void);

/* Poll the current session and log any state transitions.  Cheap;
 * a couple of WinRT calls per invocation. */
void oa2dp_smtc_poll(void);

/* Release the cached session manager.  Safe to call regardless of
 * whether init succeeded. */
void oa2dp_smtc_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_SMTC_H */
