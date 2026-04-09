/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * driver_control.c - Discover and control A2DP-related Windows services.
 *
 * Replaces the old driver_detect.c.  In addition to scanning the SCM
 * for A2DP-related services and logging them, this module exposes
 * start / stop / refresh APIs so the GUI can switch between A2DP
 * stacks (Microsoft BthA2dp vs Alternative A2DP Driver) for testing.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsvc.h>

#include "oa2dp_driver_control.h"
#include "oa2dp_actions.h"
#include "oa2dp_device_probe.h"
#include "oa2dp_log.h"
#include "oa2dp_stats.h"

#include <stdlib.h>
#include <string.h>

/* ── helpers ────────────────────────────────────────────────────────── */

static int contains_a2dp(const wchar_t *s)
{
    if (!s) return 0;
    for (const wchar_t *p = s; *p && *(p+1) && *(p+2) && *(p+3); p++) {
        if ((p[0] == L'a' || p[0] == L'A') &&
            (p[1] == L'2') &&
            (p[2] == L'd' || p[2] == L'D') &&
            (p[3] == L'p' || p[3] == L'P'))
            return 1;
    }
    return 0;
}

static OA2DP_ServiceState map_state(DWORD scm_state)
{
    switch (scm_state) {
    case SERVICE_STOPPED:          return OA2DP_SVC_STOPPED;
    case SERVICE_START_PENDING:    return OA2DP_SVC_START_PENDING;
    case SERVICE_STOP_PENDING:     return OA2DP_SVC_STOP_PENDING;
    case SERVICE_RUNNING:          return OA2DP_SVC_RUNNING;
    case SERVICE_PAUSED:           return OA2DP_SVC_PAUSED;
    default:                       return OA2DP_SVC_UNKNOWN;
    }
}

/* ── stack inference ────────────────────────────────────────────────── */

