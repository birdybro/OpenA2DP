/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_stats.h - Lightweight in-memory activity counters.
 *
 * Tracks how often the various background actions actually fire so
 * the user can tell at a glance whether the watchdogs are working
 * and how often the recovery paths get exercised.  Counters are
 * not persisted across runs by design — they show *this session's*
 * activity, which is the question users actually ask ("did anything
 * just happen?").
 *
 * All increments use InterlockedIncrement so they're safe to call
 * from worker threads (auto_heal, hfp_watchdog, stack switch, etc).
 */

#ifndef OA2DP_STATS_H
#define OA2DP_STATS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OA2DP_Stats {
    long reconnects;       /* manual or auto reconnect calls completed */
    long heal_triggers;    /* auto-heal worker invocations */
    long heal_recoveries;  /* auto-heal that actually recovered an endpoint */
    long heal_failures;    /* auto-heal that gave up after max attempts */
    long hfp_watchdog;     /* HFP watchdog disable-handsfree fires */
    long stack_switches;   /* stack switch worker invocations */
} OA2DP_Stats;

/* Reset all counters to zero (called once at startup). */
void oa2dp_stats_init(void);

/* Read-only snapshot for the UI. */
const OA2DP_Stats *oa2dp_stats_get(void);

/* Increments — safe from any thread. */
void oa2dp_stats_inc_reconnect(void);
void oa2dp_stats_inc_heal_trigger(void);
void oa2dp_stats_inc_heal_recovery(void);
void oa2dp_stats_inc_heal_failure(void);
void oa2dp_stats_inc_hfp_watchdog(void);
void oa2dp_stats_inc_stack_switch(void);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_STATS_H */
