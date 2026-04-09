/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * cli.c - Headless command-line action runner.
 *
 * Pixel Buds Pro 2 example:
 *   OpenA2DP.exe --disable-hfp AA:BB:CC:DD:EE:FF
 *
 * GUI applications under /SUBSYSTEM:WINDOWS don't get a console
 * automatically.  We attach to the parent terminal (if any) so output
 * lands where the user expects it; otherwise the messages go to the
 * in-memory log buffer and disappear when the process exits — still
 * functional, just silent.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wchar.h>

#include "oa2dp_cli.h"
#include "oa2dp_actions.h"
#include "oa2dp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Recognised commands ────────────────────────────────────────────── */

static const wchar_t *KNOWN_CMDS[] = {
    L"--reconnect",
    L"--disable-hfp",
    L"--enable-a2dp",
    L"--help",
    L"-h",
    L"/?",
    NULL
};

int oa2dp_cli_is_cli_invocation(int argc, wchar_t **argv)
{
    if (argc < 2 || !argv)
        return 0;

    for (int i = 1; i < argc; i++) {
        for (int k = 0; KNOWN_CMDS[k]; k++) {
            if (wcscmp(argv[i], KNOWN_CMDS[k]) == 0)
                return 1;
        }
    }
    return 0;
}

/* ── Console attach for /SUBSYSTEM:WINDOWS exe ──────────────────────── */

static void attach_parent_console(void)
{
    /* If launched from cmd.exe / PowerShell / Windows Terminal, attach
     * to that console so printf goes where the user expects.  If not
     * (e.g. double-clicked from Explorer), there is no parent console
     * and AttachConsole returns FALSE — that's fine, we just stay
     * silent and rely on logging. */
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;

    FILE *unused;
    freopen_s(&unused, "CONOUT$", "w", stdout);
    freopen_s(&unused, "CONOUT$", "w", stderr);
}

/* ── helpers ────────────────────────────────────────────────────────── */

static void wide_to_utf8(const wchar_t *src, char *dst, int dst_size)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dst_size, NULL, NULL);
    if (len <= 0 && dst_size > 0)
        dst[0] = '\0';
}

static int valid_bt_address(const char *s)
{
    /* Format: XX:XX:XX:XX:XX:XX (17 chars) */
    if (!s || strlen(s) != 17) return 0;
    for (int i = 0; i < 17; i++) {
        char c = s[i];
        if ((i + 1) % 3 == 0) {
            if (c != ':') return 0;
        } else {
            int hex = (c >= '0' && c <= '9') ||
                      (c >= 'a' && c <= 'f') ||
                      (c >= 'A' && c <= 'F');
            if (!hex) return 0;
        }
    }
    return 1;
}

static void print_usage(void)
{
    fprintf(stderr,
        "OpenA2DP - command-line Bluetooth audio control\n"
        "\n"
        "Usage:\n"
        "  OpenA2DP.exe [command] <bluetooth-address>\n"
        "\n"
        "Commands:\n"
        "  --reconnect <addr>     Cycle the A2DP AudioSink service\n"
        "  --disable-hfp <addr>   Disable Handsfree (HFP) on the device\n"
        "  --enable-a2dp <addr>   Enable A2DP AudioSink on the device\n"
        "  --help                 Show this message\n"
        "\n"
        "<addr> is a Bluetooth address in the form XX:XX:XX:XX:XX:XX.\n"
        "\n"
        "With no arguments, OpenA2DP launches the GUI.\n");
}

/* ── public API ─────────────────────────────────────────────────────── */

int oa2dp_cli_run(int argc, wchar_t **argv)
{
    attach_parent_console();

    if (argc < 2) {
        print_usage();
        return 2;
    }

    const wchar_t *cmd = argv[1];

    if (wcscmp(cmd, L"--help") == 0 ||
        wcscmp(cmd, L"-h") == 0 ||
        wcscmp(cmd, L"/?") == 0) {
        print_usage();
        return 0;
    }

    if (argc < 3) {
        fprintf(stderr, "error: missing <bluetooth-address>\n\n");
        print_usage();
        return 2;
    }

    char addr[64];
    wide_to_utf8(argv[2], addr, sizeof(addr));

    if (!valid_bt_address(addr)) {
        fprintf(stderr, "error: '%s' is not a valid Bluetooth address (expected XX:XX:XX:XX:XX:XX)\n",
                addr);
        return 2;
    }

    /* Logging is initialized by main() before we're called.  Hook into
     * the log buffer indirectly by also printing to stderr ourselves so
     * the user gets immediate feedback. */
    int rc = -1;

    if (wcscmp(cmd, L"--reconnect") == 0) {
        fprintf(stderr, "OpenA2DP: reconnecting %s...\n", addr);
        rc = oa2dp_action_reconnect(addr);
        fprintf(stderr, rc == 0 ? "OpenA2DP: reconnect OK\n"
                                 : "OpenA2DP: reconnect FAILED\n");
    } else if (wcscmp(cmd, L"--disable-hfp") == 0) {
        fprintf(stderr, "OpenA2DP: disabling Handsfree on %s...\n", addr);
        /* Use the sync set_service path indirectly via the async API
         * — but for CLI we want synchronous behaviour, so call the
         * sync reconnect helpers' siblings.  The actions.c module
         * exposes set_*_async only; for CLI we wait by busy-polling. */
        rc = oa2dp_action_set_handsfree_async(addr, 0);
        if (rc == 0) {
            while (oa2dp_action_busy()) Sleep(50);
            fprintf(stderr, "OpenA2DP: HFP disable issued\n");
        } else {
            fprintf(stderr, "OpenA2DP: HFP disable FAILED to enqueue\n");
        }
    } else if (wcscmp(cmd, L"--enable-a2dp") == 0) {
        fprintf(stderr, "OpenA2DP: enabling AudioSink on %s...\n", addr);
        rc = oa2dp_action_set_audiosink_async(addr, 1);
        if (rc == 0) {
            while (oa2dp_action_busy()) Sleep(50);
            fprintf(stderr, "OpenA2DP: AudioSink enable issued\n");
        } else {
            fprintf(stderr, "OpenA2DP: AudioSink enable FAILED to enqueue\n");
        }
    } else {
        fprintf(stderr, "error: unknown command '%ls'\n\n", cmd);
        print_usage();
        return 2;
    }

    return (rc == 0) ? 0 : 1;
}
