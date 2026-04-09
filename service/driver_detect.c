/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * driver_detect.c - Detect installed A2DP-related drivers/services.
 *
 * Walks the Service Control Manager looking for any service or kernel
 * driver whose name or display name contains the substring "a2dp"
 * (case-insensitive).  Used purely to surface what stack the user
 * has installed in the startup log — there is no control surface here.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsvc.h>

#include "oa2dp_driver_detect.h"
#include "oa2dp_log.h"

#include <stdlib.h>
#include <string.h>

/* ── helpers ────────────────────────────────────────────────────────── */

/* Case-insensitive substring search.  ASCII-only — service names are
 * always ASCII so this is fine. */
static int contains_a2dp(const wchar_t *s)
{
    if (!s) return 0;
    for (const wchar_t *p = s; *p; p++) {
        if ((p[0] == L'a' || p[0] == L'A') &&
            (p[1] == L'2') &&
            (p[2] == L'd' || p[2] == L'D') &&
            (p[3] == L'p' || p[3] == L'P'))
            return 1;
    }
    return 0;
}

static const char *state_name(DWORD state)
{
    switch (state) {
    case SERVICE_STOPPED:          return "stopped";
    case SERVICE_START_PENDING:    return "start-pending";
    case SERVICE_STOP_PENDING:     return "stop-pending";
    case SERVICE_RUNNING:          return "running";
    case SERVICE_CONTINUE_PENDING: return "continue-pending";
    case SERVICE_PAUSE_PENDING:    return "pause-pending";
    case SERVICE_PAUSED:           return "paused";
    default:                       return "unknown";
    }
}

/* ── public API ─────────────────────────────────────────────────────── */

int oa2dp_driver_detect_log(void)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) {
        oa2dp_log(OA2DP_LOG_WARN,
                  "driver detect: OpenSCManager failed (err=%lu)",
                  GetLastError());
        return -1;
    }

    /* Enumerate both Win32 services and kernel drivers in one shot. */
    DWORD bytes_needed   = 0;
    DWORD services_count = 0;
    DWORD resume_handle  = 0;

    /* First call to discover the buffer size we need. */
    EnumServicesStatusExW(
        scm, SC_ENUM_PROCESS_INFO,
        SERVICE_WIN32 | SERVICE_DRIVER, SERVICE_STATE_ALL,
        NULL, 0, &bytes_needed, &services_count, &resume_handle, NULL);

    DWORD err = GetLastError();
    if (err != ERROR_MORE_DATA && err != ERROR_SUCCESS) {
        oa2dp_log(OA2DP_LOG_WARN,
                  "driver detect: EnumServicesStatusEx (sizing) failed (err=%lu)",
                  err);
        CloseServiceHandle(scm);
        return -1;
    }

    if (bytes_needed == 0) {
        oa2dp_log(OA2DP_LOG_WARN, "driver detect: no services to enumerate");
        CloseServiceHandle(scm);
        return 0;
    }

    BYTE *buffer = (BYTE *)malloc(bytes_needed);
    if (!buffer) {
        CloseServiceHandle(scm);
        return -1;
    }

    DWORD buffer_size = bytes_needed;
    bytes_needed   = 0;
    services_count = 0;
    resume_handle  = 0;

    int matches = 0;

    /* The buffer may not fit everything in one call — loop until done. */
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

            if (contains_a2dp(svc_name) || contains_a2dp(disp_name)) {
                char svc_utf8[256]  = {0};
                char disp_utf8[256] = {0};
                if (svc_name)
                    WideCharToMultiByte(CP_UTF8, 0, svc_name, -1,
                                        svc_utf8, sizeof(svc_utf8), NULL, NULL);
                if (disp_name)
                    WideCharToMultiByte(CP_UTF8, 0, disp_name, -1,
                                        disp_utf8, sizeof(disp_utf8), NULL, NULL);

                oa2dp_log(OA2DP_LOG_INFO,
                          "driver detect: A2DP-related service '%s' (%s) — %s",
                          svc_utf8, disp_utf8,
                          state_name(services[i].ServiceStatusProcess.dwCurrentState));
                matches++;
            }
        }

        if (ok) break;  /* fully drained */
        /* otherwise loop with the new resume_handle */
    }

    free(buffer);
    CloseServiceHandle(scm);

    if (matches == 0) {
        oa2dp_log(OA2DP_LOG_WARN,
                  "driver detect: no A2DP-related services found "
                  "(unusual — Bluetooth audio may not be installed)");
    } else {
        oa2dp_log(OA2DP_LOG_INFO,
                  "driver detect: %d A2DP-related service(s) found", matches);
    }

    return matches;
}
