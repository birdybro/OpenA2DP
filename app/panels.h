/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * panels.h - UI panel drawing functions (pure C, using cimgui)
 */

#ifndef OA2DP_PANELS_H
#define OA2DP_PANELS_H

#include "oa2dp_device.h"
#include "oa2dp_driver_control.h"

/* Aggregated UI state passed each frame. */
typedef struct OA2DP_UIState {
    OA2DP_DeviceList    devices;
    int                 selected;       /* index into devices */

    /* A2DP stack services discovered at startup (BthA2dp, AltA2DP, etc.) */
    OA2DP_DriverList    drivers;

    /* Log panel state. */
    int                 log_show_level[4];  /* filter per OA2DP_LogLevel */
    int                 log_auto_scroll;

    /* Advanced Mode toggle.  When 0 (the default), the UI hides
     * stacks/codec/services/watchdogs/capabilities/history/log and
     * exposes only Reconnect / Reset plus a minimal status readout —
     * the 99% workflow.  Persisted alongside window state. */
    int                 advanced_mode;

    /* Main window HWND, stashed after CreateWindowW so panels can
     * reference it (used by the deferred reset path below). */
    void               *hwnd;

    /* Set by the Reset Settings popup when the user wants the
     * window resized back to its default rect.  Acted on by the
     * main loop AFTER the current frame ends — calling SetWindowPos
     * inside a draw re-enters our WM_SIZE → render_one_frame path
     * and crashes ImGui mid-frame. */
    int                 pending_window_reset;
} OA2DP_UIState;

/* Initialize UI state with sensible defaults. */
void oa2dp_ui_state_init(OA2DP_UIState *ui);

/* Draw the full UI (call once per frame between begin/end frame). */
void oa2dp_panels_draw(OA2DP_UIState *ui);

#endif /* OA2DP_PANELS_H */
