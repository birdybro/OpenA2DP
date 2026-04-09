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
#include "oa2dp_driver_control.h"

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

        /* Buttons are state-aware AND elevation-aware: even when the
         * service state would allow Start/Stop, both are greyed if the
         * process isn't elevated, since the call would just fail with
         * ACCESS_DENIED. */
        bool can_start = elevated && (svc->state == OA2DP_SVC_STOPPED);
        bool can_stop  = elevated && (svc->state == OA2DP_SVC_RUNNING ||
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

    igEndChild();
}

/* ── Settings panel (main panel) ────────────────────────────────────── */

static void draw_settings(OA2DP_UIState *ui)
{
    if (ui->selected < 0 || ui->selected >= ui->devices.count) {
        igText("No device selected.");
        return;
    }

    OA2DP_DeviceProfile *p = &ui->devices.profiles[ui->selected];

    igText("Profile: %s", p->display_name);
    igSeparator();

    /* Codec */
    {
        int codec = (int)p->preferred_codec;
        igSetNextItemWidth(150);
        if (igCombo_Str_arr("Codec", &codec, codec_labels, OA2DP_CODEC_COUNT, -1))
            p->preferred_codec = (OA2DP_CodecType)codec;
    }

    igSeparator();

    /* Channel modes */
    igText("Channels");
    {
        bool mono = (bool)p->allow_mono;
        bool stereo = (bool)p->allow_stereo;
        igCheckbox("Allow Mono", &mono);
        igSameLine(0, 16);
        igCheckbox("Allow Stereo", &stereo);
        p->allow_mono = mono;
        p->allow_stereo = stereo;
    }

    /* Sample rates */
    igText("Sample Rates");
    {
        bool r16 = (bool)p->allow_16khz;
        bool r32 = (bool)p->allow_32khz;
        bool r44 = (bool)p->allow_44_1khz;
        bool r48 = (bool)p->allow_48khz;
        igCheckbox("16 kHz", &r16);
        igSameLine(0, 10);
        igCheckbox("32 kHz", &r32);
        igSameLine(0, 10);
        igCheckbox("44.1 kHz", &r44);
        igSameLine(0, 10);
        igCheckbox("48 kHz", &r48);
        p->allow_16khz   = r16;
        p->allow_32khz   = r32;
        p->allow_44_1khz = r44;
        p->allow_48khz   = r48;
    }

    igSeparator();

    /* SBC parameters (only relevant for SBC codec) */
    {
        bool sbc_disabled = (p->preferred_codec != OA2DP_CODEC_SBC);
        if (sbc_disabled) igBeginDisabled(true);

        igText("SBC Parameters");

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

        igSeparator();

        /* Bitpool */
        {
            bool ovr = (bool)p->override_bitpool;
            igCheckbox("Override Bitpool", &ovr);
            p->override_bitpool = ovr;

            if (!ovr) igBeginDisabled(true);
            igSetNextItemWidth(200);
            igSliderInt("Bitpool", &p->bitpool, 2, 250, "%d", 0);
            if (!ovr) igEndDisabled();
        }

        {
            bool ar = (bool)p->auto_reduce_bitpool;
            igCheckbox("Auto-Reduce Bitpool", &ar);
            p->auto_reduce_bitpool = ar;
        }

        if (sbc_disabled) igEndDisabled();
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

        /* AudioSink/Handsfree install flags would go here, but
         * BluetoothEnumerateInstalledServices blocks the UI thread
         * for too long to call from this function — see the comment
         * in service/device_enum.c.  Until that's reworked off-thread
         * the install flags are not displayed. */

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
