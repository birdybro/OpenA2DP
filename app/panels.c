/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * panels.c - UI panel drawing (device list, settings, status, log)
 */

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "panels.h"
#include "oa2dp_log.h"
#include "oa2dp_config.h"
#include "oa2dp_actions.h"
#include "oa2dp_altdriver_config.h"
#include "oa2dp_driver_control.h"
#include "oa2dp_history.h"
#include "oa2dp_stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── label tables ───────────────────────────────────────────────────── */

static const char *codec_labels[]   = { "Unknown", "SBC", "AAC" };
static const char *stereo_labels[]  = { "Joint Stereo", "Stereo", "Dual Channel" };
static const char *alloc_labels[]   = { "SNR", "Loudness" };
static const char *block_labels[]   = { "4", "8", "12", "16" };
static const char *subband_labels[] = { "4", "8" };
static const char *conn_labels[]    = { "Disconnected", "Connecting", "Connected" };

static const int block_values[]   = { 4, 8, 12, 16 };
static const int subband_values[] = { 4, 8 };

/* ── helper: connection state color ─────────────────────────────────── */

static ImVec4_c conn_color(OA2DP_ConnState s)
{
    ImVec4_c c;
    switch (s) {
    case OA2DP_CONN_CONNECTED:    c.x=0.2f; c.y=0.9f; c.z=0.2f; c.w=1.0f; break;
    case OA2DP_CONN_CONNECTING:   c.x=1.0f; c.y=0.8f; c.z=0.0f; c.w=1.0f; break;
    default:                      c.x=0.6f; c.y=0.6f; c.z=0.6f; c.w=1.0f; break;
    }
    return c;
}

/* ── Device list (left panel) ───────────────────────────────────────── */

static ImVec4_c svc_state_color(OA2DP_ServiceState s)
{
    ImVec4_c c;
    switch (s) {
    case OA2DP_SVC_RUNNING:        c.x=0.2f; c.y=0.9f; c.z=0.2f; c.w=1.0f; break;
    case OA2DP_SVC_START_PENDING:
    case OA2DP_SVC_STOP_PENDING:   c.x=1.0f; c.y=0.8f; c.z=0.0f; c.w=1.0f; break;
    case OA2DP_SVC_STOPPED:        c.x=0.6f; c.y=0.6f; c.z=0.6f; c.w=1.0f; break;
    default:                       c.x=0.8f; c.y=0.4f; c.z=0.4f; c.w=1.0f; break;
    }
    return c;
}

static void draw_drivers_section(OA2DP_UIState *ui)
{
    igText("A2DP Stacks");
    igSeparator();

    if (ui->drivers.count == 0) {
        igTextDisabled("No A2DP services detected.");
        return;
    }

    int elevated = oa2dp_process_is_elevated();
    int switching = oa2dp_stack_switch_busy();

    /* One-click stack switcher.  The worker stops the wrong-stack
     * services, starts the right ones, and reconnects every connected
     * device against the new stack — all on a background thread. */
    {
        bool can_switch = elevated && !switching;
        if (!can_switch) igBeginDisabled(true);

        ImVec2_c btn = { 130, 0 };
        if (igButton("Use Microsoft", btn))
            oa2dp_stack_switch_async(OA2DP_STACK_MICROSOFT,
                                     &ui->drivers, &ui->devices);
        igSameLine(0, 4);
        if (igButton("Use Alternative", btn))
            oa2dp_stack_switch_async(OA2DP_STACK_ALTERNATIVE,
                                     &ui->drivers, &ui->devices);

        if (!can_switch) igEndDisabled();

        if (switching) {
            igSameLine(0, 8);
            ImVec4_c col = { 1.0f, 0.8f, 0.0f, 1.0f };
            igTextColored(col, "switching...");
        }
    }

    igSeparator();

    for (int i = 0; i < ui->drivers.count; i++) {
        OA2DP_A2dpService *svc = &ui->drivers.services[i];

        ImVec4_c col = svc_state_color(svc->state);
        igTextColored(col, "%s", "(*)");
        igSameLine(0, 4);
        igText("%s", svc->name);

        igSameLine(0, 6);
        igTextDisabled("%s", oa2dp_driver_state_label(svc->state));

        /* Tooltip with the full display name on hover. */
        if (igIsItemHovered(ImGuiHoveredFlags_None)) {
            igBeginTooltip();
            igText("%s", svc->display_name);
            igText("type: %s", svc->is_driver ? "kernel driver" : "win32 service");
            igEndTooltip();
        }

        ImVec2_c btn = { 60, 0 };

        /* Buttons are state-aware, elevation-aware, AND switch-aware:
         * blocked entirely while a stack switch is running so it can't
         * race with the worker. */
        bool can_start = elevated && !switching &&
                         (svc->state == OA2DP_SVC_STOPPED);
        bool can_stop  = elevated && !switching &&
                         (svc->state == OA2DP_SVC_RUNNING ||
                          svc->state == OA2DP_SVC_PAUSED);

        if (!can_start) igBeginDisabled(true);
        char start_id[80];
        snprintf(start_id, sizeof(start_id), "Start##svc%d", i);
        if (igButton(start_id, btn)) {
            oa2dp_driver_start(svc->name);
            oa2dp_driver_refresh(&ui->drivers, i);
        }
        if (!can_start) igEndDisabled();

        igSameLine(0, 4);

        if (!can_stop) igBeginDisabled(true);
        char stop_id[80];
        snprintf(stop_id, sizeof(stop_id), "Stop##svc%d", i);
        if (igButton(stop_id, btn)) {
            oa2dp_driver_stop(svc->name);
            oa2dp_driver_refresh(&ui->drivers, i);
        }
        if (!can_stop) igEndDisabled();
    }

    igSeparator();
    if (elevated) {
        ImVec4_c ok = { 0.2f, 0.9f, 0.2f, 1.0f };
        igTextColored(ok, "Running as Administrator");
    } else {
        ImVec4_c warn = { 1.0f, 0.8f, 0.0f, 1.0f };
        igTextColored(warn, "Not elevated — controls disabled");
        igTextWrapped("Relaunch via Run as administrator to enable Start/Stop.");
    }
}

