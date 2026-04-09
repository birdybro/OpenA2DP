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
#include "oa2dp_altdriver_config.h"
#include "oa2dp_config.h"
#include "oa2dp_device.h"
#include "oa2dp_driver_control.h"
#include "oa2dp_log.h"
#include "oa2dp_registry_probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Recognised commands ────────────────────────────────────────────── */

static const wchar_t *KNOWN_CMDS[] = {
    L"--reconnect",
    L"--disable-hfp",
    L"--enable-a2dp",
    L"--list-devices",
    L"--list-stacks",
    L"--switch-stack",
    L"--start-service",
    L"--stop-service",
    L"--probe-registry",
    L"--show-codec-config",
    L"--set-codec",
    L"--set-bitpool",
    L"--set-aac-bitrate",
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

/* The binary is /SUBSYSTEM:CONSOLE so the CRT has already wired up
 * stdin / stdout / stderr to the inherited (or freshly allocated)
 * console by the time wmain runs.  No AttachConsole needed. */

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
        "OpenA2DP-cli - command-line Bluetooth audio control\n"
        "\n"
        "Usage:\n"
        "  OpenA2DP-cli.exe <command> [args]\n"
        "\n"
        "Per-device actions (need <addr> in form XX:XX:XX:XX:XX:XX):\n"
        "  --reconnect <addr>          Cycle the A2DP AudioSink service\n"
        "  --disable-hfp <addr>        Disable Handsfree (HFP) on the device\n"
        "  --enable-a2dp <addr>        Enable A2DP AudioSink on the device\n"
        "\n"
        "Inventory / diagnostics:\n"
        "  --list-devices              List all paired Bluetooth audio devices\n"
        "  --list-stacks               List installed A2DP services and their state\n"
        "  --probe-registry            Dump A2DP-related registry config (read-only)\n"
        "  --show-codec-config <addr>  Show codec config from Alt A2DP registry\n"
        "\n"
        "Codec config (Alt A2DP Driver only, writes need Admin):\n"
        "  --set-codec <addr> sbc|aac      Switch preferred codec\n"
        "  --set-bitpool <addr> N          Set SBC max bitpool (clamped to device max)\n"
        "  --set-aac-bitrate <addr> N      Set AAC bitrate in kbps (64-320)\n"
        "\n"
        "Stack control (require Run as Administrator):\n"
        "  --switch-stack ms|alt       Switch active A2DP stack and reconnect devices\n"
        "  --start-service <name>      Start a Windows service by name\n"
        "  --stop-service <name>       Stop a Windows service by name\n"
        "\n"
        "  --help                      Show this message\n"
        "\n"
        "For the graphical interface, launch OpenA2DP.exe instead.\n");
}

static const char *conn_str(OA2DP_ConnState s)
{
    switch (s) {
    case OA2DP_CONN_CONNECTED:    return "connected";
    case OA2DP_CONN_CONNECTING:   return "connecting";
    case OA2DP_CONN_DISCONNECTED: return "disconnected";
    default:                      return "?";
    }
}

static int cli_list_devices(void)
{
    OA2DP_DeviceList list;
    memset(&list, 0, sizeof(list));
    int n = oa2dp_device_scan(&list);
    if (n < 0) {
        fprintf(stderr, "OpenA2DP: device scan failed\n");
        return 1;
    }
    if (n == 0) {
        printf("No paired Bluetooth audio devices found.\n");
        return 0;
    }
    printf("%-22s  %-13s  %s\n", "ADDRESS", "STATE", "NAME");
    for (int i = 0; i < list.count; i++) {
        printf("%-22s  %-13s  %s\n",
               list.profiles[i].device_id,
               conn_str(list.statuses[i].connection),
               list.profiles[i].display_name);
    }
    return 0;
}

static int cli_list_stacks(void)
{
    OA2DP_DriverList list;
    memset(&list, 0, sizeof(list));
    int n = oa2dp_driver_scan(&list);
    if (n < 0) {
        fprintf(stderr, "OpenA2DP: driver scan failed\n");
        return 1;
    }
    if (n == 0) {
        printf("No A2DP-related services found.\n");
        return 0;
    }
    char active[64];
    oa2dp_driver_active_stack_label(&list, active, sizeof(active));
    printf("Active stack: %s\n", active);
    printf("%-20s  %-10s  %s\n", "NAME", "STATE", "DISPLAY NAME");
    for (int i = 0; i < list.count; i++) {
        printf("%-20s  %-10s  %s\n",
               list.services[i].name,
               oa2dp_driver_state_label(list.services[i].state),
               list.services[i].display_name);
    }
    return 0;
}

