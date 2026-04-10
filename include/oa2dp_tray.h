/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_tray.h - System tray icon and popup menu.
 *
 * Adds an icon to the Windows notification area so common recovery
 * actions (reconnect, disable HFP, switch stack) are reachable
 * without opening the main window — which matters for the actual
 * use case: HFP kicked in mid-listening and you want to fix it now,
 * not after navigating UI.
 *
 * The tray module owns its own custom Win32 message and a small
 * range of menu command IDs.  The window proc routes the relevant
 * WM_COMMAND and the custom callback message into oa2dp_tray_*
 * functions; everything else stays inside this module.
 */

#ifndef OA2DP_TRAY_H
#define OA2DP_TRAY_H

#include "panels.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Custom Win32 message the tray icon posts back when clicked. */
#define OA2DP_WM_TRAY (WM_APP + 1)

/* Menu command ID range.  Keep these distinct from anything else. */
#define OA2DP_TRAY_ID_BASE         0x9000
#define OA2DP_TRAY_ID_SHOW         (OA2DP_TRAY_ID_BASE + 0)
#define OA2DP_TRAY_ID_HIDE         (OA2DP_TRAY_ID_BASE + 1)
#define OA2DP_TRAY_ID_SWITCH_MS    (OA2DP_TRAY_ID_BASE + 2)
#define OA2DP_TRAY_ID_SWITCH_ALT   (OA2DP_TRAY_ID_BASE + 3)
#define OA2DP_TRAY_ID_QUIT         (OA2DP_TRAY_ID_BASE + 4)
/* Per-device IDs occupy a 256-slot range starting here.  Each device
 * gets two slots: one for Reconnect, one for Disable HFP. */
#define OA2DP_TRAY_ID_DEV_BASE     (OA2DP_TRAY_ID_BASE + 0x100)
#define OA2DP_TRAY_DEV_SLOTS       2  /* per device: reconnect, disable-hfp */

/* Returns 1 if a command id falls inside the tray's reserved range. */
static inline int oa2dp_tray_owns_command(int id) {
    return id >= OA2DP_TRAY_ID_BASE &&
           id <  OA2DP_TRAY_ID_DEV_BASE + 0x100;
}

/* Initialize the tray icon and register the callback message. */
int  oa2dp_tray_init(void *hwnd);

/* Remove the tray icon.  Safe to call if init failed. */
void oa2dp_tray_shutdown(void);

/* Handle the tray callback message (right-click → show menu, etc). */
void oa2dp_tray_handle_message(void *hwnd, OA2DP_UIState *ui,
                               unsigned int wparam, long lparam);

/* Handle a WM_COMMAND whose ID is in the tray range. */
void oa2dp_tray_handle_command(void *hwnd, OA2DP_UIState *ui, int cmd_id);

/* Toggle main window visibility (show/restore vs hide). */
void oa2dp_tray_toggle_window(void *hwnd);

/*
 * Show a Windows balloon-tip / toast through the tray icon.  Used by
 * auto-heal to surface successful recoveries and give-up events
 * without requiring the user to be looking at the log panel.
 *
 * Both arguments are UTF-8.  No-op if the tray icon was never
 * successfully added (so it's safe to call from CLI mode or before
 * tray init), or if notifications have been disabled via
 * oa2dp_tray_notifications_set_enabled.
 */
void oa2dp_tray_notify(const char *title, const char *message);

/*
 * Enable or disable balloon notifications globally.  When disabled,
 * oa2dp_tray_notify becomes a no-op (the tray icon and right-click
 * menu still work).  Default is disabled — the user opts in via the
 * "Tray Notifications" checkbox in the UI.
 */
void oa2dp_tray_notifications_set_enabled(int enabled);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_TRAY_H */
