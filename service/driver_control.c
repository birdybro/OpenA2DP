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
#include "oa2dp_log.h"

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