static int icontains_ascii(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return 0;
    size_t hlen = strlen(haystack), nlen = strlen(needle);
    if (nlen > hlen) return 0;
    for (size_t i = 0; i + nlen <= hlen; i++) {
        int match = 1;
        for (size_t k = 0; k < nlen; k++) {
            char a = haystack[i + k], b = needle[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

const char *oa2dp_driver_active_stack_label(const OA2DP_DriverList *list,
                                            char *out, int out_size)
{
    if (!out || out_size <= 0) return NULL;
    out[0] = '\0';
    if (!list) { snprintf(out, out_size, "Unknown"); return out; }

    int ms_running  = 0;
    int alt_running = 0;

    for (int i = 0; i < list->count; i++) {
        const OA2DP_A2dpService *s = &list->services[i];
        if (s->state != OA2DP_SVC_RUNNING)
            continue;

        /* "BthA2dp" is the canonical Microsoft stack name; treat any
         * other a2dp-named running service as the alternative. */
        if (icontains_ascii(s->name, "btha2dp"))
            ms_running = 1;
        else
            alt_running = 1;
    }

    if (ms_running && alt_running)
        snprintf(out, out_size, "Multiple stacks running");
    else if (ms_running)
        snprintf(out, out_size, "Microsoft (BthA2dp)");
    else if (alt_running)
        snprintf(out, out_size, "Alternative A2DP Driver");
    else
        snprintf(out, out_size, "No A2DP driver running");
    return out;
}

/* ── elevation check ────────────────────────────────────────────────── */

int oa2dp_process_is_elevated(void)
{
    static int cached = -1;  /* -1 = not yet checked */
    if (cached != -1) return cached;

    cached = 0;
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return cached;

    TOKEN_ELEVATION elevation;
    DWORD bytes = 0;
    if (GetTokenInformation(token, TokenElevation,
                            &elevation, sizeof(elevation), &bytes)) {
        cached = elevation.TokenIsElevated ? 1 : 0;
    }
    CloseHandle(token);
    return cached;
}

const char *oa2dp_driver_state_label(OA2DP_ServiceState s)
{
    switch (s) {
    case OA2DP_SVC_STOPPED:        return "stopped";
    case OA2DP_SVC_START_PENDING:  return "starting";
    case OA2DP_SVC_STOP_PENDING:   return "stopping";
    case OA2DP_SVC_RUNNING:        return "running";
    case OA2DP_SVC_PAUSED:         return "paused";
    default:                       return "unknown";
    }
}

static void wide_to_utf8(const wchar_t *src, char *dst, int dst_size)
{
    if (!src || !dst || dst_size <= 0) return;
    int len = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dst_size, NULL, NULL);
    if (len <= 0) dst[0] = '\0';
}

/* ── scan ───────────────────────────────────────────────────────────── */

int oa2dp_driver_scan(OA2DP_DriverList *list)
{
    if (!list) return -1;
    list->count = 0;

    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) {
        oa2dp_log(OA2DP_LOG_WARN,
                  "driver detect: OpenSCManager failed (err=%lu)",
                  GetLastError());
        return -1;
    }

    DWORD bytes_needed   = 0;
    DWORD services_count = 0;
    DWORD resume_handle  = 0;

    EnumServicesStatusExW(
        scm, SC_ENUM_PROCESS_INFO,
        SERVICE_WIN32 | SERVICE_DRIVER, SERVICE_STATE_ALL,
        NULL, 0, &bytes_needed, &services_count, &resume_handle, NULL);

    DWORD err = GetLastError();
    if (err != ERROR_MORE_DATA && err != ERROR_SUCCESS) {
        oa2dp_log(OA2DP_LOG_WARN,
                  "driver detect: EnumServicesStatusEx (sizing) failed (err=%lu)", err);
        CloseServiceHandle(scm);
        return -1;
    }
    if (bytes_needed == 0) {
        CloseServiceHandle(scm);
        return 0;
    }

    BYTE *buffer = (BYTE *)malloc(bytes_needed);
    if (!buffer) { CloseServiceHandle(scm); return -1; }

    DWORD buffer_size = bytes_needed;
    bytes_needed   = 0;
    services_count = 0;
    resume_handle  = 0;

    for (;;) {
        BOOL ok = EnumServicesStatusExW(
            scm, SC_ENUM_PROCESS_INFO,
            SERVICE_WIN32 | SERVICE_DRIVER, SERVICE_STATE_ALL,
            buffer, buffer_size, &bytes_needed, &services_count,
            &resume_handle, NULL);

        DWORD enum_err = GetLastError();
        if (!ok && enum_err != ERROR_MORE_DATA) {
            oa2dp_log(OA2DP_LOG_WARN,
                      "driver detect: EnumServicesStatusEx failed (err=%lu)",
                      enum_err);
            break;
        }

        ENUM_SERVICE_STATUS_PROCESSW *services =
            (ENUM_SERVICE_STATUS_PROCESSW *)buffer;

        for (DWORD i = 0; i < services_count; i++) {
            const wchar_t *svc_name  = services[i].lpServiceName;
            const wchar_t *disp_name = services[i].lpDisplayName;

            if (!contains_a2dp(svc_name) && !contains_a2dp(disp_name))
                continue;

            if (list->count >= OA2DP_MAX_DRIVER_SERVICES) {
                oa2dp_log(OA2DP_LOG_WARN,
                          "driver detect: hit max %d services, ignoring rest",
                          OA2DP_MAX_DRIVER_SERVICES);
                goto done;
            }

            OA2DP_A2dpService *out = &list->services[list->count];
            memset(out, 0, sizeof(*out));
            wide_to_utf8(svc_name,  out->name,         sizeof(out->name));
            wide_to_utf8(disp_name, out->display_name, sizeof(out->display_name));
            out->state =
                map_state(services[i].ServiceStatusProcess.dwCurrentState);
            out->is_driver =
                (services[i].ServiceStatusProcess.dwServiceType
                    & (SERVICE_KERNEL_DRIVER | SERVICE_FILE_SYSTEM_DRIVER))
                ? 1 : 0;

            oa2dp_log(OA2DP_LOG_INFO,
                      "driver detect: A2DP-related service '%s' (%s) — %s",
                      out->name, out->display_name,
                      oa2dp_driver_state_label(out->state));
            list->count++;
        }

        if (ok) break;
    }

done:
    free(buffer);
    CloseServiceHandle(scm);

    if (list->count == 0) {
        oa2dp_log(OA2DP_LOG_WARN,
                  "driver detect: no A2DP-related services found "
                  "(unusual — Bluetooth audio may not be installed)");
    } else {
        oa2dp_log(OA2DP_LOG_INFO,
                  "driver detect: %d A2DP-related service(s) found",
                  list->count);
    }
    return list->count;
}

/* ── refresh single entry ───────────────────────────────────────────── */

int oa2dp_driver_refresh(OA2DP_DriverList *list, int index)
{
    if (!list || index < 0 || index >= list->count) return -1;
    OA2DP_A2dpService *svc = &list->services[index];

    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return -1;

    wchar_t wname[64];
    MultiByteToWideChar(CP_UTF8, 0, svc->name, -1, wname, 64);

    SC_HANDLE h = OpenServiceW(scm, wname, SERVICE_QUERY_STATUS);
    if (!h) { CloseServiceHandle(scm); return -1; }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytes = 0;
    if (QueryServiceStatusEx(h, SC_STATUS_PROCESS_INFO,
                             (LPBYTE)&ssp, sizeof(ssp), &bytes)) {
        svc->state = map_state(ssp.dwCurrentState);
    }
    CloseServiceHandle(h);
    CloseServiceHandle(scm);
    return 0;
}

/* ── start / stop ───────────────────────────────────────────────────── */

#define WAIT_TOTAL_MS  10000
#define WAIT_POLL_MS   250

/*
 * Wait until the service reaches one of the target states or the
 * total timeout elapses.  Returns the final state's mapped value.
 */
static OA2DP_ServiceState wait_for_state(SC_HANDLE h, DWORD target1, DWORD target2)
{
    DWORD waited = 0;
    SERVICE_STATUS_PROCESS ssp;
    DWORD bytes = 0;

    while (waited < WAIT_TOTAL_MS) {
        if (!QueryServiceStatusEx(h, SC_STATUS_PROCESS_INFO,
                                  (LPBYTE)&ssp, sizeof(ssp), &bytes))
            return OA2DP_SVC_UNKNOWN;
        if (ssp.dwCurrentState == target1 || ssp.dwCurrentState == target2)
            return map_state(ssp.dwCurrentState);
        Sleep(WAIT_POLL_MS);
        waited += WAIT_POLL_MS;
    }
    return map_state(ssp.dwCurrentState);
}

static void log_access_hint(const char *verb, const char *name, DWORD err)
{
    if (err == ERROR_ACCESS_DENIED) {
        oa2dp_log(OA2DP_LOG_ERROR,
                  "driver control: %s '%s' DENIED — relaunch OpenA2DP "
                  "as Administrator to control services",
                  verb, name);
    } else {
        oa2dp_log(OA2DP_LOG_ERROR,
                  "driver control: %s '%s' failed (err=%lu)",
                  verb, name, err);
    }
}

int oa2dp_driver_start(const char *service_name)
{
    if (!service_name || !service_name[0]) return -1;

    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) {
        log_access_hint("open SCM for", service_name, GetLastError());
        return -1;
    }

    wchar_t wname[64];
    MultiByteToWideChar(CP_UTF8, 0, service_name, -1, wname, 64);

    SC_HANDLE h = OpenServiceW(scm, wname,
                               SERVICE_START | SERVICE_QUERY_STATUS);
    if (!h) {
        log_access_hint("open", service_name, GetLastError());
        CloseServiceHandle(scm);
        return -1;
    }

    oa2dp_log(OA2DP_LOG_INFO, "driver control: starting '%s'", service_name);

    if (!StartServiceW(h, 0, NULL)) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_ALREADY_RUNNING) {
            oa2dp_log(OA2DP_LOG_INFO,
                      "driver control: '%s' was already running",
                      service_name);
            CloseServiceHandle(h);
            CloseServiceHandle(scm);
            return 0;
        }
        log_access_hint("start", service_name, err);
        CloseServiceHandle(h);
        CloseServiceHandle(scm);
        return -1;
    }

    OA2DP_ServiceState final_state =
        wait_for_state(h, SERVICE_RUNNING, SERVICE_STOPPED);

    CloseServiceHandle(h);
    CloseServiceHandle(scm);

    if (final_state == OA2DP_SVC_RUNNING) {
        oa2dp_log(OA2DP_LOG_INFO, "driver control: '%s' is now running",
                  service_name);
        return 0;
    }
    oa2dp_log(OA2DP_LOG_WARN,
              "driver control: '%s' did not reach running state (now %s)",
              service_name, oa2dp_driver_state_label(final_state));
    return -1;
}

