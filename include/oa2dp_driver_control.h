/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_driver_control.h - Discover and control A2DP-related Windows
 * services so the user can switch between different A2DP stacks
 * (e.g. the Microsoft BthA2dp driver vs the Alternative A2DP Driver
 * services from bluetoothgoodies.com).
 *
 * All operations go through the Service Control Manager.  Querying
 * service state works as a normal user, but starting and stopping
 * services requires the process to be elevated (Run as Administrator).
 * Non-admin start/stop attempts return -1 and log a clear ACCESS_DENIED
 * message rather than failing silently.
 */

#ifndef OA2DP_DRIVER_CONTROL_H
#define OA2DP_DRIVER_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#define OA2DP_MAX_DRIVER_SERVICES 16

typedef enum OA2DP_ServiceState {
    OA2DP_SVC_UNKNOWN = 0,
    OA2DP_SVC_STOPPED,
    OA2DP_SVC_START_PENDING,
    OA2DP_SVC_STOP_PENDING,
    OA2DP_SVC_RUNNING,
    OA2DP_SVC_PAUSED
} OA2DP_ServiceState;

typedef struct OA2DP_A2dpService {
    char name[64];          /* SCM service name, e.g. "BthA2dp" */
    char display_name[128]; /* SCM display name */
    OA2DP_ServiceState state;
    int  is_driver;         /* 1 = kernel driver, 0 = win32 service */
} OA2DP_A2dpService;

typedef struct OA2DP_DriverList {
    OA2DP_A2dpService services[OA2DP_MAX_DRIVER_SERVICES];
    int               count;
} OA2DP_DriverList;

/*
 * Enumerate the SCM looking for any service or kernel driver whose
 * name or display name contains "a2dp" (case-insensitive) and fill
 * the list.  Also logs each match at INFO so it shows in the startup
 * log.
 *
 * Returns the number of matches, or -1 on error.
 */
int oa2dp_driver_scan(OA2DP_DriverList *list);

/*
 * Re-query the live state of one entry in the list (by index).
 * Returns 0 on success, -1 on error.
 */
int oa2dp_driver_refresh(OA2DP_DriverList *list, int index);

/*
 * Start or stop a service by name.  Synchronous (waits for the state
 * transition or a short timeout).  Logs at INFO on success and
 * ERROR/WARN on failure.  When the failure is ERROR_ACCESS_DENIED
 * (the common case for non-admin processes), the log message
 * explicitly tells the user to relaunch as administrator.
 *
 * Returns 0 on success, -1 on failure.
 */
int oa2dp_driver_start(const char *service_name);
int oa2dp_driver_stop(const char *service_name);

/* Map state enum to short label for UI display. */
const char *oa2dp_driver_state_label(OA2DP_ServiceState state);

/*
 * Returns 1 if the current process is running with elevated
 * (Administrator) privileges, 0 otherwise.  Cached after the first
 * call — process elevation can't change for the lifetime of the
 * process, so this is safe to call every frame.
 */
int oa2dp_process_is_elevated(void);

typedef enum OA2DP_StackTarget {
    OA2DP_STACK_MICROSOFT = 0,
    OA2DP_STACK_ALTERNATIVE
} OA2DP_StackTarget;

#include "oa2dp_device.h"

/*
 * Switch the active A2DP stack on a background thread.
 *
 * The worker performs three steps in order:
 *   1. Stop every running service in `drivers` that doesn't belong to
 *      the target stack.
 *   2. Start every stopped service in `drivers` that does.
 *   3. For each connected device in `devices`, run a synchronous
 *      reconnect cycle so it re-binds to the new stack.
 *
 * Single-slot — only one switch can be in flight at a time.  Returns
 * 0 if the worker was launched, -1 if busy or on error.  All progress
 * is logged via oa2dp_log so the user can watch it from the log panel.
 *
 * Caller's `drivers` and `devices` pointers must remain valid for the
 * lifetime of the worker.  In OpenA2DP they live in g_ui which is
 * static for the process lifetime, so this is fine in practice.
 */
int oa2dp_stack_switch_async(OA2DP_StackTarget target,
                             OA2DP_DriverList *drivers,
                             OA2DP_DeviceList *devices);

/* Returns 1 if a stack switch worker is currently running. */
int oa2dp_stack_switch_busy(void);

/*
 * Infer which A2DP stack is currently handling Bluetooth audio on
 * this machine, based on which services in the list are running.
 *
 * Heuristic (machine-wide, not per device — Windows doesn't expose
 * which stack handles a specific endpoint in user mode):
 *
 *   BthA2dp running, no Alt running    -> "Microsoft"
 *   AltA2dp* running, BthA2dp stopped  -> "Alternative A2DP Driver"
 *   Both running                       -> "Multiple"
 *   Neither running                    -> "None"
 *
 * Writes a short label into out (caller buffer).  Returns the same
 * pointer for convenience.
 */
const char *oa2dp_driver_active_stack_label(const OA2DP_DriverList *list,
                                            char *out, int out_size);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_DRIVER_CONTROL_H */
