/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * main.c - Win32 entry point, window creation, and message loop
 */

#if !defined(_WIN32)
#  error "OpenA2DP only builds on Windows. Linux and macOS are not supported."
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <dbt.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "renderer.h"
#include "panels.h"
#include "oa2dp_cli.h"
#include "oa2dp_device.h"
#include "oa2dp_audio_status.h"
#include "oa2dp_config.h"
#include "oa2dp_driver_control.h"
#include "oa2dp_hfp_watchdog.h"
#include "oa2dp_log.h"
#include "oa2dp_tray.h"

#include <string.h>

/* Forward declaration — we need the UI state in WndProc for device changes. */
static OA2DP_UIState g_ui;
static int g_rescan_needed = 0;

/* Snapshot of profiles for dirty detection. */
static OA2DP_DeviceProfile g_saved_profiles[OA2DP_MAX_DEVICES];
static int g_saved_count = 0;

static void snapshot_profiles(void)
{
    g_saved_count = g_ui.devices.count;
    memcpy(g_saved_profiles, g_ui.devices.profiles,
           sizeof(OA2DP_DeviceProfile) * g_saved_count);
}

/* Save profiles for new devices that have no file on disk yet. */
static void save_new_profiles(void)
{
    for (int i = 0; i < g_ui.devices.count; i++) {
        OA2DP_DeviceProfile *cur = &g_ui.devices.profiles[i];
        char path[260];
        if (oa2dp_config_path_for_device(cur->device_id, path, sizeof(path)) != 0)
            continue;

        FILE *f = fopen(path, "r");
        if (f) {
            fclose(f);  /* file exists, skip */
            continue;
        }

        if (oa2dp_profile_save(path, cur) == 0)
            oa2dp_log(OA2DP_LOG_INFO, "saved initial profile for '%s'",
                      cur->display_name);
    }
}