int oa2dp_driver_stop(const char *service_name)
{
    if (!service_name || !service_name[0]) return -1;

    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) {
        log_access_hint("open SCM for", service_name, GetLastError());
        return -1;
    }

    wchar_t wname[64];
    MultiByteToWideChar(CP_UTF8, 0, service_name, -1, wname, 64);

    SC_HANDLE h = OpenServiceW(scm, wname,
                               SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!h) {
        log_access_hint("open", service_name, GetLastError());
        CloseServiceHandle(scm);
        return -1;
    }

    oa2dp_log(OA2DP_LOG_INFO, "driver control: stopping '%s'", service_name);

    SERVICE_STATUS_PROCESS ssp;
    if (!ControlService(h, SERVICE_CONTROL_STOP, (LPSERVICE_STATUS)&ssp)) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_NOT_ACTIVE) {
            oa2dp_log(OA2DP_LOG_INFO,
                      "driver control: '%s' was already stopped",
                      service_name);
            CloseServiceHandle(h);
            CloseServiceHandle(scm);
            return 0;
        }
        log_access_hint("stop", service_name, err);
        CloseServiceHandle(h);
        CloseServiceHandle(scm);
        return -1;
    }

    OA2DP_ServiceState final_state =
        wait_for_state(h, SERVICE_STOPPED, SERVICE_RUNNING);

    CloseServiceHandle(h);
    CloseServiceHandle(scm);

    if (final_state == OA2DP_SVC_STOPPED) {
        oa2dp_log(OA2DP_LOG_INFO, "driver control: '%s' is now stopped",
                  service_name);
        return 0;
    }
    oa2dp_log(OA2DP_LOG_WARN,
              "driver control: '%s' did not reach stopped state (now %s)",
              service_name, oa2dp_driver_state_label(final_state));
    return -1;
}

