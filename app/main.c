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
#include "panels.h"
#include "oa2dp_log.h"
#include "oa2dp_config.h"

#include <string.h>

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

    /* ── Mock data ──────────────────────────────────────────────── */
    OA2DP_UIState ui;
    memset(&ui, 0, sizeof(ui));
    ui.device_count = 3;
    ui.selected     = 0;

    /* Device 0: connected SBC headphones */
    oa2dp_profile_defaults(&ui.profiles[0]);
    snprintf(ui.profiles[0].device_id,    sizeof(ui.profiles[0].device_id),    "AA:BB:CC:DD:EE:01");
    snprintf(ui.profiles[0].display_name,  sizeof(ui.profiles[0].display_name),  "WH-1000XM5");
    ui.statuses[0].connection          = OA2DP_CONN_CONNECTED;
    ui.statuses[0].active_codec        = OA2DP_CODEC_SBC;
    ui.statuses[0].sample_rate         = 44100;
    ui.statuses[0].bit_depth           = 16;
    ui.statuses[0].channels            = 2;
    ui.statuses[0].stereo_mode         = OA2DP_STEREO_JOINT;
    ui.statuses[0].block_size          = OA2DP_BLOCK_16;
    ui.statuses[0].allocation_method   = OA2DP_ALLOC_LOUDNESS;
    ui.statuses[0].subbands            = OA2DP_SUBBANDS_8;
    ui.statuses[0].bitpool             = 53;
    ui.statuses[0].estimated_bitrate_kbps = 328;

    /* Device 1: connecting AAC earbuds */
    oa2dp_profile_defaults(&ui.profiles[1]);
    snprintf(ui.profiles[1].device_id,    sizeof(ui.profiles[1].device_id),    "AA:BB:CC:DD:EE:02");
    snprintf(ui.profiles[1].display_name,  sizeof(ui.profiles[1].display_name),  "AirPods Pro");
    ui.profiles[1].preferred_codec = OA2DP_CODEC_AAC;
    ui.statuses[1].connection      = OA2DP_CONN_CONNECTING;
    ui.statuses[1].active_codec    = OA2DP_CODEC_AAC;
    ui.statuses[1].sample_rate     = 48000;
    ui.statuses[1].channels        = 2;

    /* Device 2: disconnected speaker */
    oa2dp_profile_defaults(&ui.profiles[2]);
    snprintf(ui.profiles[2].device_id,    sizeof(ui.profiles[2].device_id),    "AA:BB:CC:DD:EE:03");
    snprintf(ui.profiles[2].display_name,  sizeof(ui.profiles[2].display_name),  "JBL Charge 5");
    ui.statuses[2].connection = OA2DP_CONN_DISCONNECTED;

    oa2dp_log(OA2DP_LOG_INFO, "loaded %d mock devices", ui.device_count);
    oa2dp_log(OA2DP_LOG_DEBUG, "mock device 0: %s (connected, SBC)", ui.profiles[0].display_name);
    oa2dp_log(OA2DP_LOG_DEBUG, "mock device 1: %s (connecting, AAC)", ui.profiles[1].display_name);
    oa2dp_log(OA2DP_LOG_WARN,  "mock device 2: %s (disconnected)", ui.profiles[2].display_name);

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

        oa2dp_panels_draw(&ui);

        oa2dp_renderer_end_frame();
    }

    oa2dp_log(OA2DP_LOG_INFO, "shutting down");
    oa2dp_renderer_shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}
