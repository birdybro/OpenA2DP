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

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_DRIVER_CONTROL_H */
