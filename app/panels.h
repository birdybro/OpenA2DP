/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * panels.h - UI panel drawing functions (pure C, using cimgui)
 */

#ifndef OA2DP_PANELS_H
#define OA2DP_PANELS_H

#include "oa2dp_types.h"

/* Maximum mock devices the UI can hold. */
#define OA2DP_MAX_DEVICES 8

/* Aggregated UI state passed each frame. */
typedef struct OA2DP_UIState {
    OA2DP_DeviceProfile profiles[OA2DP_MAX_DEVICES];
    OA2DP_DeviceStatus  statuses[OA2DP_MAX_DEVICES];
    int                 device_count;
    int                 selected;      /* index into profiles/statuses */
} OA2DP_UIState;

/* Draw the full UI (call once per frame between begin/end frame). */
void oa2dp_panels_draw(OA2DP_UIState *ui);

#endif /* OA2DP_PANELS_H */
