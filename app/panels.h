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
    int                 selected;      /* index into devices */
} OA2DP_UIState;

/* Draw the full UI (call once per frame between begin/end frame). */
void oa2dp_panels_draw(OA2DP_UIState *ui);

#endif /* OA2DP_PANELS_H */
