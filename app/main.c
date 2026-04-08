/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * main.c - Win32 entry point, window creation, and message loop
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbt.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "renderer.h"
#include "panels.h"
#include "oa2dp_device.h"
#include "oa2dp_log.h"

#include <string.h>

/* Forward declaration — we need the UI state in WndProc for device changes. */
static OA2DP_UIState g_ui;
static int g_rescan_needed = 0;

/* ── WndProc ────────────────────────────────────────────────────────── */

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg,
                                 WPARAM wparam, LPARAM lparam)
{
    if (oa2dp_renderer_wndproc(hwnd, msg, (uintptr_t)wparam, (intptr_t)lparam))
        return 0;

    switch (msg) {
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED)
            oa2dp_renderer_resize((UINT)LOWORD(lparam), (UINT)HIWORD(lparam));
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DEVICECHANGE:
        if (wparam == DBT_DEVICEARRIVAL || wparam == DBT_DEVICEREMOVECOMPLETE) {
            oa2dp_log(OA2DP_LOG_INFO, "Bluetooth device change detected");
            g_rescan_needed = 1;
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* ── Entry point ────────────────────────────────────────────────────── */

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;

    oa2dp_log_init();
    oa2dp_log(OA2DP_LOG_INFO, "OpenA2DP starting");

    /* Register window class. */
    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance      = hInstance;
    wc.lpszClassName  = L"OpenA2DP";
    RegisterClassExW(&wc);

    /* Create window. */
    HWND hwnd = CreateWindowW(
        wc.lpszClassName, L"OpenA2DP",
        WS_OVERLAPPEDWINDOW,
        100, 100, 1024, 640,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        oa2dp_log(OA2DP_LOG_ERROR, "CreateWindowW failed");
        return 1;
    }

    /* Initialize renderer (D3D11 + cimgui). */
    if (oa2dp_renderer_init(hwnd) != 0) {
        oa2dp_log(OA2DP_LOG_ERROR, "renderer init failed");
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    oa2dp_log(OA2DP_LOG_INFO, "renderer initialized");

    /* ── Device enumeration ─────────────────────────────────────── */
    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.selected = 0;

    oa2dp_device_scan(&g_ui.devices);

    if (g_ui.devices.count == 0)
        oa2dp_log(OA2DP_LOG_WARN, "no Bluetooth audio devices found");

    /* Register for device change notifications. */
    oa2dp_device_register_notify(hwnd);

    /* Status refresh timer — poll every ~2 seconds. */
    DWORD last_refresh = GetTickCount();
    const DWORD REFRESH_INTERVAL_MS = 2000;

    oa2dp_log(OA2DP_LOG_INFO, "entering main loop");

    /* Main loop. */
    MSG msg;
    int running = 1;
    while (running) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                running = 0;
        }
        if (!running)
            break;

        /* Re-scan if a device change was detected. */
        if (g_rescan_needed) {
            g_rescan_needed = 0;
            int prev_count = g_ui.devices.count;
            oa2dp_device_scan(&g_ui.devices);
            if (g_ui.selected >= g_ui.devices.count)
                g_ui.selected = (g_ui.devices.count > 0) ? 0 : -1;
            if (g_ui.devices.count != prev_count)
                oa2dp_log(OA2DP_LOG_INFO, "device list updated: %d device(s)",
                          g_ui.devices.count);
        }

        /* Periodic status refresh. */
        DWORD now = GetTickCount();
        if (now - last_refresh >= REFRESH_INTERVAL_MS) {
            oa2dp_device_refresh_status(&g_ui.devices);
            last_refresh = now;
        }

        if (!oa2dp_renderer_begin_frame())
            continue;

        oa2dp_panels_draw(&g_ui);

        oa2dp_renderer_end_frame();
    }

    oa2dp_log(OA2DP_LOG_INFO, "shutting down");
    oa2dp_device_unregister_notify();
    oa2dp_renderer_shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}