static void draw_device_list(OA2DP_UIState *ui)
{
    ImVec2_c size = { 280, 0 };
    igBeginChild_Str("##devlist", size, ImGuiChildFlags_Borders,
                     ImGuiWindowFlags_None);

    igText("Devices");
    igSeparator();

    for (int i = 0; i < ui->devices.count; i++) {
        const OA2DP_DeviceProfile *p = &ui->devices.profiles[i];
        const OA2DP_DeviceStatus  *s = &ui->devices.statuses[i];

        ImVec4_c col = conn_color(s->connection);
        igTextColored(col, "%s", "(*)");
        igSameLine(0, 4);

        ImVec2_c sel_size = {0, 0};
        bool selected = (ui->selected == i);
        if (igSelectable_BoolPtr(p->display_name, &selected, 0, sel_size)) {
            ui->selected = i;
        }
    }

    igSeparator();
    igDummy((ImVec2_c){0, 4});
    draw_drivers_section(ui);

    /* ── Activity counters ──────────────────────────────────────── */
    igSeparator();
    {
        const OA2DP_Stats *st = oa2dp_stats_get();
        igText("Activity");
        igTextDisabled("Reconnects:    %ld", st->reconnects);
        igTextDisabled("Heal trig/rec/fail: %ld / %ld / %ld",
                       st->heal_triggers, st->heal_recoveries, st->heal_failures);
        igTextDisabled("HFP watchdog:  %ld", st->hfp_watchdog);
        igTextDisabled("Stack switches: %ld", st->stack_switches);
    }

    igEndChild();
}

/* Copy the codec-relevant snap_* fields from the registry-snapshot
 * back into the profile, reverting any unsaved edits.  Used by the
 * Discard button. */
static void revert_codec_to_snapshot(OA2DP_DeviceProfile *p,
                                     const OA2DP_DeviceStatus *s)
{
    if (!s->alt_snapshot_valid) return;
    p->preferred_codec    = (OA2DP_CodecType)s->snap_preferred_codec;
    p->allow_16khz        = s->snap_allow_16khz;
    p->allow_32khz        = s->snap_allow_32khz;
    p->allow_44_1khz      = s->snap_allow_44_1khz;
    p->allow_48khz        = s->snap_allow_48khz;
    p->stereo_mode        = (OA2DP_StereoMode)s->snap_stereo_mode;
    p->block_size         = (OA2DP_BlockSize)s->snap_block_size;
    p->allocation_method  = (OA2DP_AllocMethod)s->snap_allocation_method;
    p->subbands           = (OA2DP_Subbands)s->snap_subbands;
    p->bitpool            = s->snap_bitpool;
    p->aac_bitrate_kbps   = s->snap_aac_bitrate_kbps;
    p->aac_allow_stereo   = s->snap_aac_allow_stereo;
    p->aac_allow_mono     = s->snap_aac_allow_mono;
    p->aac_allow_44_1khz  = s->snap_aac_allow_44_1khz;
    p->aac_allow_48khz    = s->snap_aac_allow_48khz;
    p->abr_enable         = s->snap_abr_enable;
}

/* ── Settings panel (main panel) ────────────────────────────────────── */

