/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * registry_probe.c - One-shot registry reconnaissance.
 *
 * Walks a fixed set of likely locations and dumps every value to
 * the log.  Used to figure out where Alternative A2DP Driver stores
 * its codec/SBC settings without guessing — once we have the actual
 * keys we can write targeted read/write logic.
 *
 * Read-only.  Never writes anything.  Output is depth-limited and
 * count-capped so the log stays sane.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "oa2dp_registry_probe.h"
#include "oa2dp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── tunables ───────────────────────────────────────────────────────── */

#define MAX_RECURSE_DEPTH    4
#define MAX_VALUES_PER_KEY  64
#define MAX_SUBKEYS_PER_KEY 64
#define MAX_DATA_PREVIEW    64   /* bytes shown for REG_BINARY */

/* ── helpers ────────────────────────────────────────────────────────── */

static const char *type_name(DWORD t)
{
    switch (t) {
    case REG_SZ:                       return "SZ";
    case REG_EXPAND_SZ:                return "EXPAND_SZ";
    case REG_MULTI_SZ:                 return "MULTI_SZ";
    case REG_DWORD:                    return "DWORD";
    case REG_DWORD_BIG_ENDIAN:         return "DWORD_BE";
    case REG_QWORD:                    return "QWORD";
    case REG_BINARY:                   return "BINARY";
    case REG_NONE:                     return "NONE";
    case REG_LINK:                     return "LINK";
    case REG_RESOURCE_LIST:            return "RESOURCE_LIST";
    default:                           return "?";
    }
}

static void wide_to_utf8(const wchar_t *src, char *dst, int dst_size)
{
    if (!src || !dst || dst_size <= 0) return;
    int len = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dst_size, NULL, NULL);
    if (len <= 0) dst[0] = '\0';
}

/* Build an indent string of `depth` two-space steps. */
static void make_indent(int depth, char *out, int out_size)
{
    int i = 0;
    for (; i < depth * 2 && i < out_size - 1; i++) out[i] = ' ';
    out[i] = '\0';
}

/* ── value formatting ───────────────────────────────────────────────── */

static void log_value(int depth, const wchar_t *name, DWORD type,
                      const BYTE *data, DWORD data_size)
{
    char indent[16];
    make_indent(depth, indent, sizeof(indent));

    char name_utf8[256];
    wide_to_utf8(name && name[0] ? name : L"(default)", name_utf8, sizeof(name_utf8));

    switch (type) {
    case REG_SZ:
    case REG_EXPAND_SZ: {
        char val_utf8[512] = {0};
        wide_to_utf8((const wchar_t *)data, val_utf8, sizeof(val_utf8));
        oa2dp_log(OA2DP_LOG_INFO, "%s%s [%s] = \"%s\"",
                  indent, name_utf8, type_name(type), val_utf8);
        break;
    }
    case REG_MULTI_SZ: {
        /* Multi-string is a sequence of null-terminated wide strings
         * ending with a double null.  Print each on its own line. */
        const wchar_t *p = (const wchar_t *)data;
        oa2dp_log(OA2DP_LOG_INFO, "%s%s [%s] =", indent, name_utf8, type_name(type));
        while (p && *p) {
            char str_utf8[512] = {0};
            wide_to_utf8(p, str_utf8, sizeof(str_utf8));
            oa2dp_log(OA2DP_LOG_INFO, "%s    \"%s\"", indent, str_utf8);
            p += wcslen(p) + 1;
        }
        break;
    }
    case REG_DWORD: {
        if (data_size >= sizeof(DWORD)) {
            DWORD v = *(const DWORD *)data;
            oa2dp_log(OA2DP_LOG_INFO, "%s%s [%s] = %lu (0x%08lx)",
                      indent, name_utf8, type_name(type), v, v);
        }
        break;
    }
    case REG_QWORD: {
        if (data_size >= sizeof(UINT64)) {
            UINT64 v = *(const UINT64 *)data;
            oa2dp_log(OA2DP_LOG_INFO, "%s%s [%s] = %llu",
                      indent, name_utf8, type_name(type),
                      (unsigned long long)v);
        }
        break;
    }
    case REG_BINARY: {
        char hex[3 * MAX_DATA_PREVIEW + 16];
        DWORD show = data_size < MAX_DATA_PREVIEW ? data_size : MAX_DATA_PREVIEW;
        int  pos = 0;
        for (DWORD i = 0; i < show; i++) {
            int n = snprintf(hex + pos, sizeof(hex) - pos, "%02x ", data[i]);
            if (n < 0) break;
            pos += n;
            if ((size_t)pos + 4 >= sizeof(hex)) break;
        }
        oa2dp_log(OA2DP_LOG_INFO, "%s%s [%s,%lu bytes] = %s%s",
                  indent, name_utf8, type_name(type),
                  data_size, hex,
                  data_size > show ? "..." : "");
        break;
    }
    default:
        oa2dp_log(OA2DP_LOG_INFO, "%s%s [%s,%lu bytes] = (not shown)",
                  indent, name_utf8, type_name(type), data_size);
        break;
    }
}

/* ── recursive walker ───────────────────────────────────────────────── */

