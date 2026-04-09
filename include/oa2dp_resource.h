/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_resource.h - Win32 resource IDs shared between the C source
 * and the .rc files.  Both `cl.exe` (compiling .c) and `rc.exe`
 * (compiling .rc) understand `#define`, so this header can be
 * included from either side and the IDs stay in sync automatically.
 */

#ifndef OA2DP_RESOURCE_H
#define OA2DP_RESOURCE_H

/* Application icon embedded into both binaries via version_*.rc.
 * Loaded by main.c for the window class (taskbar/title-bar) and by
 * tray.c for the system-tray notification icon. */
#define IDI_APP_ICON 101

#endif /* OA2DP_RESOURCE_H */