/* ── stack switch worker ────────────────────────────────────────────── */

static volatile LONG g_switch_busy = 0;

int oa2dp_stack_switch_busy(void)
{
    return (int)g_switch_busy;
}

typedef struct {
    OA2DP_StackTarget  target;
    OA2DP_DriverList  *drivers;
    OA2DP_DeviceList  *devices;
} SwitchParam;

/* Returns 1 if `name` is the Microsoft BthA2dp driver. */
static int is_microsoft_service(const char *name)
{
    return icontains_ascii(name, "btha2dp");
}

/*
 * Core stack-switch routine.  Caller is responsible for the busy
 * flag — async wrapper sets it before calling, CLI sync wrapper sets
 * it before calling.  Both clear it after.
 */
static void do_stack_switch(OA2DP_StackTarget target,
                            OA2DP_DriverList *drivers,
                            OA2DP_DeviceList *devices)
{
    const char *target_name =
        (target == OA2DP_STACK_MICROSOFT)
            ? "Microsoft (BthA2dp)" : "Alternative A2DP Driver";

    oa2dp_log(OA2DP_LOG_INFO, "stack switch: starting → %s", target_name);
    oa2dp_stats_inc_stack_switch();

    /* Step 1: stop services that don't belong to the target. */
    for (int i = 0; i < drivers->count; i++) {
        OA2DP_A2dpService *svc = &drivers->services[i];
        int wants_running =
            (target == OA2DP_STACK_MICROSOFT)
                ? is_microsoft_service(svc->name)
                : !is_microsoft_service(svc->name);

        if (!wants_running && svc->state == OA2DP_SVC_RUNNING) {
            oa2dp_driver_stop(svc->name);
            oa2dp_driver_refresh(drivers, i);
        }
    }

    /* Step 2: start services that do belong to the target. */
    for (int i = 0; i < drivers->count; i++) {
        OA2DP_A2dpService *svc = &drivers->services[i];
        int wants_running =
            (target == OA2DP_STACK_MICROSOFT)
                ? is_microsoft_service(svc->name)
                : !is_microsoft_service(svc->name);

        if (wants_running && svc->state != OA2DP_SVC_RUNNING) {
            oa2dp_driver_start(svc->name);
            oa2dp_driver_refresh(drivers, i);
        }
    }

    /* Step 3: reconnect every connected device so it re-binds. */
    if (devices) {
        for (int i = 0; i < devices->count; i++) {
            const OA2DP_DeviceProfile *prof = &devices->profiles[i];
            const OA2DP_DeviceStatus  *stat = &devices->statuses[i];
            if (stat->connection != OA2DP_CONN_CONNECTED)
                continue;

            oa2dp_log(OA2DP_LOG_INFO,
                      "stack switch: reconnecting '%s' on new stack",
                      prof->display_name);
            oa2dp_action_reconnect(prof->device_id);
        }
    }

    /* Refresh installed-services flags / battery / Alt A2DP snapshot
     * for every device against the new stack so the UI doesn't show
     * stale data after the switch. */
    if (devices)
        oa2dp_device_probe_start(devices);

    oa2dp_log(OA2DP_LOG_INFO, "stack switch: complete (%s)", target_name);
}

