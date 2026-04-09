/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * hfp_watchdog.c - Periodically re-disable Handsfree (HFP) on opted-in
 * devices to prevent Windows from falling back to narrowband mono SCO.
 *
 * The watchdog has no detection step — it just re-issues the disable
 * call on a fixed cadence.  BluetoothSetServiceState is idempotent and
 * cheap when the service is already off (returns ERROR_NOT_FOUND, which
 * actions.c handles), so the brute approach is fine and avoids the need
 * for a per-service "is currently enabled?" query that Windows does not
 * cleanly expose.
 */

#include "oa2dp_hfp_watchdog.h"
#include "oa2dp_actions.h"
#include "oa2dp_log.h"

void oa2dp_hfp_watchdog_tick(const OA2DP_DeviceList *list)
{
    if (!list) return;

    int fired = 0;

    for (int i = 0; i < list->count; i++) {
        const OA2DP_DeviceProfile *prof = &list->profiles[i];
        const OA2DP_DeviceStatus  *stat = &list->statuses[i];

        if (!prof->hfp_watchdog_enabled)
            continue;
        if (stat->connection != OA2DP_CONN_CONNECTED)
            continue;

        /* Fire-and-forget async disable.  If a manual action is already
         * running, the async call will return -1 and we'll catch this
         * device on the next tick — no need to retry here. */
        if (oa2dp_action_set_handsfree_async(prof->device_id, 0) == 0) {
            oa2dp_log(OA2DP_LOG_DEBUG,
                      "hfp watchdog: re-disabling Handsfree on '%s'",
                      prof->display_name);
            fired++;

            /* The actions module is single-slot — only one outstanding
             * async action across the whole app — so stop after the
             * first successful enqueue and let the next tick get the
             * remaining devices. */
            break;
        }
    }

    if (fired == 0) {
        /* Quiet: no devices needed action this tick. */
    }
}