static void draw_settings(OA2DP_UIState *ui)
{
    if (ui->selected < 0 || ui->selected >= ui->devices.count) {
        igText("No device selected.");
        return;
    }

    OA2DP_DeviceProfile *p = &ui->devices.profiles[ui->selected];
    OA2DP_DeviceStatus  *s = &ui->devices.statuses[ui->selected];

    igText("Profile: %s", p->display_name);
    igSeparator();

    /* ── Codec settings (Alternative A2DP Driver only) ──────────────
     *
     * The codec/SBC/AAC parameters here only have any effect when
     * the device is on the Alternative A2DP Driver stack.  We
     * detect that via s->alt_snapshot_valid (set by the device
     * probe when it successfully read Next\<addr>).
     *
     * On the Microsoft stack the whole section is greyed out with
     * an explanation that those settings are unenforceable. */
    int codec_editable = s->alt_snapshot_valid;
    int elevated = oa2dp_process_is_elevated();

    igText("Codec Settings");
    if (!codec_editable) {
        ImVec4_c warn = { 1.0f, 0.7f, 0.0f, 1.0f };
        igTextColored(warn,
            "Microsoft stack ignores these — switch to Alt A2DP Driver to apply.");
    }

    if (!codec_editable) igBeginDisabled(true);

    /* Codec selector — only offer SBC and AAC, not "Unknown" which
     * the underlying enum has as index 0 for "we don't know what's
     * negotiated yet". */
    {
        static const char *choices[] = { "SBC", "AAC" };
        int idx = (p->preferred_codec == OA2DP_CODEC_AAC) ? 1 : 0;
        igSetNextItemWidth(150);
        if (igCombo_Str_arr("Codec", &idx, choices, 2, -1))
            p->preferred_codec =
                (idx == 1) ? OA2DP_CODEC_AAC : OA2DP_CODEC_SBC;
    }

    /* Codec-specific UI */
    if (p->preferred_codec == OA2DP_CODEC_SBC) {
        igText("Sample Rates");
        {
            bool r16 = (bool)p->allow_16khz;
            bool r32 = (bool)p->allow_32khz;
            bool r44 = (bool)p->allow_44_1khz;
            bool r48 = (bool)p->allow_48khz;
            igCheckbox("16 kHz", &r16);   igSameLine(0, 10);
            igCheckbox("32 kHz", &r32);   igSameLine(0, 10);
            igCheckbox("44.1 kHz", &r44); igSameLine(0, 10);
            igCheckbox("48 kHz", &r48);
            p->allow_16khz = r16; p->allow_32khz = r32;
            p->allow_44_1khz = r44; p->allow_48khz = r48;
        }

        int sm = (int)p->stereo_mode;
        igSetNextItemWidth(150);
        if (igCombo_Str_arr("Stereo Mode", &sm, stereo_labels, OA2DP_STEREO_COUNT, -1))
            p->stereo_mode = (OA2DP_StereoMode)sm;

        int bs = (int)p->block_size;
        igSetNextItemWidth(150);
        if (igCombo_Str_arr("Block Size", &bs, block_labels, OA2DP_BLOCK_COUNT, -1))
            p->block_size = (OA2DP_BlockSize)bs;

        int am = (int)p->allocation_method;
        igSetNextItemWidth(150);
        if (igCombo_Str_arr("Allocation", &am, alloc_labels, OA2DP_ALLOC_COUNT, -1))
            p->allocation_method = (OA2DP_AllocMethod)am;

        int sb = (int)p->subbands;
        igSetNextItemWidth(150);
        if (igCombo_Str_arr("Subbands", &sb, subband_labels, OA2DP_SUBBANDS_COUNT, -1))
            p->subbands = (OA2DP_Subbands)sb;

        /* SBC bitpool with device-cap safety guard.  Default slider
         * max = device's reported Capability.SbcMaximumBitpool.  The
         * "Override device max" checkbox lets the user push beyond
         * that, but only when running as Administrator. */
        int dev_cap = s->sbc_max_bitpool_capability;
        int slider_max = (dev_cap > 0 && !p->sbc_override_device_max)
                             ? dev_cap : 250;
        if (slider_max < 2) slider_max = 250;

        /* Force-clamp the stored value if it exceeds the active
         * slider max — protects against pre-existing INI values
         * that were saved before this safety guard existed. */
        if (p->bitpool > slider_max) p->bitpool = slider_max;
        if (p->bitpool < 2) p->bitpool = 2;

        igSetNextItemWidth(200);
        igSliderInt("Max Bitpool", &p->bitpool, 2, slider_max, "%d", 0);

        /* Override toggle — gated on elevation. */
        {
            int can_toggle = elevated;
            if (!can_toggle) igBeginDisabled(true);
            bool ovr = (bool)p->sbc_override_device_max;
            igCheckbox("Override device max bitpool", &ovr);
            p->sbc_override_device_max = ovr;
            if (!can_toggle) igEndDisabled();

            if (dev_cap > 0) {
                igSameLine(0, 8);
                igTextDisabled("(device max: %d)", dev_cap);
            }
            if (p->sbc_override_device_max) {
                ImVec4_c warn = { 1.0f, 0.5f, 0.3f, 1.0f };
                igTextColored(warn,
                    "WARNING: bitpool above the device's reported max may "
                    "produce broken audio or damage some Bluetooth chips.");
            } else if (!elevated && dev_cap > 0) {
                igTextDisabled(
                    "(Override is admin-only — relaunch as Administrator to enable)");
            }
        }
    } else if (p->preferred_codec == OA2DP_CODEC_AAC) {
        igText("AAC Channels");
        {
            bool ast = (bool)p->aac_allow_stereo;
            bool amo = (bool)p->aac_allow_mono;
            igCheckbox("Allow Stereo##aac", &ast);
            igSameLine(0, 16);
            igCheckbox("Allow Mono##aac", &amo);
            p->aac_allow_stereo = ast;
            p->aac_allow_mono   = amo;
        }
        igText("AAC Sample Rates");
        {
            bool r44 = (bool)p->aac_allow_44_1khz;
            bool r48 = (bool)p->aac_allow_48khz;
            igCheckbox("44.1 kHz##aac", &r44);
            igSameLine(0, 16);
            igCheckbox("48 kHz##aac", &r48);
            p->aac_allow_44_1khz = r44;
            p->aac_allow_48khz   = r48;
        }
        igSetNextItemWidth(200);
        /* 0 = "device default" sentinel; otherwise 64..320 kbps. */
        if (p->aac_bitrate_kbps == 0) p->aac_bitrate_kbps = 256;
        igSliderInt("AAC Bitrate (kbps)", &p->aac_bitrate_kbps, 64, 320, "%d", 0);
    }

    /* ABR Enable applies to both codecs. */
    {
        bool abr = (bool)p->abr_enable;
        igCheckbox("Adaptive Bit Rate (ABR)", &abr);
        p->abr_enable = abr;
    }

    if (!codec_editable) igEndDisabled();

    /* ── Dirty detection & Apply ────────────────────────────────── */
    if (codec_editable) {
        /* Compute the bitpool value that would actually get written
         * (after the device-cap clamp), so the dirty check matches
         * what we'd push to the registry — otherwise the form would
         * show "dirty" forever just because the snapshot has 37 and
         * the profile has 53. */
        int effective_bp = p->bitpool;
        if (!p->sbc_override_device_max &&
            s->sbc_max_bitpool_capability > 0 &&
            effective_bp > s->sbc_max_bitpool_capability) {
            effective_bp = s->sbc_max_bitpool_capability;
        }

        int dirty =
            (p->preferred_codec    != s->snap_preferred_codec) ||
            (p->allow_16khz        != s->snap_allow_16khz) ||
            (p->allow_32khz        != s->snap_allow_32khz) ||
            (p->allow_44_1khz      != s->snap_allow_44_1khz) ||
            (p->allow_48khz        != s->snap_allow_48khz) ||
            (p->stereo_mode        != s->snap_stereo_mode) ||
            (p->block_size         != s->snap_block_size) ||
            (p->allocation_method  != s->snap_allocation_method) ||
            (p->subbands           != s->snap_subbands) ||
            (effective_bp          != s->snap_bitpool) ||
            (p->aac_bitrate_kbps   != s->snap_aac_bitrate_kbps) ||
            (p->aac_allow_stereo   != s->snap_aac_allow_stereo) ||
            (p->aac_allow_mono     != s->snap_aac_allow_mono) ||
            (p->aac_allow_44_1khz  != s->snap_aac_allow_44_1khz) ||
            (p->aac_allow_48khz    != s->snap_aac_allow_48khz) ||
            (p->abr_enable         != s->snap_abr_enable);

        if (dirty) {
            ImVec4_c warn = { 1.0f, 0.8f, 0.0f, 1.0f };
            igTextColored(warn,
                "Unsaved changes — click Apply to push to driver, "
                "then reconnect the device for them to take effect.");
            if (!elevated) {
                igTextDisabled(
                    "(Apply requires running OpenA2DP as Administrator)");
            }

            int can_apply = elevated;
            if (!can_apply) igBeginDisabled(true);

            ImVec2_c btn = { 100, 0 };
            if (igButton("Apply", btn)) {
                if (oa2dp_altdriver_write_next(p->device_id, p,
                        s->sbc_max_bitpool_capability) == 0) {
                    /* Refresh snapshot from registry so dirty clears. */
                    oa2dp_altdriver_read_next(p->device_id, s);
                }
            }
            igSameLine(0, 6);
            ImVec2_c btn_long = { 160, 0 };
            if (igButton("Apply & Reconnect", btn_long)) {
                if (oa2dp_altdriver_write_next(p->device_id, p,
                        s->sbc_max_bitpool_capability) == 0) {
                    oa2dp_altdriver_read_next(p->device_id, s);
                    oa2dp_action_reconnect_async(p->device_id);
                }
            }

            if (!can_apply) igEndDisabled();

            /* Discard doesn't write anywhere — it just reverts the
             * profile to the last-known snapshot.  No admin needed. */
            igSameLine(0, 6);
            if (igButton("Discard", btn)) {
                /* Re-read in case anything changed externally. */
                oa2dp_altdriver_read_next(p->device_id, s);
                revert_codec_to_snapshot(p, s);
            }
        }
    }

    igSeparator();

    /* Actions */
    {
        int busy = oa2dp_action_busy();
        if (busy) igBeginDisabled(true);

        ImVec2_c btn = { 120, 0 };
        if (igButton("Reconnect", btn))
            oa2dp_action_reconnect_async(p->device_id);
        igSameLine(0, 8);
        if (igButton("Reset", btn))
            oa2dp_action_reset_async(p->device_id);

        if (busy) {
            igEndDisabled();
            igSameLine(0, 8);
            igText("Working...");
        }
    }

    igSeparator();

    /* Service toggles */
    {
        int busy = oa2dp_action_busy();
        if (busy) igBeginDisabled(true);

        igText("Services");
        ImVec2_c sbtn = { 80, 0 };

        igText("AudioSink (A2DP)");
        igSameLine(0, 8);
        if (igButton("Enable##as", sbtn))
            oa2dp_action_set_audiosink_async(p->device_id, 1);
        igSameLine(0, 4);
        if (igButton("Disable##as", sbtn))
            oa2dp_action_set_audiosink_async(p->device_id, 0);

        igText("Handsfree (HFP)");
        igSameLine(0, 8);
        if (igButton("Enable##hf", sbtn))
            oa2dp_action_set_handsfree_async(p->device_id, 1);
        igSameLine(0, 4);
        if (igButton("Disable##hf", sbtn))
            oa2dp_action_set_handsfree_async(p->device_id, 0);

        if (busy) igEndDisabled();
    }

    igSeparator();

    /* Watchdogs */
    {
        igText("Watchdogs");

        bool ah = (bool)p->auto_heal_enabled;
        if (igCheckbox("Auto-Heal: reconnect on connect-but-no-audio", &ah))
            p->auto_heal_enabled = ah;

        bool hw = (bool)p->hfp_watchdog_enabled;
        if (igCheckbox("HFP Watchdog: keep Handsfree disabled", &hw))
            p->hfp_watchdog_enabled = hw;
    }
}

