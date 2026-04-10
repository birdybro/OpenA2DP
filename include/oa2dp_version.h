/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_version.h - Single source of truth for the version number.
 *
 * Included by both scripts/version_{gui,cli}.rc (resource compiler)
 * AND by C source files that need the version at runtime
 * (e.g. the update checker).  When bumping the version, edit ONLY
 * this file and add a CHANGELOG.md entry — the .rc files pick up
 * the macros automatically.
 */

#ifndef OA2DP_VERSION_H
#define OA2DP_VERSION_H

#define OA2DP_VER_MAJOR 0
#define OA2DP_VER_MINOR 6
#define OA2DP_VER_PATCH 0
#define OA2DP_VER_BUILD 0

#define OA2DP_VER_STRING "0.6.0"

#endif /* OA2DP_VERSION_H */
