/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * log.c - In-memory ring buffer logging
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "oa2dp_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static OA2DP_LogBuffer g_log;
static CRITICAL_SECTION g_log_cs;

void oa2dp_log_init(void)
{
    InitializeCriticalSection(&g_log_cs);
    memset(&g_log, 0, sizeof(g_log));
}

void oa2dp_log(OA2DP_LogLevel level, const char *fmt, ...)
{
    EnterCriticalSection(&g_log_cs);

    OA2DP_LogEntry *entry = &g_log.entries[g_log.head];

    entry->timestamp = time(NULL);
    entry->level = level;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(entry->message, OA2DP_LOG_MSG_MAX, fmt, ap);
    va_end(ap);

    g_log.head = (g_log.head + 1) % OA2DP_LOG_RING_SIZE;
    if (g_log.count < OA2DP_LOG_RING_SIZE)
        g_log.count++;

    LeaveCriticalSection(&g_log_cs);
}

void oa2dp_log_clear(void)
{
    EnterCriticalSection(&g_log_cs);
    g_log.head = 0;
    g_log.count = 0;
    LeaveCriticalSection(&g_log_cs);
}

const OA2DP_LogBuffer *oa2dp_log_get_buffer(void)
{
    return &g_log;
}

static const char *s_level_labels[OA2DP_LOG_COUNT] = {
    "DEBUG", "INFO", "WARN", "ERROR"
};

const char *oa2dp_log_level_str(OA2DP_LogLevel level)
{
    if (level >= 0 && level < OA2DP_LOG_COUNT)
        return s_level_labels[level];
    return "???";
}