/* ── Status panel ───────────────────────────────────────────────────── */

static void draw_status(OA2DP_UIState *ui)
{
    if (ui->selected < 0 || ui->selected >= ui->devices.count) {
        igText("No device selected.");
        return;
    }

    const OA2DP_DeviceProfile *p = &ui->devices.profiles[ui->selected];
    const OA2DP_DeviceStatus  *s = &ui->devices.statuses[ui->selected];

    /* Connection */
    {
        ImVec4_c col = conn_color(s->connection);
        igText("Connection:");
        igSameLine(0, 4);
        igTextColored(col, "%s", conn_labels[s->connection]);
    }

    /* Bluetooth address — useful for CLI mode and copy/paste. */
    {
        igText("Address:");
        igSameLine(0, 4);
        igTextDisabled("%s", p->device_id);
    }

    /* Active A2DP stack inferred from SCM service state. */
    {
        char stack_label[64];
        oa2dp_driver_active_stack_label(&ui->drivers,
                                        stack_label, sizeof(stack_label));
        igText("Stack:");
        igSameLine(0, 4);
        igTextDisabled("%s", stack_label);
    }

    igSeparator();

    /* Two-column layout for status fields.
     *
     * We only render fields we can actually measure.  Codec and the
     * SBC-internal parameters (bitpool, subbands, allocation method)
     * are not exposed by any user-mode Windows API, so we don't
     * pretend to know them — see docs/driver-evaluation.md.  The
     * sample rate / bit depth / channels come from WASAPI's mix
     * format and are real. */
    ImVec2_c tbl_size = { 0, 0 };
    if (igBeginTable("##statustbl", 2, ImGuiTableFlags_None, tbl_size, 0)) {
        igTableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 140, 0);
        igTableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0, 0);

        if (s->endpoint_name[0]) {
            igTableNextRow(0, 0);
            igTableNextColumn(); igText("WASAPI Endpoint");
            igTableNextColumn(); igText("%s", s->endpoint_name);
        }

        /* Active codec / SBC details — populated by the Alternative
         * A2DP Driver registry reader on the device probe.  Skipped
         * when active_codec is UNKNOWN, which is what the Microsoft
         * stack always shows because it doesn't expose negotiation
         * results in user mode. */
        if (s->active_codec != OA2DP_CODEC_UNKNOWN) {
            igTableNextRow(0, 0);
            igTableNextColumn(); igText("Active Codec");
            igTableNextColumn(); igText("%s", codec_labels[s->active_codec]);
        }
        if (s->active_codec == OA2DP_CODEC_SBC) {
            igTableNextRow(0, 0);
            igTableNextColumn(); igText("Stereo Mode");
            igTableNextColumn(); igText("%s", stereo_labels[s->stereo_mode]);

            igTableNextRow(0, 0);
            igTableNextColumn(); igText("Block Size");
            igTableNextColumn(); igText("%s", block_labels[s->block_size]);

            igTableNextRow(0, 0);
            igTableNextColumn(); igText("Allocation");
            igTableNextColumn(); igText("%s", alloc_labels[s->allocation_method]);

            igTableNextRow(0, 0);
            igTableNextColumn(); igText("Subbands");
            igTableNextColumn(); igText("%s", subband_labels[s->subbands]);

            if (s->bitpool > 0) {
                igTableNextRow(0, 0);
                igTableNextColumn(); igText("Max Bitpool");
                igTableNextColumn(); igText("%d", s->bitpool);
            }
        }

        /* Battery (populated by background probe).  -1 = pending,
         * -2 = device doesn't expose battery to Windows, 0..100 = real. */
        if (s->battery_pct >= 0) {
            igTableNextRow(0, 0);
            igTableNextColumn(); igText("Battery");
            igTableNextColumn();
            ImVec4_c col;
            if      (s->battery_pct >= 50) { col.x=0.2f; col.y=0.9f; col.z=0.2f; col.w=1.0f; }
            else if (s->battery_pct >= 20) { col.x=1.0f; col.y=0.8f; col.z=0.0f; col.w=1.0f; }
            else                            { col.x=1.0f; col.y=0.3f; col.z=0.3f; col.w=1.0f; }
            igTextColored(col, "%d%%", s->battery_pct);
        }

        /* Installed-services flags (populated by background probe).
         * -1 = not yet probed, treat as "unknown" and skip. */
        if (s->audio_sink_installed >= 0) {
            igTableNextRow(0, 0);
            igTableNextColumn(); igText("AudioSink (A2DP)");
            igTableNextColumn();
            ImVec4_c on  = { 0.2f, 0.9f, 0.2f, 1.0f };
            ImVec4_c off = { 0.6f, 0.6f, 0.6f, 1.0f };
            igTextColored(s->audio_sink_installed ? on : off,
                          "%s",
                          s->audio_sink_installed ? "installed" : "not installed");
        }
        if (s->handsfree_installed >= 0) {
            igTableNextRow(0, 0);
            igTableNextColumn(); igText("Handsfree (HFP)");
            igTableNextColumn();
            ImVec4_c on  = { 1.0f, 0.7f, 0.0f, 1.0f }; /* yellow — usually unwanted */
            ImVec4_c off = { 0.6f, 0.6f, 0.6f, 1.0f };
            igTextColored(s->handsfree_installed ? on : off,
                          "%s",
                          s->handsfree_installed ? "installed" : "not installed");
        }

        if (s->sample_rate > 0) {
            igTableNextRow(0, 0);
            igTableNextColumn(); igText("Sample Rate");
            igTableNextColumn(); igText("%d Hz", s->sample_rate);
        }

        if (s->bit_depth > 0) {
            igTableNextRow(0, 0);
            igTableNextColumn(); igText("Bit Depth");
            igTableNextColumn(); igText("%d-bit", s->bit_depth);
        }

        if (s->channels > 0) {
            igTableNextRow(0, 0);
            igTableNextColumn(); igText("Channels");
            igTableNextColumn(); igText("%d", s->channels);
        }

        if (s->estimated_bitrate_kbps > 0) {
            igTableNextRow(0, 0);
            igTableNextColumn(); igText("Endpoint Bitrate");
            igTableNextColumn(); igText("%d kbps", s->estimated_bitrate_kbps);
        }

        /* Codec bitrate from Alt A2DP Driver registry — distinct
         * from the WASAPI mix-format rate above. */
        if (s->codec_bitrate_kbps > 0) {
            igTableNextRow(0, 0);
            igTableNextColumn(); igText("Codec Bitrate");
            igTableNextColumn(); igText("%d kbps", s->codec_bitrate_kbps);
        }

        igEndTable();
    }

    /* If connected but WASAPI never gave us anything, say so explicitly
     * rather than showing an empty panel.  This is the symptom that
     * auto-heal looks for. */
    if (s->connection == OA2DP_CONN_CONNECTED && s->sample_rate == 0) {
        igSeparator();
        ImVec4_c warn = { 1.0f, 0.8f, 0.0f, 1.0f };
        igTextColored(warn, "Connected but no audio endpoint visible to WASAPI.");
        igTextWrapped("This is the Windows 11 \"connected-but-silent\" bug. "
                      "Enable Auto-Heal in Settings, or click Reconnect.");
    }

    /* ── Connection history (persistent across runs) ────────────── */
    igSeparator();
    igText("Recent connection events");
    {
        #define HIST_LINES 12
        #define HIST_LINE_LEN 64
        static char hist[HIST_LINES * HIST_LINE_LEN];
        int n = oa2dp_history_load(p->device_id, hist, HIST_LINES, HIST_LINE_LEN);
        if (n == 0) {
            igTextDisabled("(no history yet)");
        } else {
            ImVec2_c child_size = { 0, 120 };
            igBeginChild_Str("##histscroll", child_size, ImGuiChildFlags_Borders,
                             ImGuiWindowFlags_None);
            for (int i = n - 1; i >= 0; i--) {
                const char *line = hist + (size_t)i * HIST_LINE_LEN;
                /* Color: green for connect, gray for disconnect. */
                int is_conn = (strstr(line, "connected") != NULL &&
                               strstr(line, "disconnected") == NULL);
                ImVec4_c on  = { 0.5f, 0.9f, 0.5f, 1.0f };
                ImVec4_c off = { 0.7f, 0.7f, 0.7f, 1.0f };
                igTextColored(is_conn ? on : off, "%s", line);
            }
            igEndChild();
        }
    }
}

