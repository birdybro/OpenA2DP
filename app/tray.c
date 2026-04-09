/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * tray.c - System tray icon and dynamic popup menu.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include "oa2dp_tray.h"
#include "oa2dp_actions.h"
#include "oa2dp_driver_control.h"
#include "oa2dp_log.h"

#include <stdio.h>
#include <string.h>

/* ── state ──────────────────────────────────────────────────────────── */

static NOTIFYICONDATAW g_nid = {0};
static int g_added = 0;

/* ── add / remove icon ──────────────────────────────────────────────── */

int oa2dp_tray_init(void *hwnd_void)
{
    HWND hwnd = (HWND)hwnd_void;

    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = OA2DP_WM_TRAY;
    /* Default app icon — good enough until we ship a custom one. */
    g_nid.hIcon            = LoadIconW(NULL, IDI_APPLICATION);
    wcsncpy_s(g_nid.szTip, ARRAYSIZE(g_nid.szTip),
              L"OpenA2DP", _TRUNCATE);

    if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        oa2dp_log(OA2DP_LOG_WARN, "tray: Shell_NotifyIcon NIM_ADD failed (err=%lu)",
                  GetLastError());
        return -1;
    }
    g_added = 1;
    oa2dp_log(OA2DP_LOG_INFO, "tray: icon added");
    return 0;
}

void oa2dp_tray_shutdown(void)
{
    if (g_added) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_added = 0;
    }
}

/* ── window show / hide ─────────────────────────────────────────────── */

void oa2dp_tray_toggle_window(void *hwnd_void)
{
    HWND hwnd = (HWND)hwnd_void;
    if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_HIDE);
    } else {
        ShowWindow(hwnd, SW_SHOW);
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
    }
}

static void show_window(HWND hwnd)
{
    ShowWindow(hwnd, SW_SHOW);
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

/* ── popup menu ─────────────────────────────────────────────────────── */

static void show_popup_menu(HWND hwnd, OA2DP_UIState *ui)
{
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
        AppendMenuW(menu, MF_STRING, OA2DP_TRAY_ID_HIDE, L"Hide window");
    } else {
        AppendMenuW(menu, MF_STRING, OA2DP_TRAY_ID_SHOW, L"Show OpenA2DP");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);

    /* Per-device submenu(s).  We're capped to 256 / TRAY_DEV_SLOTS
     * device entries — far more than any sane Bluetooth setup. */
    int dev_count = ui ? ui->devices.count : 0;
    if (dev_count == 0) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"(no devices)");
    } else {
        for (int i = 0; i < dev_count && i < 16; i++) {
            const OA2DP_DeviceProfile *p = &ui->devices.profiles[i];
            const OA2DP_DeviceStatus  *s = &ui->devices.statuses[i];

            HMENU sub = CreatePopupMenu();

            UINT flags = (s->connection == OA2DP_CONN_CONNECTED)
                             ? MF_STRING : (MF_STRING | MF_GRAYED);

            int recon_id = OA2DP_TRAY_ID_DEV_BASE + i * OA2DP_TRAY_DEV_SLOTS + 0;
            int hfp_id   = OA2DP_TRAY_ID_DEV_BASE + i * OA2DP_TRAY_DEV_SLOTS + 1;

            AppendMenuW(sub, flags, recon_id, L"Reconnect");
            AppendMenuW(sub, flags, hfp_id,   L"Disable HFP");

            wchar_t label[256];
            MultiByteToWideChar(CP_UTF8, 0, p->display_name, -1, label, 256);
            AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub, label);
        }
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);

    /* Stack switcher (always visible, but disabled when not elevated
     * or already mid-switch). */
    UINT switch_flags = MF_STRING;
    if (!oa2dp_process_is_elevated() || oa2dp_stack_switch_busy())
        switch_flags |= MF_GRAYED;
    AppendMenuW(menu, switch_flags, OA2DP_TRAY_ID_SWITCH_MS,
                L"Switch to Microsoft stack");
    AppendMenuW(menu, switch_flags, OA2DP_TRAY_ID_SWITCH_ALT,
                L"Switch to Alternative A2DP Driver");

    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, OA2DP_TRAY_ID_QUIT, L"Quit");

    /* TrackPopupMenu requires the owner window to be foreground or
     * the menu can refuse to dismiss properly. */
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu,
                   TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   pt.x, pt.y, 0, hwnd, NULL);
    /* Standard fix-up so the menu disappears reliably. */
    PostMessage(hwnd, WM_NULL, 0, 0);

    DestroyMenu(menu);
}

/* ── message routing ────────────────────────────────────────────────── */

void oa2dp_tray_handle_message(void *hwnd_void, OA2DP_UIState *ui,
                               unsigned int wparam, long lparam)
{
    (void)wparam;
    HWND hwnd = (HWND)hwnd_void;

    UINT event = (UINT)LOWORD(lparam);
    switch (event) {
    case WM_LBUTTONDBLCLK:
        oa2dp_tray_toggle_window(hwnd);
        break;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        show_popup_menu(hwnd, ui);
        break;
    default:
        break;
    }
}

void oa2dp_tray_handle_command(void *hwnd_void, OA2DP_UIState *ui, int cmd_id)
{
    HWND hwnd = (HWND)hwnd_void;

    switch (cmd_id) {
    case OA2DP_TRAY_ID_SHOW:
        show_window(hwnd);
        return;
    case OA2DP_TRAY_ID_HIDE:
        ShowWindow(hwnd, SW_HIDE);
        return;
    case OA2DP_TRAY_ID_SWITCH_MS:
        oa2dp_stack_switch_async(OA2DP_STACK_MICROSOFT,
                                 &ui->drivers, &ui->devices);
        return;
    case OA2DP_TRAY_ID_SWITCH_ALT:
        oa2dp_stack_switch_async(OA2DP_STACK_ALTERNATIVE,
                                 &ui->drivers, &ui->devices);
        return;
    case OA2DP_TRAY_ID_QUIT:
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        return;
    default:
        break;
    }

    /* Per-device commands. */
    if (cmd_id >= OA2DP_TRAY_ID_DEV_BASE &&
        cmd_id <  OA2DP_TRAY_ID_DEV_BASE + 0x100) {
        int rel  = cmd_id - OA2DP_TRAY_ID_DEV_BASE;
        int idx  = rel / OA2DP_TRAY_DEV_SLOTS;
        int slot = rel % OA2DP_TRAY_DEV_SLOTS;
        if (idx < 0 || idx >= ui->devices.count)
            return;

        const OA2DP_DeviceProfile *p = &ui->devices.profiles[idx];
        if (slot == 0) {
            oa2dp_action_reconnect_async(p->device_id);
        } else if (slot == 1) {
            oa2dp_action_set_handsfree_async(p->device_id, 0);
        }
    }
}
