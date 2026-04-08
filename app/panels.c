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

#include <stdio.h>
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

static void draw_device_list(OA2DP_UIState *ui)
{
    ImVec2_c size = { 200, 0 };
    igBeginChild_Str("##devlist", size, ImGuiChildFlags_Borders,
                     ImGuiWindowFlags_None);

    igText("Devices");
    igSeparator();

    for (int i = 0; i < ui->device_count; i++) {
        const OA2DP_DeviceProfile *p = &ui->profiles[i];
        const OA2DP_DeviceStatus  *s = &ui->statuses[i];

        ImVec4_c col = conn_color(s->connection);
        igTextColored(col, "%s", "(*)");
        igSameLine(0, 4);

        ImVec2_c sel_size = {0, 0};
        bool selected = (ui->selected == i);
        if (igSelectable_BoolPtr(p->display_name, &selected, 0, sel_size)) {
            ui->selected = i;
        }
    }

    igEndChild();
}

/* ── Settings panel (main panel) ────────────────────────────────────── */

static void draw_settings(OA2DP_UIState *ui)
{
    if (ui->selected < 0 || ui->selected >= ui->device_count) {
        igText("No device selected.");
        return;
    }

    OA2DP_DeviceProfile *p = &ui->profiles[ui->selected];

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
        ImVec2_c btn = { 120, 0 };
        if (igButton("Reconnect", btn)) {
            oa2dp_log(OA2DP_LOG_INFO, "reconnect requested for '%s'",
                      p->display_name);
        }
        igSameLine(0, 8);
        if (igButton("Reset", btn)) {
            oa2dp_log(OA2DP_LOG_INFO, "reset requested for '%s'",
                      p->display_name);
        }
    }
}

/* ── Status panel ───────────────────────────────────────────────────── */

static void draw_status(OA2DP_UIState *ui)
{
    if (ui->selected < 0 || ui->selected >= ui->device_count) {
        igText("No device selected.");
        return;
    }

    const OA2DP_DeviceStatus *s = &ui->statuses[ui->selected];

    /* Connection */
    {
        ImVec4_c col = conn_color(s->connection);
        igText("Connection:");
        igSameLine(0, 4);
        igTextColored(col, "%s", conn_labels[s->connection]);
    }

    igSeparator();

    /* Two-column layout for status fields */
    ImVec2_c tbl_size = { 0, 0 };
    if (igBeginTable("##statustbl", 2, ImGuiTableFlags_None, tbl_size, 0)) {
        igTableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 140, 0);
        igTableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0, 0);

        igTableNextRow(0, 0);
        igTableNextColumn(); igText("Active Codec");
        igTableNextColumn(); igText("%s", codec_labels[s->active_codec]);

        igTableNextRow(0, 0);
        igTableNextColumn(); igText("Sample Rate");
        igTableNextColumn(); igText("%d Hz", s->sample_rate);

        if (s->bit_depth > 0) {
            igTableNextRow(0, 0);
            igTableNextColumn(); igText("Bit Depth");
            igTableNextColumn(); igText("%d-bit", s->bit_depth);
        }

        igTableNextRow(0, 0);
        igTableNextColumn(); igText("Channels");
        igTableNextColumn(); igText("%d", s->channels);

        /* SBC-specific fields */
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

            igTableNextRow(0, 0);
            igTableNextColumn(); igText("Bitpool");
            igTableNextColumn(); igText("%d", s->bitpool);
        }

        if (s->estimated_bitrate_kbps > 0) {
            igTableNextRow(0, 0);
            igTableNextColumn(); igText("Est. Bitrate");
            igTableNextColumn(); igText("%d kbps", s->estimated_bitrate_kbps);
        }

        igEndTable();
    }
}

/* ── Log panel ──────────────────────────────────────────────────────── */

static const ImVec4_c log_colors[] = {
    { 0.6f, 0.6f, 0.6f, 1.0f },   /* DEBUG - gray */
    { 0.9f, 0.9f, 0.9f, 1.0f },   /* INFO  - white */
    { 1.0f, 0.8f, 0.0f, 1.0f },   /* WARN  - yellow */
    { 1.0f, 0.3f, 0.3f, 1.0f },   /* ERROR - red */
};

static void draw_log(void)
{
    const OA2DP_LogBuffer *buf = oa2dp_log_get_buffer();
    int start = (buf->count < OA2DP_LOG_RING_SIZE)
                    ? 0
                    : buf->head;

    for (int i = 0; i < buf->count; i++) {
        int idx = (start + i) % OA2DP_LOG_RING_SIZE;
        const OA2DP_LogEntry *e = &buf->entries[idx];

        /* Format timestamp */
        struct tm tm_buf;
        localtime_s(&tm_buf, &e->timestamp);
        char ts[32];
        strftime(ts, sizeof(ts), "%H:%M:%S", &tm_buf);

        /* Level color */
        ImVec4_c col = log_colors[e->level < OA2DP_LOG_COUNT ? e->level : 0];

        char line[600];
        snprintf(line, sizeof(line), "[%s] [%-5s] %s",
                 ts, oa2dp_log_level_str(e->level), e->message);
        igTextColored(col, "%s", line);
    }

    /* Auto-scroll to bottom */
    if (igGetScrollY() >= igGetScrollMaxY() - 20)
        igSetScrollHereY(1.0f);
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
                             ImGuiWindowFlags_HorizontalScrollbar);
            igText("Log");
            igSeparator();
            draw_log();
            igEndChild();
        }

        igEndChild();
    }

    igEnd();
}
