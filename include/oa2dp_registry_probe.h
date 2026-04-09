/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_registry_probe.h - One-shot registry diagnostic.
 *
 * Walks a small set of likely locations to find configuration for
 * the Alternative A2DP Driver (and any other third-party A2DP
 * stack), and dumps every value it finds to the log.  Used as
 * reconnaissance before writing real read/write logic — once we
 * know what keys exist and what types they hold, we can build
 * targeted readers/writers.
 *
 * Read-only, safe to run on any system.  No writes are performed.
 */

#ifndef OA2DP_REGISTRY_PROBE_H
#define OA2DP_REGISTRY_PROBE_H

#include "oa2dp_driver_control.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dump everything that looks A2DP-related from the Windows registry
 * to the log at INFO level.
 *
 * Probes:
 *  1. HKLM\SYSTEM\CurrentControlSet\Services\<svc>\Parameters for
 *     each service in the detected drivers list
 *  2. HKLM\SYSTEM\CurrentControlSet\Services\<svc> top-level values
 *     (some drivers store config directly there, not in Parameters)
 *  3. A short list of likely vendor paths under HKLM\SOFTWARE and
 *     HKLM\SOFTWARE\Wow6432Node
 *
 * Recursion is depth-limited and value counts are capped so the
 * log output stays bounded even if a driver has a huge subtree.
 *
 * Returns the number of distinct registry roots that were
 * successfully opened (for sanity checking).
 */
int oa2dp_registry_probe_log(const OA2DP_DriverList *drivers);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_REGISTRY_PROBE_H */