static void save_dirty_profiles(void)
{
    for (int i = 0; i < g_ui.devices.count; i++) {
        OA2DP_DeviceProfile *cur = &g_ui.devices.profiles[i];

        /* Check if this profile differs from the snapshot. */
        int dirty = 0;
        if (i >= g_saved_count) {
            dirty = 1;
        } else if (memcmp(cur, &g_saved_profiles[i], sizeof(*cur)) != 0) {
            dirty = 1;
        }

        if (dirty) {
            char path[260];
            if (oa2dp_config_path_for_device(cur->device_id, path, sizeof(path)) == 0) {
                if (oa2dp_profile_save(path, cur) == 0)
                    oa2dp_log(OA2DP_LOG_DEBUG, "auto-saved profile for '%s'",
                              cur->display_name);
            }
        }
    }
    snapshot_profiles();
}

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
        /* Hide to tray on minimize instead of taskbar-minimizing.
         * The user can restore via the tray icon (double-click) or
         * the tray menu's "Show OpenA2DP". */
        if ((wparam & 0xFFF0) == SC_MINIMIZE) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;
    case WM_DEVICECHANGE:
        if (wparam == DBT_DEVICEARRIVAL || wparam == DBT_DEVICEREMOVECOMPLETE) {
            oa2dp_log(OA2DP_LOG_INFO, "Bluetooth device change detected");
            g_rescan_needed = 1;
        }
        return 0;
    case OA2DP_WM_TRAY:
        oa2dp_tray_handle_message(hwnd, &g_ui,
                                  (unsigned int)wparam, (long)lparam);
        return 0;
    case WM_COMMAND:
        if (oa2dp_tray_owns_command(LOWORD(wparam))) {
            oa2dp_tray_handle_command(hwnd, &g_ui, LOWORD(wparam));
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* ── Entry point ────────────────────────────────────────────────────
 *
 * The binary is built with /SUBSYSTEM:CONSOLE so that cmd.exe properly
 * waits for us when invoked in CLI mode (--reconnect, --list-devices,
 * etc).  /SUBSYSTEM:WINDOWS does not — cmd returns to its prompt
 * immediately and any console output the GUI exe writes back gets
 * interleaved with the new prompt, making CLI mode unusable.
 *
 * The downside of CONSOLE subsystem is that launching from Explorer
 * normally allocates a fresh console window for the process.  We
 * detect that (GetConsoleProcessList == 1) and hide+detach the
 * console before any visible work begins, so a normal GUI launch
 * doesn't show a stray cmd window.
 *
 * Console launches inherit the parent console (count > 1), in which
 * case we leave it alone — that's exactly the case where we want
 * stdout to flow to the user's terminal.
 */
int wmain(int argc, wchar_t **argv)
{
    HINSTANCE hInstance = GetModuleHandleW(NULL);
    int nCmdShow = SW_SHOW;

    oa2dp_log_init();

    /* ── CLI mode short-circuit ─────────────────────────────────────
     * If the user passed a recognised --command argument, run that
     * action headlessly and exit without touching D3D11, ImGui, or
     * the device scan UI plumbing.  Console is already attached, so
     * stdout/stderr just work. */
    if (oa2dp_cli_is_cli_invocation(argc, argv))
        return oa2dp_cli_run(argc, argv);

    /* GUI mode — hide and detach the console if we got a fresh one
     * (i.e. launched from Explorer or another GUI app).  If a parent
     * console exists (launched from cmd or PowerShell) leave it
     * alone so log output still has somewhere to go in principle. */
    {
        DWORD console_pids[2];
        DWORD console_count = GetConsoleProcessList(console_pids, 2);
        if (console_count == 1) {
            HWND console = GetConsoleWindow();
            if (console) ShowWindow(console, SW_HIDE);
            FreeConsole();
        }
    }

    oa2dp_log(OA2DP_LOG_INFO, "OpenA2DP starting");

    /* Register window class. */
    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance      = hInstance;
    wc.lpszClassName  = L"OpenA2DP";
    RegisterClassExW(&wc);

    /* Config dir is needed for window state load — initialize it
     * before CreateWindow even though it's logically a "later" step. */
    if (oa2dp_config_init() != 0)
        oa2dp_log(OA2DP_LOG_WARN, "config init failed, profiles will not persist");

    /* Restore previous window placement if available. */
    int win_x = 100, win_y = 100, win_w = 1440, win_h = 900;
    oa2dp_window_state_load(&win_x, &win_y, &win_w, &win_h);

    /* Create window. */
    HWND hwnd = CreateWindowW(
        wc.lpszClassName, L"OpenA2DP",
        WS_OVERLAPPEDWINDOW,
        win_x, win_y, win_w, win_h,
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

    /* ── Audio status subsystem ─────────────────────────────────── */
    if (oa2dp_audio_status_init() != 0)
        oa2dp_log(OA2DP_LOG_WARN, "audio status init failed, endpoint data unavailable");

    /* ── Device enumeration ─────────────────────────────────────── */
    oa2dp_ui_state_init(&g_ui);

    /* ── A2DP driver detection ──────────────────────────────────
     * Populates g_ui.drivers so the UI panel can render and control
     * them.  Also logs each match at INFO level for the issue dump. */
    oa2dp_log(OA2DP_LOG_INFO,
              "process: running %s",
              oa2dp_process_is_elevated()
                  ? "elevated (Administrator)"
                  : "non-elevated (service control disabled)");
    oa2dp_driver_scan(&g_ui.drivers);

    oa2dp_device_scan(&g_ui.devices);
    save_new_profiles();
    snapshot_profiles();

    if (g_ui.devices.count == 0)
        oa2dp_log(OA2DP_LOG_WARN, "no Bluetooth audio devices found");

    /* Register for device change notifications. */
    oa2dp_device_register_notify(hwnd);

    /* System tray icon — adds OpenA2DP to the notification area with
     * a right-click menu of common recovery actions. */
    oa2dp_tray_init(hwnd);

    /* Timers. */
    DWORD last_refresh  = GetTickCount();
    DWORD last_save     = GetTickCount();
    DWORD last_watchdog = GetTickCount();
    const DWORD REFRESH_INTERVAL_MS = 2000;
    const DWORD SAVE_INTERVAL_MS    = 3000;

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
            save_dirty_profiles();
            int prev_count = g_ui.devices.count;
            oa2dp_device_scan(&g_ui.devices);
            save_new_profiles();
            snapshot_profiles();
            if (g_ui.selected >= g_ui.devices.count)
                g_ui.selected = (g_ui.devices.count > 0) ? 0 : -1;
            if (g_ui.devices.count != prev_count)
                oa2dp_log(OA2DP_LOG_INFO, "device list updated: %d device(s)",
                          g_ui.devices.count);
        }

        /* Periodic status refresh + profile save. */
        DWORD now = GetTickCount();
        if (now - last_refresh >= REFRESH_INTERVAL_MS) {
            oa2dp_device_refresh_status(&g_ui.devices);
            last_refresh = now;
        }
        if (now - last_save >= SAVE_INTERVAL_MS) {
            save_dirty_profiles();
            last_save = now;
        }
        if (now - last_watchdog >= OA2DP_HFP_WATCHDOG_INTERVAL_MS) {
            oa2dp_hfp_watchdog_tick(&g_ui.devices);
            last_watchdog = now;
        }

        if (!oa2dp_renderer_begin_frame())
            continue;

        oa2dp_panels_draw(&g_ui);

        oa2dp_renderer_end_frame();
    }

    oa2dp_log(OA2DP_LOG_INFO, "shutting down");
    save_dirty_profiles();

    /* Persist window placement.  GetWindowPlacement gives the
     * "normal" rect even when the window is currently minimized
     * or hidden to tray, which is exactly what we want to restore. */
    {
        WINDOWPLACEMENT wp = {0};
        wp.length = sizeof(wp);
        if (GetWindowPlacement(hwnd, &wp)) {
            RECT *r = &wp.rcNormalPosition;
            oa2dp_window_state_save(r->left, r->top,
                                    r->right - r->left,
                                    r->bottom - r->top);
        }
    }

    oa2dp_tray_shutdown();
    oa2dp_device_unregister_notify();
    oa2dp_audio_status_shutdown();
    oa2dp_renderer_shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}
