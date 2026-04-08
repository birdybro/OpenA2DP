/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * renderer.h - C-callable D3D11 + cimgui backend wrapper
 */

#ifndef OA2DP_RENDERER_H
#define OA2DP_RENDERER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize D3D11 device/swapchain and imgui backends.
 * hwnd is the HWND of the application window.
 * Returns 0 on success, -1 on failure. */
int oa2dp_renderer_init(void *hwnd);

/* Tear down imgui backends and D3D11 resources. */
void oa2dp_renderer_shutdown(void);

/* Begin a new frame (D3D11 + imgui NewFrame).
 * Returns 1 if the frame should proceed, 0 if it should be skipped
 * (e.g. window is occluded/minimized). */
int oa2dp_renderer_begin_frame(void);

/* End the frame: igRender, draw, present with vsync. */
void oa2dp_renderer_end_frame(void);

/* Queue a resize (called from WM_SIZE). */
void oa2dp_renderer_resize(unsigned int w, unsigned int h);

/* Forward a Win32 message to the imgui backend.
 * Returns 1 if imgui consumed the message (caller should return 0/TRUE),
 * 0 otherwise. */
int oa2dp_renderer_wndproc(void *hwnd, unsigned int msg,
                           uintptr_t wparam, intptr_t lparam);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_RENDERER_H */
