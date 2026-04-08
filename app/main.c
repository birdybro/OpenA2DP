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

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "renderer.h"
#include "oa2dp_log.h"

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
        if ((wparam & 0xFFF0) == SC_KEYMENU)  /* disable ALT menu */
            return 0;
        break;
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

    oa2dp_log(OA2DP_LOG_INFO, "renderer initialized, entering main loop");

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

        if (!oa2dp_renderer_begin_frame())
            continue;

        /* ── UI goes here (step 4) ──────────────────────────────── */
        {
            igBegin("OpenA2DP", NULL, 0);
            igText("OpenA2DP shell is running.");
            igEnd();
        }

        oa2dp_renderer_end_frame();
    }

    oa2dp_log(OA2DP_LOG_INFO, "shutting down");
    oa2dp_renderer_shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}