/* ── UI state init ──────────────────────────────────────────────────── */

void oa2dp_ui_state_init(OA2DP_UIState *ui)
{
    memset(ui, 0, sizeof(*ui));
    ui->selected = 0;
    ui->log_show_level[OA2DP_LOG_DEBUG] = 1;
    ui->log_show_level[OA2DP_LOG_INFO]  = 1;
    ui->log_show_level[OA2DP_LOG_WARN]  = 1;
    ui->log_show_level[OA2DP_LOG_ERROR] = 1;
    ui->log_auto_scroll = 1;
}

/* ── Log panel ──────────────────────────────────────────────────────── */

static const ImVec4_c log_colors[] = {
    { 0.6f, 0.6f, 0.6f, 1.0f },   /* DEBUG - gray */
    { 0.9f, 0.9f, 0.9f, 1.0f },   /* INFO  - white */
    { 1.0f, 0.8f, 0.0f, 1.0f },   /* WARN  - yellow */
    { 1.0f, 0.3f, 0.3f, 1.0f },   /* ERROR - red */
};

static const char *log_level_names[] = { "DEBUG", "INFO", "WARN", "ERROR" };

/*
 * Build a single newline-separated text dump of the log buffer
 * (filtered by the current UI severity toggles) and put it on the
 * system clipboard via cimgui.  Intended for "copy and paste into a
 * GitHub issue" workflows.
 *
 * Caller is responsible for ensuring filters are set the way the user
 * wants — we just dump whatever passes them.
 */
