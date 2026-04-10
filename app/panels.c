/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * panels.c - UI panel drawing (device list, settings, status, log)
 */

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "panels.h"
#include "oa2dp_log.h"
#include "oa2dp_config.h"
#include "oa2dp_actions.h"
#include "oa2dp_altdriver_config.h"
#include "oa2dp_audio_visualizer.h"
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

/* Pack 8-bit RGBA into ImGui's ImU32 color format (ABGR on
 * little-endian, which is what cimgui expects).  Used by the audio
 * visualizer to colour bars without going through ImVec4 → U32. */
static ImU32 vis_rgba(int r, int g, int b, int a)
{
    return ((ImU32)(a & 0xFF) << 24) |
           ((ImU32)(b & 0xFF) << 16) |
           ((ImU32)(g & 0xFF) <<  8) |
           ((ImU32)(r & 0xFF));
}

/* Draw a WASAPI-loopback spectrum visualizer that fills the
 * available content region of its parent child window.  Reads
 * OA2DP_VIS_BANDS magnitudes from the audio_visualizer worker
 * (each in [0..1]) and renders them as colored bars. */
static void draw_audio_visualizer(void)
{
    const int   N   = OA2DP_VIS_BANDS;
    const float pad = 4.0f;
    const float gap = 1.0f;

    float bands[OA2DP_VIS_BANDS];
    oa2dp_audio_visualizer_get_bands(bands, N);

    ImVec2_c avail = igGetContentRegionAvail();
    float W = avail.x;
    float H = avail.y;
    if (W < 1.0f || H < 1.0f) return;

    ImVec2_c p0 = igGetCursorScreenPos();
    ImVec2_c p1 = { p0.x + W, p0.y + H };
    ImDrawList *dl = igGetWindowDrawList();

    /* Background panel + 1px border. */
    ImDrawList_AddRectFilled(dl, p0, p1, vis_rgba(12, 14, 22, 255), 4.0f, 0);
    ImDrawList_AddRect(dl, p0, p1, vis_rgba(60, 70, 90, 255), 4.0f, 0, 1.0f);

    float bar_w  = (W - pad * 2.0f - gap * (N - 1)) / (float)N;
    float bottom = p1.y - pad;
    float top    = p0.y + pad;
    float bar_h  = bottom - top;

    for (int i = 0; i < N; i++) {
        float v = bands[i];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;

        float h = bar_h * v;
        if (h < 1.0f && v > 0.0f) h = 1.0f;

        ImVec2_c b0 = { p0.x + pad + i * (bar_w + gap), bottom - h };
        ImVec2_c b1 = { b0.x + bar_w, bottom };

        /* Color gradient from cyan (low energy) → green → yellow → red. */
        int r  = (int)(40.0f  + 215.0f * v);
        int g  = (int)(220.0f - 100.0f * v * v);
        int bl = (int)(180.0f * (1.0f - v));
        ImU32 col = vis_rgba(r, g, bl, 255);

        ImDrawList_AddRectFilled(dl, b0, b1, col, 1.0f, 0);
    }

    /* Reserve the layout space so the child auto-sizes correctly. */
    igDummy((ImVec2_c){ W, H });
}

/* Compute the screen-space center of the main host window.  Used to
 * position modal popups that would otherwise drift to the wrong
 * monitor under ImGui's multi-viewport mode (where popup positions
 * are absolute screen coordinates, not host-relative). */