static DWORD WINAPI switch_thread(LPVOID param)
{
    SwitchParam *p = (SwitchParam *)param;
    do_stack_switch(p->target, p->drivers, p->devices);
    free(p);
    InterlockedExchange(&g_switch_busy, 0);
    return 0;
}

int oa2dp_stack_switch_async(OA2DP_StackTarget target,
                             OA2DP_DriverList *drivers,
                             OA2DP_DeviceList *devices)
{
    if (!drivers) return -1;

    if (InterlockedCompareExchange(&g_switch_busy, 1, 0) != 0) {
        oa2dp_log(OA2DP_LOG_WARN,
                  "stack switch: already in progress, ignoring request");
        return -1;
    }

    SwitchParam *p = (SwitchParam *)malloc(sizeof(*p));
    if (!p) {
        InterlockedExchange(&g_switch_busy, 0);
        return -1;
    }
    p->target  = target;
    p->drivers = drivers;
    p->devices = devices;

    HANDLE h = CreateThread(NULL, 0, switch_thread, p, 0, NULL);
    if (!h) {
        oa2dp_log(OA2DP_LOG_ERROR,
                  "stack switch: CreateThread failed (err=%lu)", GetLastError());
        free(p);
        InterlockedExchange(&g_switch_busy, 0);
        return -1;
    }
    CloseHandle(h);
    return 0;
}

int oa2dp_stack_switch_sync(OA2DP_StackTarget target,
                            OA2DP_DriverList *drivers,
                            OA2DP_DeviceList *devices)
{
    if (!drivers) return -1;

    if (InterlockedCompareExchange(&g_switch_busy, 1, 0) != 0) {
        oa2dp_log(OA2DP_LOG_WARN,
                  "stack switch: already in progress, ignoring request");
        return -1;
    }

    do_stack_switch(target, drivers, devices);
    InterlockedExchange(&g_switch_busy, 0);
    return 0;
}