static void copy_log_to_clipboard(const OA2DP_UIState *ui)
{
    const OA2DP_LogBuffer *buf = oa2dp_log_get_buffer();
    if (!buf || buf->count == 0) {
        igSetClipboardText("OpenA2DP log is empty.\n");
        return;
    }

    /* Worst-case size: every entry up to ~600 chars + a small header.
     * Cheaper to over-allocate once than grow dynamically. */
    size_t cap = 256 + (size_t)buf->count * (OA2DP_LOG_MSG_MAX + 64);
    char *out = (char *)malloc(cap);
    if (!out) return;

    int n = snprintf(out, cap,
                     "OpenA2DP log dump (%d entries)\n"
                     "----------------------------------------\n",
                     buf->count);
    if (n < 0 || (size_t)n >= cap) { free(out); return; }
    size_t off = (size_t)n;

    int start = (buf->count < OA2DP_LOG_RING_SIZE) ? 0 : buf->head;

    for (int i = 0; i < buf->count; i++) {
        int idx = (start + i) % OA2DP_LOG_RING_SIZE;
        const OA2DP_LogEntry *e = &buf->entries[idx];

        if (e->level < OA2DP_LOG_COUNT && !ui->log_show_level[e->level])
            continue;

        struct tm tm_buf;
        localtime_s(&tm_buf, &e->timestamp);
        char ts[32];
        strftime(ts, sizeof(ts), "%H:%M:%S", &tm_buf);

        int written = snprintf(out + off, cap - off,
                               "[%s] [%-5s] %s\n",
                               ts, oa2dp_log_level_str(e->level), e->message);
        if (written < 0) break;
        off += (size_t)written;
        if (off >= cap - 1) break;
    }

    igSetClipboardText(out);
    free(out);
}

