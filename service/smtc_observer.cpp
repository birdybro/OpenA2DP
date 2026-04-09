/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * smtc_observer.cpp - WinRT-based System Media Transport Controls
 * observer.  See oa2dp_smtc.h for the why.
 *
 * The model is dead-simple polling.  Every poll we ask the session
 * manager for the current session, then read its PlaybackInfo and
 * MediaProperties.  We diff against cached previous values and log
 * deltas.  No event subscription, no callbacks, no thread juggling.
 *
 * Why polling instead of event subscription:
 *   - Subscriptions need long-lived delegates and careful lifetime
 *     management with cppwinrt
 *   - The poll cost is genuinely tiny (a few WinRT vtable calls per
 *     200 ms tick)
 *   - The whole module is debug-tracking, not a hot path
 *
 * Compiles with /std:c++17.  Requires runtimeobject.lib for the
 * WinRT activation factory machinery.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

#include "oa2dp_smtc.h"
#include "oa2dp_log.h"

#include <stdio.h>
#include <string.h>

namespace wmc = winrt::Windows::Media::Control;

/* ── module state ───────────────────────────────────────────────────── */

/* Cached session manager — survives between polls.  cppwinrt types
 * are nullable; default-construct as nullptr to mean "not initialised". */
static wmc::GlobalSystemMediaTransportControlsSessionManager g_manager{nullptr};
static bool g_init_ok = false;

/* Last-seen state, used for delta detection. */
static int  g_last_status     = -1;          /* PlaybackStatus enum value */
static char g_last_title[256] = {0};
static char g_last_artist[256] = {0};
static char g_last_app[256]    = {0};

/* ── helpers ────────────────────────────────────────────────────────── */

static void hstring_to_utf8(const winrt::hstring &s, char *dst, size_t cap)
{
    if (!dst || cap == 0) return;
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1,
                                dst, (int)cap, nullptr, nullptr);
    if (n <= 0) dst[0] = '\0';
}

static const char *playback_status_name(
    wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus s)
{
    using ps = wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
    switch (s) {
    case ps::Closed:   return "closed";
    case ps::Opened:   return "opened";
    case ps::Changing: return "changing";
    case ps::Stopped:  return "stopped";
    case ps::Playing:  return "playing";
    case ps::Paused:   return "paused";
    default:           return "unknown";
    }
}

/* ── public API ─────────────────────────────────────────────────────── */

extern "C" int oa2dp_smtc_init(void)
{
    try {
        /* RequestAsync() returns an IAsyncOperation; .get() blocks
         * the calling thread until it completes.  Main-thread STA
         * is fine for this — the call is fast (<10 ms typically). */
        g_manager =
            wmc::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        if (g_manager) {
            g_init_ok = true;
            oa2dp_log(OA2DP_LOG_INFO,
                      "remote events: SMTC observer ready");
            return 0;
        }
    } catch (winrt::hresult_error const &ex) {
        oa2dp_log(OA2DP_LOG_WARN,
                  "remote events: SMTC init failed (hresult=0x%08x)",
                  (unsigned)ex.code());
    } catch (...) {
        oa2dp_log(OA2DP_LOG_WARN,
                  "remote events: SMTC init threw an unknown exception");
    }
    g_init_ok = false;
    return -1;
}

extern "C" void oa2dp_smtc_shutdown(void)
{
    if (g_init_ok) {
        g_manager = nullptr;
        g_init_ok = false;
    }
}

extern "C" void oa2dp_smtc_poll(void)
{
    if (!g_init_ok || !g_manager) return;

    try {
        auto session = g_manager.GetCurrentSession();
        if (!session) {
            /* No active media session.  Drop cached state so the
             * next session triggers a logged "started" event rather
             * than a silent first-time baseline. */
            return;
        }

        /* ── Source app (which app currently owns the SMTC session) */
        try {
            auto src_id = session.SourceAppUserModelId();
            char app_buf[256];
            hstring_to_utf8(src_id, app_buf, sizeof(app_buf));
            if (strcmp(app_buf, g_last_app) != 0) {
                if (g_last_app[0] != '\0') {
                    oa2dp_log(OA2DP_LOG_INFO,
                              "remote: SMTC session moved -> %s", app_buf);
                }
                snprintf(g_last_app, sizeof(g_last_app), "%s", app_buf);
            }
        } catch (...) {}

        /* ── Playback status (Playing / Paused / Stopped / etc.) */
        try {
            auto info = session.GetPlaybackInfo();
            auto status = info.PlaybackStatus();
            int status_int = (int)status;
            if (status_int != g_last_status) {
                if (g_last_status >= 0) {
                    oa2dp_log(OA2DP_LOG_INFO,
                              "remote: playback %s",
                              playback_status_name(status));
                }
                g_last_status = status_int;
            }
        } catch (...) {}

        /* ── Track metadata (title + artist) */
        try {
            auto media = session.TryGetMediaPropertiesAsync().get();
            if (media) {
                char title_buf[256], artist_buf[256];
                hstring_to_utf8(media.Title(),  title_buf,  sizeof(title_buf));
                hstring_to_utf8(media.Artist(), artist_buf, sizeof(artist_buf));

                if (strcmp(title_buf,  g_last_title)  != 0 ||
                    strcmp(artist_buf, g_last_artist) != 0) {
                    if (g_last_title[0] != '\0') {
                        oa2dp_log(OA2DP_LOG_INFO,
                                  "remote: track changed -> '%s' by '%s'",
                                  title_buf, artist_buf);
                    }
                    snprintf(g_last_title,  sizeof(g_last_title),  "%s", title_buf);
                    snprintf(g_last_artist, sizeof(g_last_artist), "%s", artist_buf);
                }
            }
        } catch (...) {
            /* TryGetMediaPropertiesAsync can throw on apps that don't
             * publish metadata.  Ignore — playback status alone is
             * still useful. */
        }
    } catch (winrt::hresult_error const &ex) {
        /* Don't spam — log once at DEBUG and move on. */
        static int once = 0;
        if (!once) {
            once = 1;
            oa2dp_log(OA2DP_LOG_DEBUG,
                      "remote events: SMTC poll error 0x%08x",
                      (unsigned)ex.code());
        }
    } catch (...) {}
}
