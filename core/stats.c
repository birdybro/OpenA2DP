/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * stats.c - In-memory activity counters.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "oa2dp_stats.h"

#include <string.h>

static OA2DP_Stats g_stats;

void oa2dp_stats_init(void)
{
    memset((void *)&g_stats, 0, sizeof(g_stats));
}

const OA2DP_Stats *oa2dp_stats_get(void)
{
    return &g_stats;
}

void oa2dp_stats_inc_reconnect(void)    { InterlockedIncrement(&g_stats.reconnects); }
void oa2dp_stats_inc_heal_trigger(void) { InterlockedIncrement(&g_stats.heal_triggers); }
void oa2dp_stats_inc_heal_recovery(void){ InterlockedIncrement(&g_stats.heal_recoveries); }
void oa2dp_stats_inc_heal_failure(void) { InterlockedIncrement(&g_stats.heal_failures); }
void oa2dp_stats_inc_hfp_watchdog(void) { InterlockedIncrement(&g_stats.hfp_watchdog); }
void oa2dp_stats_inc_stack_switch(void) { InterlockedIncrement(&g_stats.stack_switches); }
