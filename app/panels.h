/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * panels.h - UI panel drawing functions (pure C, using cimgui)
 */

#ifndef OA2DP_PANELS_H
#define OA2DP_PANELS_H

#include "oa2dp_device.h"

/* Aggregated UI state passed each frame. */
typedef struct OA2DP_UIState {
    OA2DP_DeviceList    devices;
    int                 selected;       /* index into devices */

    /* Log panel state. */
    int                 log_show_level[4];  /* filter per OA2DP_LogLevel */
    int                 log_auto_scroll;
} OA2DP_UIState;

/* Initialize UI state with sensible defaults. */
void oa2dp_ui_state_init(OA2DP_UIState *ui);

/* Draw the full UI (call once per frame between begin/end frame). */
void oa2dp_panels_draw(OA2DP_UIState *ui);

#endif /* OA2DP_PANELS_H */