static void walk_key(HKEY hkey, const wchar_t *display_path, int depth)
{
    if (depth > MAX_RECURSE_DEPTH) return;

    char indent[16];
    make_indent(depth, indent, sizeof(indent));

    char path_utf8[512];
    wide_to_utf8(display_path, path_utf8, sizeof(path_utf8));
    oa2dp_log(OA2DP_LOG_INFO, "%s[%s]", indent, path_utf8);

    /* Values. */
    DWORD num_values = 0;
    DWORD max_value_name = 0;
    DWORD max_value_data = 0;
    if (RegQueryInfoKeyW(hkey, NULL, NULL, NULL, NULL, NULL, NULL,
                         &num_values, &max_value_name, &max_value_data,
                         NULL, NULL) == ERROR_SUCCESS) {
        if (num_values > MAX_VALUES_PER_KEY) {
            oa2dp_log(OA2DP_LOG_INFO,
                      "%s  (%lu values total, capping at %d)",
                      indent, num_values, MAX_VALUES_PER_KEY);
            num_values = MAX_VALUES_PER_KEY;
        }

        wchar_t *name_buf = (wchar_t *)malloc((max_value_name + 2) * sizeof(wchar_t));
        BYTE   *data_buf  = (BYTE *)malloc(max_value_data + 4);
        if (name_buf && data_buf) {
            for (DWORD i = 0; i < num_values; i++) {
                DWORD name_len = max_value_name + 1;
                DWORD data_len = max_value_data;
                DWORD type = 0;
                if (RegEnumValueW(hkey, i, name_buf, &name_len, NULL,
                                  &type, data_buf, &data_len) == ERROR_SUCCESS) {
                    log_value(depth + 1, name_buf, type, data_buf, data_len);
                }
            }
        }
        free(name_buf);
        free(data_buf);
    }

    /* Subkeys. */
    DWORD num_subkeys = 0;
    DWORD max_subkey_name = 0;
    if (RegQueryInfoKeyW(hkey, NULL, NULL, NULL, &num_subkeys, &max_subkey_name,
                         NULL, NULL, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
        return;

    if (num_subkeys > MAX_SUBKEYS_PER_KEY) {
        oa2dp_log(OA2DP_LOG_INFO,
                  "%s  (%lu subkeys total, capping at %d)",
                  indent, num_subkeys, MAX_SUBKEYS_PER_KEY);
        num_subkeys = MAX_SUBKEYS_PER_KEY;
    }

    wchar_t *sub_name = (wchar_t *)malloc((max_subkey_name + 2) * sizeof(wchar_t));
    if (!sub_name) return;

    for (DWORD i = 0; i < num_subkeys; i++) {
        DWORD name_len = max_subkey_name + 1;
        if (RegEnumKeyExW(hkey, i, sub_name, &name_len, NULL, NULL, NULL,
                          NULL) != ERROR_SUCCESS)
            continue;

        HKEY child = NULL;
        if (RegOpenKeyExW(hkey, sub_name, 0, KEY_READ, &child) != ERROR_SUCCESS)
            continue;

        wchar_t child_path[512];
        _snwprintf_s(child_path, 512, _TRUNCATE, L"%s\\%s",
                     display_path, sub_name);
        walk_key(child, child_path, depth + 1);
        RegCloseKey(child);
    }
    free(sub_name);
}

/* Open a key by full path under HKLM and walk it. */
static int probe_path(const wchar_t *path)
{
    HKEY h = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &h) != ERROR_SUCCESS)
        return 0;

    char path_utf8[512];
    wide_to_utf8(path, path_utf8, sizeof(path_utf8));
    oa2dp_log(OA2DP_LOG_INFO, "registry probe: HKLM\\%s exists", path_utf8);
    walk_key(h, path, 0);
    RegCloseKey(h);
    return 1;
}

/* ── public API ─────────────────────────────────────────────────────── */

int oa2dp_registry_probe_log(const OA2DP_DriverList *drivers)
{
    int roots = 0;
    oa2dp_log(OA2DP_LOG_INFO,
              "registry probe: starting reconnaissance for A2DP driver settings");

    /* 1. Per-service registry entries. */
    if (drivers) {
        for (int i = 0; i < drivers->count; i++) {
            const OA2DP_A2dpService *svc = &drivers->services[i];

            wchar_t svc_name_w[64];
            MultiByteToWideChar(CP_UTF8, 0, svc->name, -1, svc_name_w, 64);

            wchar_t svc_path[256];
            _snwprintf_s(svc_path, 256, _TRUNCATE,
                         L"SYSTEM\\CurrentControlSet\\Services\\%s",
                         svc_name_w);
            roots += probe_path(svc_path);

            wchar_t params_path[256];
            _snwprintf_s(params_path, 256, _TRUNCATE,
                         L"SYSTEM\\CurrentControlSet\\Services\\%s\\Parameters",
                         svc_name_w);
            roots += probe_path(params_path);
        }
    }

    /* 2. Likely vendor keys.  Try several spellings since the actual
     * vendor name varies. */
    static const wchar_t *vendor_paths[] = {
        L"SOFTWARE\\Bluetoothgoodies",
        L"SOFTWARE\\Bluetooth Goodies",
        L"SOFTWARE\\BluetoothGoodies",
        L"SOFTWARE\\Alternative A2DP Driver",
        L"SOFTWARE\\AlternativeA2DPDriver",
        L"SOFTWARE\\AltA2DP",
        L"SOFTWARE\\Wow6432Node\\Bluetoothgoodies",
        L"SOFTWARE\\Wow6432Node\\Bluetooth Goodies",
        L"SOFTWARE\\Wow6432Node\\Alternative A2DP Driver",
        L"SOFTWARE\\Wow6432Node\\AltA2DP",
        NULL
    };
    for (int i = 0; vendor_paths[i]; i++)
        roots += probe_path(vendor_paths[i]);

    oa2dp_log(OA2DP_LOG_INFO,
              "registry probe: done — opened %d root(s).  "
              "If your Alternative A2DP Driver settings aren't visible above, "
              "tell me where its control panel writes to and I'll add the path.",
              roots);
    return roots;
}
