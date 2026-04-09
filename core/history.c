/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * history.c - Per-device connection history file IO.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>

#include "oa2dp_history.h"
#include "oa2dp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── path helper ────────────────────────────────────────────────────── */

static int history_path(const char *device_id, char *out, int out_size)
{
    if (!device_id || !out) return -1;

    char appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata)))
        return -1;

    char safe_id[64];
    snprintf(safe_id, sizeof(safe_id), "%s", device_id);
    for (char *p = safe_id; *p; p++)
        if (*p == ':') *p = '_';

    snprintf(out, out_size, "%s\\OpenA2DP\\%s.history", appdata, safe_id);
    return 0;
}

/* ── append + auto-prune ────────────────────────────────────────────── */

static void timestamp_now(char *out, int out_size)
{
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_s(&tm_buf, &now);
    strftime(out, out_size, "%Y-%m-%d %H:%M:%S", &tm_buf);
}

/* Count newlines in a file (cheap-ish — only called when we suspect
 * the file may be over the cap). */
static int count_lines(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int count = 0;
    int c;
    while ((c = fgetc(f)) != EOF)
        if (c == '\n') count++;
    fclose(f);
    return count;
}

/* Drop the oldest half of the file's lines, keeping the newest. */
static void prune_oldest_half(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;

    /* Slurp into memory.  History files are tiny — at most a few KB. */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 1 * 1024 * 1024) { fclose(f); return; }

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return; }
    size_t n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    fclose(f);

    /* Find the start of the second half by line. */
    int total_lines = 0;
    for (size_t i = 0; i < n; i++)
        if (buf[i] == '\n') total_lines++;

    int skip = total_lines / 2;
    int seen = 0;
    size_t cut = 0;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            seen++;
            if (seen >= skip) { cut = i + 1; break; }
        }
    }

    /* Rewrite. */
    f = fopen(path, "w");
    if (f) {
        fwrite(buf + cut, 1, n - cut, f);
        fclose(f);
    }
    free(buf);
}

void oa2dp_history_append(const char *device_id, const char *state_label)
{
    if (!device_id || !state_label) return;

    char path[MAX_PATH];
    if (history_path(device_id, path, sizeof(path)) != 0) return;

    /* Prune if we're over the cap.  Cheap check: only count lines
     * when the file is suspiciously big to avoid scanning every time. */
    static int prune_check_counter = 0;
    if ((prune_check_counter++ % 16) == 0) {
        if (count_lines(path) > OA2DP_HISTORY_MAX_LINES)
            prune_oldest_half(path);
    }

    FILE *f = fopen(path, "a");
    if (!f) return;

    char ts[32];
    timestamp_now(ts, sizeof(ts));
    fprintf(f, "%s %s\n", ts, state_label);
    fclose(f);
}

/* ── load tail ──────────────────────────────────────────────────────── */

int oa2dp_history_load(const char *device_id,
                       char *out, int out_capacity, int entry_size)
{
    if (!device_id || !out || out_capacity <= 0 || entry_size <= 0)
        return 0;

    char path[MAX_PATH];
    if (history_path(device_id, path, sizeof(path)) != 0) return 0;

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    /* Read all lines into a temporary ring so we keep the newest N. */
    char *ring = (char *)calloc((size_t)out_capacity, (size_t)entry_size);
    if (!ring) { fclose(f); return 0; }

    int head = 0;
    int count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;
        snprintf(ring + (size_t)head * (size_t)entry_size,
                 (size_t)entry_size, "%s", line);
        head = (head + 1) % out_capacity;
        if (count < out_capacity) count++;
    }
    fclose(f);

    /* Copy out in chronological order. */
    int start = (count < out_capacity) ? 0 : head;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % out_capacity;
        memcpy(out + (size_t)i * (size_t)entry_size,
               ring + (size_t)idx * (size_t)entry_size,
               (size_t)entry_size);
    }

    free(ring);
    return count;
}