static ImVec2_c host_window_center(void *hwnd_void)
{
    ImVec2_c c = { 0.0f, 0.0f };
    if (!hwnd_void) {
        ImGuiViewport *vp = igGetMainViewport();
        c.x = vp->WorkPos.x + vp->WorkSize.x * 0.5f;
        c.y = vp->WorkPos.y + vp->WorkSize.y * 0.5f;
        return c;
    }
    RECT r;
    if (GetWindowRect((HWND)hwnd_void, &r)) {
        c.x = (float)((r.left + r.right) / 2);
        c.y = (float)((r.top + r.bottom) / 2);
    }
    return c;
}

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

    /* AltA2DP isn't shipped with Windows — only present if the user
     * installed it from bluetoothgoodies.com.  Detect by looking for
     * any service in the list whose name doesn't start with "btha2dp"
     * (case-insensitive). */
    int alt_installed = 0;
    for (int i = 0; i < ui->drivers.count; i++) {
        const char *n = ui->drivers.services[i].name;
        if (!n) continue;
        char head[8] = {0};
        for (int k = 0; k < 7 && n[k]; k++) {
            char c = n[k];
            if (c >= 'A' && c <= 'Z') c += 32;
            head[k] = c;
        }
        if (strcmp(head, "btha2dp") != 0) {
            alt_installed = 1;
            break;
        }
    }

    /* One-click stack switcher.  Buttons just request a confirm
     * dialog; the actual switch fires from the popup body so the
     * user has to explicitly say yes — switching kicks audio out
     * for ~30 seconds, easy to mis-click. */
    {
        bool ms_can_switch  = elevated && !switching;
        bool alt_can_switch = elevated && !switching && alt_installed;

        ImVec2_c btn = { 130, 0 };
        static int pending_target = -1; /* -1 = none, else OA2DP_StackTarget */

        if (!ms_can_switch) igBeginDisabled(true);
        if (igButton("Use Microsoft", btn)) {
            pending_target = OA2DP_STACK_MICROSOFT;
            igOpenPopup_Str("##confirm_stack_switch", 0);
        }
        if (!ms_can_switch) igEndDisabled();

        igSameLine(0, 4);

        if (!alt_can_switch) igBeginDisabled(true);
        if (igButton("Use AltA2DP", btn)) {
            pending_target = OA2DP_STACK_ALTERNATIVE;
            igOpenPopup_Str("##confirm_stack_switch", 0);
        }
        if (!alt_can_switch) igEndDisabled();

        /* Hover-help on the AltA2DP button explains why it's disabled
         * (or just what it does, when enabled). */
        if (igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            igBeginTooltip();
            igPushTextWrapPos(360.0f);
            if (!alt_installed) {
                igTextUnformatted(
                    "Alternative A2DP Driver is not installed.  Get it from "
                    "https://www.bluetoothgoodies.com/ to enable AAC, custom "
                    "codec settings, and live registry-based tuning.", NULL);
            } else if (!elevated) {
                igTextUnformatted(
                    "Switch the active A2DP stack to Alternative A2DP Driver.  "
                    "Requires running OpenA2DP as Administrator.", NULL);
            } else {
                igTextUnformatted(
                    "Switch the active A2DP stack to Alternative A2DP Driver.", NULL);
            }
            igPopTextWrapPos();
            igEndTooltip();
        }

        if (switching) {
            igSameLine(0, 8);
            ImVec4_c col = { 1.0f, 0.8f, 0.0f, 1.0f };
            igTextColored(col, "switching...");
        }

        if (!alt_installed) {
            ImVec4_c info = { 0.6f, 0.6f, 0.6f, 1.0f };
            igTextColored(info,
                "AltA2DP not installed — see bluetoothgoodies.com");
        }

        /* Modal confirmation popup. */
        ImVec2_c center = host_window_center(ui->hwnd);
        igSetNextWindowPos(center, ImGuiCond_Always, (ImVec2_c){0.5f, 0.5f});
        if (igBeginPopupModal("##confirm_stack_switch", NULL,
                              ImGuiWindowFlags_AlwaysAutoResize |
                              ImGuiWindowFlags_NoMove)) {
            const char *target_name =
                (pending_target == OA2DP_STACK_MICROSOFT)
                    ? "Microsoft (BthA2dp)" : "Alternative A2DP Driver";
            igText("Switch active A2DP stack to:");
            igText("    %s", target_name);
            igDummy((ImVec2_c){0, 6});
            igTextWrapped(
                "This will stop the currently running A2DP services, "
                "start the target stack's services, and reconnect every "
                "connected Bluetooth audio device. Audio will drop out "
                "for roughly 15-30 seconds.");
            igDummy((ImVec2_c){0, 6});

            ImVec2_c popbtn = { 120, 0 };
            if (igButton("Switch", popbtn)) {
                oa2dp_stack_switch_async((OA2DP_StackTarget)pending_target,
                                         &ui->drivers, &ui->devices);
                pending_target = -1;
                igCloseCurrentPopup();
            }
            igSameLine(0, 8);
            if (igButton("Cancel", popbtn)) {
                pending_target = -1;
                igCloseCurrentPopup();
            }
            igEndPopup();
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
    /* Wrap the device-list child + visualizer child in a Group so
     * they behave as a single layout item.  Without this, the
     * SameLine call after draw_device_list() returns would anchor
     * to the bottom-right of the visualizer (the last child drawn)
     * instead of the top-right of the device list, which would
     * push the right-hand panel down and leave a big empty gap. */
    igBeginGroup();

    /* Negative height = "available - |value|", reserving room for
     * the visualizer child that gets drawn below this one in the
     * same column. */
    const float vis_block_h = 90.0f;
    ImVec2_c size = { 280, -(vis_block_h + 4.0f) };
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

    if (ui->advanced_mode) {
        igSeparator();
        igDummy((ImVec2_c){0, 4});
        draw_drivers_section(ui);

        /* ── Activity counters ──────────────────────────────────── */
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
    }

    /* ── Reset settings button ──────────────────────────────────
     * Visible in both modes — useful escape hatch when a profile
     * gets borked.  Confirm modal prevents accidental clicks. */
    igSeparator();
    igDummy((ImVec2_c){0, 4});
    {
        ImVec2_c btn = { 240, 0 };
        if (igButton("Reset All Settings to Defaults", btn))
            igOpenPopup_Str("##confirm_reset_settings", 0);

        ImVec2_c center = host_window_center(ui->hwnd);
        igSetNextWindowPos(center, ImGuiCond_Always, (ImVec2_c){0.5f, 0.5f});
        if (igBeginPopupModal("##confirm_reset_settings", NULL,
                              ImGuiWindowFlags_AlwaysAutoResize |
                              ImGuiWindowFlags_NoMove)) {
            igTextWrapped(
                "Reset every device profile to its built-in defaults? "
                "This clears codec settings, watchdog opt-ins, and the "
                "bitpool override flag for all paired devices, then "
                "writes the defaults back to disk. The main window will "
                "also resize to its default 1440x900.");
            igDummy((ImVec2_c){0, 4});
            igTextDisabled(
                "Connection history is not affected.");
            igDummy((ImVec2_c){0, 6});

            ImVec2_c popbtn = { 120, 0 };
            if (igButton("Reset", popbtn)) {
                for (int i = 0; i < ui->devices.count; i++) {
                    OA2DP_DeviceProfile *prof = &ui->devices.profiles[i];

                    /* Preserve identity fields across the defaults
                     * memset — defaults() zeros device_id and
                     * display_name which we definitely want to keep. */
                    char saved_id[64];
                    char saved_name[128];
                    snprintf(saved_id, sizeof(saved_id), "%s", prof->device_id);
                    snprintf(saved_name, sizeof(saved_name), "%s", prof->display_name);

                    oa2dp_profile_defaults(prof);

                    snprintf(prof->device_id, sizeof(prof->device_id),
                             "%s", saved_id);
                    snprintf(prof->display_name, sizeof(prof->display_name),
                             "%s", saved_name);

                    char path[260];
                    if (oa2dp_config_path_for_device(prof->device_id,
                                                     path, sizeof(path)) == 0) {
                        oa2dp_profile_save(path, prof);
                    }
                }
                oa2dp_log(OA2DP_LOG_INFO,
                          "settings: reset %d device profile(s) to defaults",
                          ui->devices.count);

                /* Defer the actual SetWindowPos to after the frame
                 * ends — calling it from inside draw would synchronously
                 * fire WM_SIZE → render_one_frame → ImGui re-entry. */
                ui->pending_window_reset = 1;

                igCloseCurrentPopup();
            }
            igSameLine(0, 8);
            if (igButton("Cancel", popbtn)) {
                igCloseCurrentPopup();
            }
            igEndPopup();
        }
    }

    igEndChild();

    /* ── Audio visualizer in its own dedicated child below the
     * device list.  Fills 100% of the child's content region.
     * Visible in both simple and advanced modes — even with no
     * headphones connected you can see the system audio playback. */
    {
        ImVec2_c vis_size = { 280, 90 };
        igBeginChild_Str("##visualizer", vis_size, ImGuiChildFlags_Borders,
                         ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoScrollWithMouse);
        draw_audio_visualizer();
        igEndChild();
    }

    igEndGroup();
}

/* Push an amber FrameBg style for the next widget so the user can
 * see which codec field they've edited.  Pop after the widget. */
static void push_dirty_highlight(int dirty)
{
    if (!dirty) return;
    ImVec4_c bg  = { 0.45f, 0.35f, 0.0f, 1.0f };
    ImVec4_c bgh = { 0.55f, 0.42f, 0.0f, 1.0f };
    ImVec4_c bga = { 0.65f, 0.50f, 0.0f, 1.0f };
    igPushStyleColor_Vec4(ImGuiCol_FrameBg,        bg);
    igPushStyleColor_Vec4(ImGuiCol_FrameBgHovered, bgh);
    igPushStyleColor_Vec4(ImGuiCol_FrameBgActive,  bga);
}
static void pop_dirty_highlight(int dirty)
{
    if (dirty) igPopStyleColor(3);
}

/* Show a hover-help tooltip on the previous widget.  Wraps text
 * sensibly so longer explanations don't go off the right edge. */
static void hover_help(const char *text)
{
    if (igIsItemHovered(ImGuiHoveredFlags_None)) {
        igBeginTooltip();
        igPushTextWrapPos(360.0f);
        igTextUnformatted(text, NULL);
        igPopTextWrapPos();
        igEndTooltip();
    }
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

    /* ── Simple mode: just Reconnect / Reset, nothing else. ──────── */
    if (!ui->advanced_mode) {
        int busy = oa2dp_action_busy();
        if (busy) igBeginDisabled(true);

        ImVec2_c btn = { 140, 0 };
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

        igDummy((ImVec2_c){0, 6});
        igTextWrapped(
            "Reconnect re-establishes the A2DP audio link. Reset cycles "
            "all audio services on this device — try this if Reconnect "
            "alone doesn't fix the audio.");
        return;
    }

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
        int dirty = (p->preferred_codec != s->snap_preferred_codec);
        push_dirty_highlight(dirty);
        static const char *choices[] = { "SBC", "AAC" };
        int idx = (p->preferred_codec == OA2DP_CODEC_AAC) ? 1 : 0;
        igSetNextItemWidth(150);
        if (igCombo_Str_arr("Codec", &idx, choices, 2, -1))
            p->preferred_codec =
                (idx == 1) ? OA2DP_CODEC_AAC : OA2DP_CODEC_SBC;
        pop_dirty_highlight(dirty);
        hover_help(
            "SBC is the universal A2DP codec — every Bluetooth audio device "
            "supports it. AAC has noticeably better quality at the same bitrate "
            "and is supported by Apple devices, recent Android devices, Pixel "
            "Buds, and many wireless earbuds. Picking AAC on a device that "
            "doesn't support it just falls back to SBC.");
    }

    /* Codec-specific UI */
    if (p->preferred_codec == OA2DP_CODEC_SBC) {
        igText("Sample Rates");
        {
            bool r16 = (bool)p->allow_16khz;
            bool r32 = (bool)p->allow_32khz;
            bool r44 = (bool)p->allow_44_1khz;
            bool r48 = (bool)p->allow_48khz;
            int d16 = (p->allow_16khz   != s->snap_allow_16khz);
            int d32 = (p->allow_32khz   != s->snap_allow_32khz);
            int d44 = (p->allow_44_1khz != s->snap_allow_44_1khz);
            int d48 = (p->allow_48khz   != s->snap_allow_48khz);

            push_dirty_highlight(d16);
            igCheckbox("16 kHz", &r16);
            pop_dirty_highlight(d16);
            igSameLine(0, 10);

            push_dirty_highlight(d32);
            igCheckbox("32 kHz", &r32);
            pop_dirty_highlight(d32);
            igSameLine(0, 10);

            push_dirty_highlight(d44);
            igCheckbox("44.1 kHz", &r44);
            pop_dirty_highlight(d44);
            igSameLine(0, 10);

            push_dirty_highlight(d48);
            igCheckbox("48 kHz", &r48);
            pop_dirty_highlight(d48);

            p->allow_16khz = r16; p->allow_32khz = r32;
            p->allow_44_1khz = r44; p->allow_48khz = r48;
        }

        int dirty_sm = (p->stereo_mode != s->snap_stereo_mode);
        push_dirty_highlight(dirty_sm);
        int sm = (int)p->stereo_mode;
        igSetNextItemWidth(150);
        if (igCombo_Str_arr("Stereo Mode", &sm, stereo_labels, OA2DP_STEREO_COUNT, -1))
            p->stereo_mode = (OA2DP_StereoMode)sm;
        pop_dirty_highlight(dirty_sm);
        hover_help(
            "Joint Stereo gives the best compression for typical music by "
            "sharing some bits between left and right channels. Stereo and "
            "Dual Channel encode each channel separately — slightly bigger "
            "frames, no quality difference for most material.");

        int dirty_bs = (p->block_size != s->snap_block_size);
        push_dirty_highlight(dirty_bs);
        int bs = (int)p->block_size;
        igSetNextItemWidth(150);
        if (igCombo_Str_arr("Block Size", &bs, block_labels, OA2DP_BLOCK_COUNT, -1))
            p->block_size = (OA2DP_BlockSize)bs;
        pop_dirty_highlight(dirty_bs);
        hover_help(
            "Number of samples per SBC frame. Larger blocks = better "
            "compression efficiency but slightly higher encoding latency. "
            "16 is the typical high-quality choice.");

        int dirty_am = (p->allocation_method != s->snap_allocation_method);
        push_dirty_highlight(dirty_am);
        int am = (int)p->allocation_method;
        igSetNextItemWidth(150);
        if (igCombo_Str_arr("Allocation", &am, alloc_labels, OA2DP_ALLOC_COUNT, -1))
            p->allocation_method = (OA2DP_AllocMethod)am;
        pop_dirty_highlight(dirty_am);
        hover_help(
            "How SBC distributes bits across subbands. Loudness is preferred "
            "for music (perceptual model). SNR optimises raw signal-to-noise "
            "ratio and is rarely chosen.");

        int dirty_sb = (p->subbands != s->snap_subbands);
        push_dirty_highlight(dirty_sb);
        int sb = (int)p->subbands;
        igSetNextItemWidth(150);
        if (igCombo_Str_arr("Subbands", &sb, subband_labels, OA2DP_SUBBANDS_COUNT, -1))
            p->subbands = (OA2DP_Subbands)sb;
        pop_dirty_highlight(dirty_sb);
        hover_help(
            "Number of frequency subbands SBC splits the signal into. "
            "8 gives noticeably better quality than 4 for the same bitrate. "
            "Almost always choose 8.");

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

        /* Bitpool dirty uses the post-clamp effective value so the
         * highlight matches what would actually get written. */
        int effective_bp_for_dirty = p->bitpool;
        if (!p->sbc_override_device_max && dev_cap > 0 &&
            effective_bp_for_dirty > dev_cap)
            effective_bp_for_dirty = dev_cap;
        int dirty_bp = (effective_bp_for_dirty != s->snap_bitpool);
        push_dirty_highlight(dirty_bp);
        igSetNextItemWidth(200);
        igSliderInt("Max Bitpool", &p->bitpool, 2, slider_max, "%d", 0);
        pop_dirty_highlight(dirty_bp);
        hover_help(
            "SBC's main quality knob. Higher = more bits per frame = better "
            "audio at the cost of more Bluetooth bandwidth. The slider's max "
            "is your device's reported maximum (the safe ceiling). Going "
            "higher requires the override checkbox below.");

        /* Override toggle — gated on elevation. */
        {
            int can_toggle = elevated;
            if (!can_toggle) igBeginDisabled(true);
            bool ovr = (bool)p->sbc_override_device_max;
            igCheckbox("Override device max bitpool", &ovr);
            p->sbc_override_device_max = ovr;
            if (!can_toggle) igEndDisabled();
            hover_help(
                "DANGEROUS — disables the device-cap safety guard. Setting "
                "bitpool above the device's reported maximum can produce "
                "broken/garbled audio or, on some cheaper Bluetooth chips, "
                "physically damage them. Only enable if you understand the "
                "risk. Admin-only.");

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
            int dst = (p->aac_allow_stereo != s->snap_aac_allow_stereo);
            int dmo = (p->aac_allow_mono   != s->snap_aac_allow_mono);

            push_dirty_highlight(dst);
            igCheckbox("Allow Stereo##aac", &ast);
            pop_dirty_highlight(dst);
            igSameLine(0, 16);

            push_dirty_highlight(dmo);
            igCheckbox("Allow Mono##aac", &amo);
            pop_dirty_highlight(dmo);

            p->aac_allow_stereo = ast;
            p->aac_allow_mono   = amo;
        }
        igText("AAC Sample Rates");
        {
            bool r44 = (bool)p->aac_allow_44_1khz;
            bool r48 = (bool)p->aac_allow_48khz;
            int d44 = (p->aac_allow_44_1khz != s->snap_aac_allow_44_1khz);
            int d48 = (p->aac_allow_48khz   != s->snap_aac_allow_48khz);

            push_dirty_highlight(d44);
            igCheckbox("44.1 kHz##aac", &r44);
            pop_dirty_highlight(d44);
            igSameLine(0, 16);

            push_dirty_highlight(d48);
            igCheckbox("48 kHz##aac", &r48);
            pop_dirty_highlight(d48);

            p->aac_allow_44_1khz = r44;
            p->aac_allow_48khz   = r48;
        }
        /* 0 = "device default" sentinel; otherwise 64 to the device's
         * Capability.AacBitrate ceiling (or 320 if we don't know it). */
        if (p->aac_bitrate_kbps == 0) p->aac_bitrate_kbps = 256;
        int aac_slider_max = (s->cap_aac_bitrate_kbps > 0)
                                 ? s->cap_aac_bitrate_kbps : 320;
        if (aac_slider_max < 64) aac_slider_max = 320;
        if (p->aac_bitrate_kbps > aac_slider_max) p->aac_bitrate_kbps = aac_slider_max;
        int dirty_abr_kbps = (p->aac_bitrate_kbps != s->snap_aac_bitrate_kbps);
        push_dirty_highlight(dirty_abr_kbps);
        igSetNextItemWidth(200);
        igSliderInt("AAC Bitrate (kbps)", &p->aac_bitrate_kbps, 64, aac_slider_max, "%d", 0);
        pop_dirty_highlight(dirty_abr_kbps);
        hover_help(
            "Target AAC encode rate. Higher = better quality. The slider "
            "is capped to whatever your device claims it supports — pushing "
            "higher would just be ignored. 256 is the typical sweet spot "
            "for headphones.");
        if (s->cap_aac_bitrate_kbps > 0) {
            igTextDisabled("(device max: %d kbps)", s->cap_aac_bitrate_kbps);
        }
    }

    /* ABR Enable applies to both codecs. */
    {
        bool abr = (bool)p->abr_enable;
        int dirty_abr = (p->abr_enable != s->snap_abr_enable);
        push_dirty_highlight(dirty_abr);
        igCheckbox("Adaptive Bit Rate (ABR)", &abr);
        pop_dirty_highlight(dirty_abr);
        p->abr_enable = abr;
        hover_help(
            "Adaptive Bit Rate — let the driver lower the codec bitrate "
            "automatically when the Bluetooth link gets congested (e.g. "
            "interference, distance). Recommended on for most use cases.");
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
        hover_help(
            "When this device connects, wait a moment then check whether "
            "Windows actually created a WASAPI audio endpoint. If not (the "
            "Windows 11 connect-but-silent bug), automatically cycle "
            "AudioSink to recover. Capped at 3 attempts. Fires a tray "
            "notification on success or failure.");

        bool hw = (bool)p->hfp_watchdog_enabled;
        if (igCheckbox("HFP Watchdog: keep Handsfree disabled", &hw))
            p->hfp_watchdog_enabled = hw;
        hover_help(
            "Periodically re-disable the Handsfree (HFP) Bluetooth service "
            "on this device. Stops Windows from falling back to narrowband "
            "mono SCO when something accidentally re-enables HFP. "
            "Recommended for headphones-only use.");
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

    /* ── Simple mode: connection, battery, silent-bug warning. ──── */
    if (!ui->advanced_mode) {
        if (s->battery_pct >= 0) {
            igText("Battery:");
            igSameLine(0, 4);
            ImVec4_c col;
            if      (s->battery_pct >= 50) { col.x=0.2f; col.y=0.9f; col.z=0.2f; col.w=1.0f; }
            else if (s->battery_pct >= 20) { col.x=1.0f; col.y=0.8f; col.z=0.0f; col.w=1.0f; }
            else                            { col.x=1.0f; col.y=0.3f; col.z=0.3f; col.w=1.0f; }
            igTextColored(col, "%d%%", s->battery_pct);
        }
        if (s->connection == OA2DP_CONN_CONNECTED && s->endpoint_miss_count >= 2) {
            igSeparator();
            ImVec4_c warn = { 1.0f, 0.8f, 0.0f, 1.0f };
            igTextColored(warn, "Connected but no audio.");
            igTextWrapped("Click Reconnect to fix it.");
        }
        return;
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

        /* Negotiated audio latency from Alt A2DP Driver Current.Delay
         * (1/10 ms units, so 2800 = 280 ms). */
        if (s->latency_tenths_ms > 0) {
            igTableNextRow(0, 0);
            igTableNextColumn(); igText("Audio Latency");
            igTableNextColumn(); igText("%d ms", s->latency_tenths_ms / 10);
        }

        igEndTable();
    }

    /* ── Device Capabilities (from Alt A2DP Driver Capability\<addr>) */
    if (s->cap_codecs != 0) {
        igSeparator();
        igText("Device Capabilities");

        char buf[128];
        int pos = 0;
        if (s->cap_codecs & 0x01) pos += snprintf(buf + pos, sizeof(buf) - pos, "SBC ");
        if (s->cap_codecs & 0x02) pos += snprintf(buf + pos, sizeof(buf) - pos, "AAC ");
        if (s->cap_codecs & 0x04) pos += snprintf(buf + pos, sizeof(buf) - pos, "LDAC ");
        if (s->cap_codecs & 0x08) pos += snprintf(buf + pos, sizeof(buf) - pos, "aptX ");
        if (s->cap_codecs & 0x10) pos += snprintf(buf + pos, sizeof(buf) - pos, "aptX-HD ");
        if (s->cap_codecs & 0x20) pos += snprintf(buf + pos, sizeof(buf) - pos, "aptX-LL ");
        igTextDisabled("Codecs:    %s", buf[0] ? buf : "(none)");

        if (s->cap_sbc_freq != 0) {
            pos = 0; buf[0] = '\0';
            if (s->cap_sbc_freq & 0x01) pos += snprintf(buf + pos, sizeof(buf) - pos, "48 ");
            if (s->cap_sbc_freq & 0x02) pos += snprintf(buf + pos, sizeof(buf) - pos, "44.1 ");
            if (s->cap_sbc_freq & 0x04) pos += snprintf(buf + pos, sizeof(buf) - pos, "32 ");
            if (s->cap_sbc_freq & 0x08) pos += snprintf(buf + pos, sizeof(buf) - pos, "16 ");
            igTextDisabled("SBC rates: %skHz", buf);
        }
        if (s->sbc_max_bitpool_capability > 0) {
            int min_bp = s->cap_sbc_min_bitpool > 0 ? s->cap_sbc_min_bitpool : 2;
            igTextDisabled("SBC bitpool: %d-%d", min_bp, s->sbc_max_bitpool_capability);
        }
        if (s->cap_aac_freq != 0) {
            /* AAC freq bits — only label the rates the device actually supports. */
            pos = 0; buf[0] = '\0';
            static const struct { int bit; const char *label; } aac_rates[] = {
                {0, "96 "}, {1, "88.2 "}, {2, "64 "}, {3, "48 "}, {4, "44.1 "},
                {5, "32 "}, {6, "24 "}, {7, "22.05 "}, {8, "16 "},
                {9, "12 "}, {10, "11.025 "}, {11, "8 "}
            };
            for (size_t k = 0; k < sizeof(aac_rates) / sizeof(aac_rates[0]); k++) {
                if (s->cap_aac_freq & (1u << aac_rates[k].bit))
                    pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", aac_rates[k].label);
            }
            igTextDisabled("AAC rates: %skHz", buf);
        }
        if (s->cap_aac_bitrate_kbps > 0) {
            if (s->cap_aac_peak_bitrate_kbps > 0)
                igTextDisabled("AAC bitrate: max %d kbps (peak %d)",
                               s->cap_aac_bitrate_kbps, s->cap_aac_peak_bitrate_kbps);
            else
                igTextDisabled("AAC bitrate: max %d kbps", s->cap_aac_bitrate_kbps);
        }
    }

    /* If connected but WASAPI never gave us anything for several
     * consecutive polls, say so.  Debounced (>=2 misses) so transient
     * races during codec switches don't false-trigger the warning. */
    if (s->connection == OA2DP_CONN_CONNECTED && s->endpoint_miss_count >= 2) {
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

    /* ── Top header: active stack indicator + Advanced Mode toggle.
     * Stack indicator is advanced-only (it's noise for the 99%
     * audience) and lives on the left.  The Advanced Mode checkbox
     * is always visible and pinned to the right edge so the user
     * can toggle in either direction. */
    {
        if (ui->advanced_mode) {
            char stack_label[64];
            oa2dp_driver_active_stack_label(&ui->drivers,
                                            stack_label, sizeof(stack_label));
            igText("Active A2DP stack:");
            igSameLine(0, 6);
            /* Order matters: "Alternative ... (Microsoft also loaded)"
             * must match Alternative first, not Microsoft. */
            ImVec4_c col;
            if (strstr(stack_label, "Alternative")) {
                col.x = 0.3f; col.y = 0.9f; col.z = 0.5f; col.w = 1.0f;  /* green */
            } else if (strstr(stack_label, "Microsoft")) {
                col.x = 0.4f; col.y = 0.7f; col.z = 1.0f; col.w = 1.0f;  /* blue */
            } else {
                col.x = 1.0f; col.y = 0.4f; col.z = 0.4f; col.w = 1.0f;  /* red */
            }
            igTextColored(col, "%s", stack_label);
        } else {
            /* Reserve a row so the checkbox below sits on its own
             * line at a consistent vertical position. */
            igDummy((ImVec2_c){1, 1});
        }

        /* Pin "Advanced Mode" checkbox to the right edge.  Compute
         * its width from the label + checkbox glyph + style padding,
         * then SameLine to (cursor_x_now + remaining_avail - box_w). */
        ImVec2_c text_size = igCalcTextSize("Advanced Mode", NULL, false, -1.0f);
        ImGuiStyle *style = igGetStyle();
        float box_w = text_size.x
                    + igGetFrameHeight()           /* the check square */
                    + style->ItemInnerSpacing.x    /* gap between them */
                    + style->FramePadding.x * 2.0f;
        ImVec2_c avail = igGetContentRegionAvail();
        float cursor_x = igGetCursorPosX();
        float target_x = cursor_x + avail.x - box_w;
        if (target_x < cursor_x) target_x = cursor_x;
        igSameLine(target_x, 0);

        bool adv = (bool)ui->advanced_mode;
        if (igCheckbox("Advanced Mode", &adv))
            ui->advanced_mode = adv ? 1 : 0;
        hover_help(
            "Show codec settings, A2DP stack control, service toggles, "
            "watchdogs, device capabilities, connection history, and the "
            "diagnostic log. Off by default — most users only need "
            "Reconnect / Reset.");
        igSeparator();
    }

    /* ── Left: device list ──────────────────────────────────────── */
    draw_device_list(ui);

    igSameLine(0, 8);

    /* ── Right side: settings + status + log ────────────────────── */
    {
        ImVec2_c right_size = { 0, 0 };  /* fill remaining */
        igBeginChild_Str("##right", right_size, ImGuiChildFlags_None,
                         ImGuiWindowFlags_None);

        /* Top: settings and status side by side.  In simple mode the
         * log is hidden, so settings/status fill the entire right
         * side.  In advanced mode they take ~65% and the log gets
         * the rest. */
        {
            ImVec2_c avail = igGetContentRegionAvail();
            float top_h = ui->advanced_mode ? avail.y * 0.65f : avail.y;

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

        /* Bottom: log (advanced only). */
        if (ui->advanced_mode) {
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