/* ── codec config helpers ───────────────────────────────────────────── */

/* Load (or default) the profile for a device.  Caller must have
 * already initialized the config dir via oa2dp_config_init. */
static void cli_load_profile(const char *addr, OA2DP_DeviceProfile *p)
{
    oa2dp_profile_defaults(p);
    snprintf(p->device_id, sizeof(p->device_id), "%s", addr);

    char path[MAX_PATH];
    if (oa2dp_config_path_for_device(addr, path, sizeof(path)) == 0) {
        oa2dp_profile_load(path, p);
        snprintf(p->device_id, sizeof(p->device_id), "%s", addr);
    }
}

static int cli_save_profile(const char *addr, const OA2DP_DeviceProfile *p)
{
    char path[MAX_PATH];
    if (oa2dp_config_path_for_device(addr, path, sizeof(path)) != 0)
        return -1;
    return oa2dp_profile_save(path, p);
}

static int cli_show_codec_config(const char *addr)
{
    OA2DP_DeviceStatus stat;
    memset(&stat, 0, sizeof(stat));
    if (oa2dp_altdriver_read_current(addr, &stat) != 0) {
        fprintf(stderr,
                "error: device %s has no Alt A2DP Driver entry "
                "(driver not installed or device unknown to it)\n", addr);
        return 1;
    }
    oa2dp_altdriver_read_next(addr, &stat);

    OA2DP_DeviceProfile p;
    cli_load_profile(addr, &p);

    static const char *codec_strs[] = { "Unknown", "SBC", "AAC" };
    static const char *stereo_strs[] = { "joint", "stereo", "dual" };
    static const int   block_ints[] = { 4, 8, 12, 16 };
    static const int   subband_ints[] = { 4, 8 };

    printf("Codec config for %s:\n", addr);
    printf("\n  [Current — what's negotiated this session]\n");
    printf("  active codec:   %s\n", codec_strs[stat.active_codec]);
    if (stat.active_codec == OA2DP_CODEC_SBC) {
        printf("  stereo mode:    %s\n", stereo_strs[stat.stereo_mode]);
        printf("  block size:     %d\n", block_ints[stat.block_size]);
        printf("  allocation:     %s\n",
               stat.allocation_method == OA2DP_ALLOC_SNR ? "SNR" : "loudness");
        printf("  subbands:       %d\n", subband_ints[stat.subbands]);
        printf("  max bitpool:    %d\n", stat.bitpool);
    }
    if (stat.codec_bitrate_kbps > 0)
        printf("  codec bitrate:  %d kbps\n", stat.codec_bitrate_kbps);
    if (stat.sbc_max_bitpool_capability > 0)
        printf("  device max bp:  %d (Capability)\n",
               stat.sbc_max_bitpool_capability);

    printf("\n  [Next — preferences for the next reconnect]\n");
    if (stat.alt_snapshot_valid) {
        printf("  preferred:      %s\n",
               stat.snap_preferred_codec == OA2DP_CODEC_AAC ? "AAC" : "SBC");
        printf("  sbc rates:      %s%s%s%s\n",
               stat.snap_allow_48khz   ? "48 " : "",
               stat.snap_allow_44_1khz ? "44.1 " : "",
               stat.snap_allow_32khz   ? "32 " : "",
               stat.snap_allow_16khz   ? "16 " : "");
        printf("  sbc bitpool:    %d\n", stat.snap_bitpool);
        printf("  aac stereo:     %d  aac mono: %d\n",
               stat.snap_aac_allow_stereo, stat.snap_aac_allow_mono);
        printf("  aac rates:      %s%s\n",
               stat.snap_aac_allow_48khz   ? "48 " : "",
               stat.snap_aac_allow_44_1khz ? "44.1" : "");
        printf("  aac bitrate:    %d kbps\n", stat.snap_aac_bitrate_kbps);
        printf("  abr:            %d\n", stat.snap_abr_enable);
    } else {
        printf("  (snapshot not available)\n");
    }

    printf("\n  [Profile — saved to %%APPDATA%%\\OpenA2DP\\<addr>.ini]\n");
    printf("  preferred:      %s\n",
           p.preferred_codec == OA2DP_CODEC_AAC ? "AAC" : "SBC");
    printf("  sbc bitpool:    %d  (override device max: %d)\n",
           p.bitpool, p.sbc_override_device_max);
    printf("  aac bitrate:    %d kbps\n", p.aac_bitrate_kbps);
    printf("  abr:            %d\n", p.abr_enable);
    return 0;
}