static void draw_log(OA2DP_UIState *ui)
{
    /* ── Toolbar row ────────────────────────────────────────────── */
    for (int lv = 0; lv < OA2DP_LOG_COUNT; lv++) {
        if (lv > 0) igSameLine(0, 8);
        bool show = (bool)ui->log_show_level[lv];
        igTextColored(log_colors[lv], "%s", log_level_names[lv]);
        igSameLine(0, 2);
        char label[32];
        snprintf(label, sizeof(label), "##logfilt%d", lv);
        if (igCheckbox(label, &show))
            ui->log_show_level[lv] = show;
    }

    igSameLine(0, 16);
    {
        bool as = (bool)ui->log_auto_scroll;
        igCheckbox("Auto-scroll", &as);
        ui->log_auto_scroll = as;
    }

    igSameLine(0, 16);
    {
        ImVec2_c btn = { 50, 0 };
        if (igButton("Clear", btn))
            oa2dp_log_clear();
    }

    igSameLine(0, 4);
    {
        ImVec2_c btn = { 130, 0 };
        if (igButton("Copy to Clipboard", btn)) {
            copy_log_to_clipboard(ui);
            oa2dp_log(OA2DP_LOG_INFO,
                      "log: copied filtered entries to clipboard");
        }
    }

    igSeparator();

    /* ── Log entries ────────────────────────────────────────────── */
    {
        ImVec2_c child_size = { 0, 0 };
        igBeginChild_Str("##logscroll", child_size, ImGuiChildFlags_None,
                         ImGuiWindowFlags_HorizontalScrollbar);

        const OA2DP_LogBuffer *buf = oa2dp_log_get_buffer();
        int start = (buf->count < OA2DP_LOG_RING_SIZE)
                        ? 0
                        : buf->head;

        for (int i = 0; i < buf->count; i++) {
            int idx = (start + i) % OA2DP_LOG_RING_SIZE;
            const OA2DP_LogEntry *e = &buf->entries[idx];

            /* Filter by level. */
            if (e->level < OA2DP_LOG_COUNT && !ui->log_show_level[e->level])
                continue;

            /* Format timestamp. */
            struct tm tm_buf;
            localtime_s(&tm_buf, &e->timestamp);
            char ts[32];
            strftime(ts, sizeof(ts), "%H:%M:%S", &tm_buf);

            /* Level color. */
            ImVec4_c col = log_colors[e->level < OA2DP_LOG_COUNT ? e->level : 0];

            char line[600];
            snprintf(line, sizeof(line), "[%s] [%-5s] %s",
                     ts, oa2dp_log_level_str(e->level), e->message);
            igTextColored(col, "%s", line);
        }

        /* Auto-scroll to bottom when enabled and near the end. */
        if (ui->log_auto_scroll && igGetScrollY() >= igGetScrollMaxY() - 20)
            igSetScrollHereY(1.0f);

        igEndChild();
    }
}

