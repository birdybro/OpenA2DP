/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_remote_events.h - Observe Bluetooth remote control events
 * (tap = play/pause, double-tap = next, swipe = volume, etc.)
 *
 * Phase A surfaces:
 *   1. Low-level keyboard hook for VK_MEDIA_* virtual keys.  Some
 *      Bluetooth drivers translate AVRCP commands into synthetic
 *      Windows media keystrokes; this catches those.
 *   2. WASAPI default render endpoint master volume polling.  AVRCP
 *      volume commands always show up here regardless of driver.
 *
 * If Pixel Buds Pro 2 (or similar) doesn't generate synthetic
 * keystrokes for play/pause/next/prev on Win10/11, Phase B will
 * add a WinRT GlobalSystemMediaTransportControlsSessionManager
 * observer to catch the AVRCP-to-SMTC path directly.  For now we
 * ship the cheap surfaces and see what comes through.
 *
 * All events go to the standard ring-buffer logger with INFO
 * severity, prefixed with "remote: " so they're easy to filter.
 */

#ifndef OA2DP_REMOTE_EVENTS_H
#define OA2DP_REMOTE_EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Install the keyboard hook and initialise volume tracking.
 * `hwnd` is currently unused but kept for future use (e.g. if we
 * want to subscribe to default-device-changed notifications via
 * IMMNotificationClient on the window's COM apartment).
 *
 * Returns 0 on success, -1 on failure (typically only fails if
 * the keyboard hook can't be installed, which would mean the
 * process lacks the rights to add a system-wide hook — rare).
 */
int oa2dp_remote_events_init(void *hwnd);

/* Tear down hook + COM state. */
void oa2dp_remote_events_shutdown(void);

/*
 * Poll the default render endpoint's master volume and log any
 * change since the previous poll.  Call from the main loop on a
 * sub-second cadence (~200 ms is fine — swipes are slow user
 * actions, no need for tighter polling).  Cheap: a couple of COM
 * calls per invocation.
 */
void oa2dp_remote_events_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_REMOTE_EVENTS_H */