/* Apply a one-field change: load profile, mutate, write registry,
 * save profile.  Caller has already validated admin elevation. */
static int cli_apply_one(const char *addr,
                         void (*mutate)(OA2DP_DeviceProfile *, int),
                         int arg, const char *what)
{
    OA2DP_DeviceProfile p;
    cli_load_profile(addr, &p);

    /* Read device's Capability bitpool max for the safety clamp. */
    OA2DP_DeviceStatus stat;
    memset(&stat, 0, sizeof(stat));
    oa2dp_altdriver_read_current(addr, &stat);

    mutate(&p, arg);

    if (oa2dp_altdriver_write_next(addr, &p,
                                   stat.sbc_max_bitpool_capability) != 0) {
        fprintf(stderr, "OpenA2DP: %s for %s FAILED\n", what, addr);
        return 1;
    }

    cli_save_profile(addr, &p);
    fprintf(stderr, "OpenA2DP: %s for %s OK (reconnect device for it to apply)\n",
            what, addr);
    return 0;
}

static void mut_set_codec_sbc(OA2DP_DeviceProfile *p, int unused) {
    (void)unused; p->preferred_codec = OA2DP_CODEC_SBC;
}
static void mut_set_codec_aac(OA2DP_DeviceProfile *p, int unused) {
    (void)unused; p->preferred_codec = OA2DP_CODEC_AAC;
}
static void mut_set_bitpool(OA2DP_DeviceProfile *p, int v) {
    if (v < 2)   v = 2;
    if (v > 250) v = 250;
    p->bitpool = v;
}
static void mut_set_aac_bitrate(OA2DP_DeviceProfile *p, int v) {
    if (v < 64)  v = 64;
    if (v > 320) v = 320;
    p->aac_bitrate_kbps = v;
}

static int cli_switch_stack(const wchar_t *target_arg)
{
    if (!oa2dp_process_is_elevated()) {
        fprintf(stderr,
                "error: --switch-stack needs Administrator. "
                "Relaunch from an elevated terminal.\n");
        return 1;
    }
    OA2DP_StackTarget target;
    if (wcscmp(target_arg, L"ms") == 0 ||
        wcscmp(target_arg, L"microsoft") == 0) {
        target = OA2DP_STACK_MICROSOFT;
    } else if (wcscmp(target_arg, L"alt") == 0 ||
               wcscmp(target_arg, L"alternative") == 0) {
        target = OA2DP_STACK_ALTERNATIVE;
    } else {
        fprintf(stderr, "error: --switch-stack expects 'ms' or 'alt'\n");
        return 2;
    }

    OA2DP_DriverList drivers;
    OA2DP_DeviceList devices;
    memset(&drivers, 0, sizeof(drivers));
    memset(&devices, 0, sizeof(devices));
    oa2dp_driver_scan(&drivers);
    oa2dp_device_scan(&devices);

    fprintf(stderr, "OpenA2DP: switching stack...\n");
    int rc = oa2dp_stack_switch_sync(target, &drivers, &devices);
    fprintf(stderr, rc == 0
                       ? "OpenA2DP: stack switch complete\n"
                       : "OpenA2DP: stack switch FAILED\n");
    return rc == 0 ? 0 : 1;
}

/* ── public API ─────────────────────────────────────────────────────── */