/* ── Main draw function ─────────────────────────────────────────────── */

void oa2dp_panels_draw(OA2DP_UIState *ui)
{
    /* Full-window dockspace-style layout */
    ImGuiViewport *vp = igGetMainViewport();
    igSetNextWindowPos(vp->WorkPos, ImGuiCond_Always, (ImVec2_c){0,0});
    igSetNextWindowSize(vp->WorkSize, ImGuiCond_Always);
    igBegin("##main", NULL,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

    /* ── Left: device list ──────────────────────────────────────── */
    draw_device_list(ui);

    igSameLine(0, 8);

    /* ── Right side: settings + status + log ────────────────────── */
    {
        ImVec2_c right_size = { 0, 0 };  /* fill remaining */
        igBeginChild_Str("##right", right_size, ImGuiChildFlags_None,
                         ImGuiWindowFlags_None);

        /* Top: settings and status side by side */
        {
            ImVec2_c avail = igGetContentRegionAvail();
            float top_h = avail.y * 0.65f;

            ImVec2_c top_size = { 0, top_h };
            igBeginChild_Str("##top", top_size, ImGuiChildFlags_None,
                             ImGuiWindowFlags_None);
            {
                ImVec2_c top_avail = igGetContentRegionAvail();
                float settings_w = top_avail.x * 0.55f;

                /* Settings */
                ImVec2_c settings_size = { settings_w, 0 };
                igBeginChild_Str("##settings", settings_size,
                                 ImGuiChildFlags_Borders,
                                 ImGuiWindowFlags_None);
                igText("Settings");
                igSeparator();
                draw_settings(ui);
                igEndChild();

                igSameLine(0, 4);

                /* Status */
                ImVec2_c status_size = { 0, 0 };
                igBeginChild_Str("##status", status_size,
                                 ImGuiChildFlags_Borders,
                                 ImGuiWindowFlags_None);
                igText("Status");
                igSeparator();
                draw_status(ui);
                igEndChild();
            }
            igEndChild();
        }

        /* Bottom: log */
        {
            ImVec2_c log_size = { 0, 0 };  /* fill remaining */
            igBeginChild_Str("##log", log_size,
                             ImGuiChildFlags_Borders,
                             ImGuiWindowFlags_None);
            igText("Log");
            igSeparator();
            draw_log(ui);
            igEndChild();
        }

        igEndChild();
    }

    igEnd();
}
