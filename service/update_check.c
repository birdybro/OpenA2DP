/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * update_check.c - GitHub Releases API version check via WinHTTP
 *
 * Fires a worker thread that GETs api.github.com/repos/.../releases/latest,
 * extracts the "tag_name" field with a tiny ad-hoc parser (no JSON
 * library — we only need one string), compares to the embedded
 * OA2DP_VER_STRING, and posts a tray notification if a newer
 * version is available.
 *
 * Network failure is silent — if the user is offline or behind a
 * proxy that blocks the request, the check just doesn't fire.  No
 * point spamming the log with "couldn't reach GitHub" warnings.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include "oa2dp_update_check.h"
#include "oa2dp_version.h"
#include "oa2dp_log.h"
#include "oa2dp_tray.h"

#include <stdio.h>
#include <string.h>

/* ── single-shot guard ──────────────────────────────────────────── */

static volatile LONG g_already_checked = 0;
static volatile LONG g_in_flight       = 0;

/* ── version compare ────────────────────────────────────────────── */

/*
 * Parse "v0.6.0" or "0.6.0" into three integers.  Returns 0 on
 * success, -1 if the string doesn't have at least three dot-
 * separated numeric components.
 */
static int parse_version(const char *s, int *maj, int *min, int *pat)
{
    if (!s) return -1;
    if (*s == 'v' || *s == 'V') s++;
    int n = sscanf(s, "%d.%d.%d", maj, min, pat);
    return (n == 3) ? 0 : -1;
}

/* Returns 1 if 'latest' is strictly newer than 'current'. */
static int version_newer(const char *latest, const char *current)
{
    int la, lb, lc, ca, cb, cc;
    if (parse_version(latest,  &la, &lb, &lc) != 0) return 0;
    if (parse_version(current, &ca, &cb, &cc) != 0) return 0;
    if (la != ca) return la > ca;
    if (lb != cb) return lb > cb;
    return lc > cc;
}

/* ── HTTP worker ────────────────────────────────────────────────── */

static DWORD WINAPI check_thread(LPVOID param)
{
    (void)param;

    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;
    char body[8192] = {0};
    DWORD total = 0;

    hSession = WinHttpOpen(
        L"OpenA2DP-UpdateChecker/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) goto done;

    hConnect = WinHttpConnect(hSession, L"api.github.com",
                              INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) goto done;

    hRequest = WinHttpOpenRequest(
        hConnect, L"GET",
        L"/repos/birdybro/OpenA2DP/releases/latest",
        NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) goto done;

    /* GitHub requires a recognisable Accept header. */
    if (!WinHttpSendRequest(
            hRequest,
            L"Accept: application/vnd.github+json\r\n",
            (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        goto done;

    if (!WinHttpReceiveResponse(hRequest, NULL))
        goto done;

    /* Read up to body buffer minus 1, leave room for NUL. */
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
        if (total + avail > sizeof(body) - 1)
            avail = (DWORD)(sizeof(body) - 1 - total);
        if (avail == 0) break;
        DWORD got = 0;
        if (!WinHttpReadData(hRequest, body + total, avail, &got))
            break;
        if (got == 0) break;
        total += got;
    }
    body[total] = '\0';

    /* Tiny ad-hoc parse: find "tag_name":"…". */
    const char *p = strstr(body, "\"tag_name\"");
    if (!p) {
        oa2dp_log(OA2DP_LOG_DEBUG,
                  "update check: response missing tag_name (got %lu bytes)",
                  (unsigned long)total);
        goto done;
    }
    p = strchr(p, ':');
    if (!p) goto done;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '"') p++;

    char tag[32];
    int n = 0;
    while (n < (int)sizeof(tag) - 1 && p[n] && p[n] != '"')
        n++;
    if (n == 0) goto done;
    memcpy(tag, p, (size_t)n);
    tag[n] = '\0';

    if (version_newer(tag, OA2DP_VER_STRING)) {
        oa2dp_log(OA2DP_LOG_INFO,
            "update check: new version %s available (current %s)",
            tag, OA2DP_VER_STRING);
        char title[96];
        char msg[256];
        snprintf(title, sizeof(title),
                 "OpenA2DP %s available", tag);
        snprintf(msg, sizeof(msg),
                 "You're on %s. Get the latest from "
                 "github.com/birdybro/OpenA2DP/releases",
                 OA2DP_VER_STRING);
        oa2dp_tray_notify(title, msg);
    } else {
        oa2dp_log(OA2DP_LOG_INFO,
            "update check: up to date (latest %s, current %s)",
            tag, OA2DP_VER_STRING);
    }

done:
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    InterlockedExchange(&g_in_flight, 0);
    return 0;
}

/* ── public API ─────────────────────────────────────────────────── */

int oa2dp_update_check_async(void)
{
    /* Already ran once this session — refuse to run again. */
    if (InterlockedCompareExchange(&g_already_checked, 1, 0) != 0)
        return -1;

    /* Defensive: also reject overlapping calls. */
    if (InterlockedCompareExchange(&g_in_flight, 1, 0) != 0)
        return -1;

    HANDLE h = CreateThread(NULL, 0, check_thread, NULL, 0, NULL);
    if (!h) {
        InterlockedExchange(&g_in_flight, 0);
        InterlockedExchange(&g_already_checked, 0);
        oa2dp_log(OA2DP_LOG_WARN, "update check: CreateThread failed");
        return -1;
    }
    CloseHandle(h);
    return 0;
}