int oa2dp_cli_run(int argc, wchar_t **argv)
{
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

    /* ── Inventory commands (no extra arg) ──────────────────────── */

    if (wcscmp(cmd, L"--list-devices") == 0)
        return cli_list_devices();
    if (wcscmp(cmd, L"--list-stacks") == 0)
        return cli_list_stacks();

    /* ── Codec config commands ──────────────────────────────────── */
    if (wcscmp(cmd, L"--show-codec-config") == 0 ||
        wcscmp(cmd, L"--set-codec") == 0 ||
        wcscmp(cmd, L"--set-bitpool") == 0 ||
        wcscmp(cmd, L"--set-aac-bitrate") == 0) {

        if (argc < 3) {
            fprintf(stderr, "error: %ls needs a Bluetooth address\n", cmd);
            return 2;
        }
        char addr[64];
        wide_to_utf8(argv[2], addr, sizeof(addr));
        if (!valid_bt_address(addr)) {
            fprintf(stderr, "error: '%s' is not a valid Bluetooth address\n", addr);
            return 2;
        }

        /* All these commands need the config dir for INI access. */
        oa2dp_config_init();

        if (wcscmp(cmd, L"--show-codec-config") == 0)
            return cli_show_codec_config(addr);

        /* All write commands need admin (registry write). */
        if (!oa2dp_process_is_elevated()) {
            fprintf(stderr,
                    "error: %ls needs Administrator. "
                    "Relaunch from an elevated terminal.\n", cmd);
            return 1;
        }

        if (wcscmp(cmd, L"--set-codec") == 0) {
            if (argc < 4) {
                fprintf(stderr, "error: --set-codec needs 'sbc' or 'aac'\n");
                return 2;
            }
            if (wcscmp(argv[3], L"sbc") == 0)
                return cli_apply_one(addr, mut_set_codec_sbc, 0, "--set-codec sbc");
            if (wcscmp(argv[3], L"aac") == 0)
                return cli_apply_one(addr, mut_set_codec_aac, 0, "--set-codec aac");
            fprintf(stderr, "error: --set-codec value must be 'sbc' or 'aac'\n");
            return 2;
        }
        if (wcscmp(cmd, L"--set-bitpool") == 0) {
            if (argc < 4) {
                fprintf(stderr, "error: --set-bitpool needs an integer\n");
                return 2;
            }
            int v = _wtoi(argv[3]);
            return cli_apply_one(addr, mut_set_bitpool, v, "--set-bitpool");
        }
        if (wcscmp(cmd, L"--set-aac-bitrate") == 0) {
            if (argc < 4) {
                fprintf(stderr, "error: --set-aac-bitrate needs an integer (kbps)\n");
                return 2;
            }
            int v = _wtoi(argv[3]);
            return cli_apply_one(addr, mut_set_aac_bitrate, v, "--set-aac-bitrate");
        }
    }

    if (wcscmp(cmd, L"--probe-registry") == 0) {
        OA2DP_DriverList drivers;
        memset(&drivers, 0, sizeof(drivers));
        oa2dp_driver_scan(&drivers);
        oa2dp_registry_probe_log(&drivers);
        /* The probe writes to the in-memory log buffer; flush it to
         * stdout so the user sees it. */
        const OA2DP_LogBuffer *buf = oa2dp_log_get_buffer();
        int start = (buf->count < OA2DP_LOG_RING_SIZE) ? 0 : buf->head;
        for (int i = 0; i < buf->count; i++) {
            int idx = (start + i) % OA2DP_LOG_RING_SIZE;
            const OA2DP_LogEntry *e = &buf->entries[idx];
            printf("%s\n", e->message);
        }
        return 0;
    }

    /* ── Stack switch (one positional arg: target) ──────────────── */

    if (wcscmp(cmd, L"--switch-stack") == 0) {
        if (argc < 3) {
            fprintf(stderr, "error: --switch-stack needs 'ms' or 'alt'\n");
            return 2;
        }
        return cli_switch_stack(argv[2]);
    }

    /* ── Service start/stop (one positional arg: service name) ─── */

    if (wcscmp(cmd, L"--start-service") == 0 ||
        wcscmp(cmd, L"--stop-service") == 0) {
        if (argc < 3) {
            fprintf(stderr, "error: %ls needs a service name\n", cmd);
            return 2;
        }
        if (!oa2dp_process_is_elevated()) {
            fprintf(stderr,
                    "error: %ls needs Administrator. "
                    "Relaunch from an elevated terminal.\n", cmd);
            return 1;
        }
        char svc_name[64];
        wide_to_utf8(argv[2], svc_name, sizeof(svc_name));
        int rc = (wcscmp(cmd, L"--start-service") == 0)
                     ? oa2dp_driver_start(svc_name)
                     : oa2dp_driver_stop(svc_name);
        fprintf(stderr, rc == 0 ? "OpenA2DP: %ls '%s' OK\n"
                                 : "OpenA2DP: %ls '%s' FAILED\n",
                cmd, svc_name);
        return rc == 0 ? 0 : 1;
    }

    /* ── Per-device action commands (one positional arg: address) */

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
