/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * audio_status.cpp - Query audio endpoint properties via MMDevice/WASAPI
 *
 * This is C++ because the MMDevice API is COM-based.  It exposes a
 * plain C interface via oa2dp_audio_status.h.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>

#include "oa2dp_audio_status.h"
#include "oa2dp_log.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

/* ── thread-local COM state ─────────────────────────────────────────
 *
 * Earlier versions cached a single IMMDeviceEnumerator created on the
 * main thread and reused it from worker threads (auto_heal especially).
 * That's technically undefined cross-apartment access — Windows let it
 * slide most of the time, but it could fail unpredictably.
 *
 * The current model:
 *   - oa2dp_audio_status_init  initialises COM on the *main* thread.
 *   - oa2dp_audio_status_query creates and releases its own enumerator
 *     for every call, so there's no cross-thread object handoff.
 *   - Worker threads that call audio_status_query MUST initialise COM
 *     for their own thread first (apartment-threaded), via the
 *     companion oa2dp_audio_status_thread_init / _thread_shutdown
 *     helpers below.
 */

static bool g_com_init_main = false;

/* ── helpers ────────────────────────────────────────────────────────── */

/*
 * Build a search string from a BT address like "AA:BB:CC:DD:EE:FF"
 * that we can look for inside endpoint device IDs.
 *
 * Windows audio endpoint IDs for Bluetooth devices typically contain
 * the BT address in various formats:
 *   - "aabbccddeeff"  (lowercase, no separators)
 *   - "aa:bb:cc:dd:ee:ff"
 *   - "AA_BB_CC_DD_EE_FF"  (some drivers)
 *
 * We search for the no-separator lowercase form since it's the
 * most common substring across all formats.
 */
static void bt_addr_to_search(const char *device_id, wchar_t *out, int out_len)
{
    /* Strip colons and lowercase. */
    char stripped[16] = {0};
    int j = 0;
    for (int i = 0; device_id[i] && j < 12; i++) {
        char c = device_id[i];
        if (c == ':') continue;
        if (c >= 'A' && c <= 'F') c += 32;
        stripped[j++] = c;
    }
    stripped[j] = '\0';

    MultiByteToWideChar(CP_UTF8, 0, stripped, -1, out, out_len);
}

/* Case-insensitive wide string search. */
static bool wstr_icontains(const wchar_t *haystack, const wchar_t *needle)
{
    if (!haystack || !needle) return false;
    size_t hlen = wcslen(haystack);
    size_t nlen = wcslen(needle);
    if (nlen > hlen) return false;

    for (size_t i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (size_t k = 0; k < nlen; k++) {
            wchar_t a = haystack[i + k];
            wchar_t b = needle[k];
            if (a >= L'A' && a <= L'Z') a += 32;
            if (b >= L'A' && b <= L'Z') b += 32;
            if (a != b) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

/* ── public API ─────────────────────────────────────────────────────── */

extern "C" int oa2dp_audio_status_init(void)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        oa2dp_log(OA2DP_LOG_ERROR, "audio status: CoInitializeEx failed (0x%08X)",
                  (unsigned)hr);
        return -1;
    }
    g_com_init_main = true;
    oa2dp_log(OA2DP_LOG_INFO, "audio status: initialized");
    return 0;
}

extern "C" void oa2dp_audio_status_shutdown(void)
{
    if (g_com_init_main) {
        CoUninitialize();
        g_com_init_main = false;
    }
    oa2dp_log(OA2DP_LOG_INFO, "audio status: shut down");
}

/* Per-thread COM init for worker threads that want to call
 * audio_status_query.  Returns 0 on success, -1 on failure (in which
 * case audio_status_query will also fail with -1 — the failure mode
 * is graceful). */
extern "C" int oa2dp_audio_status_thread_init(void)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE)
        return -1;
    return 0;
}

extern "C" void oa2dp_audio_status_thread_shutdown(void)
{
    CoUninitialize();
}

/* Read PKEY_Device_FriendlyName from a device into a wide buffer.
 * Returns true on success.  Caller's buffer must be at least 256 wchars. */
static bool get_friendly_name(IMMDevice *device, wchar_t *out, int out_len)
{
    if (!device || !out || out_len <= 0) return false;
    out[0] = L'\0';

    IPropertyStore *props = nullptr;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &props)) || !props)
        return false;

    bool ok = false;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv))) {
        if (pv.vt == VT_LPWSTR && pv.pwszVal) {
            wcsncpy_s(out, out_len, pv.pwszVal, _TRUNCATE);
            ok = true;
        }
    }
    PropVariantClear(&pv);
    props->Release();
    return ok;
}

/* Pull WASAPI mix format off an endpoint into status fields. */
static bool fill_mix_format(IMMDevice *device, OA2DP_DeviceStatus *status)
{
    IAudioClient *client = nullptr;
    HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                  nullptr, (void **)&client);
    if (FAILED(hr) || !client)
        return false;

    bool ok = false;
    WAVEFORMATEX *wfx = nullptr;
    if (SUCCEEDED(client->GetMixFormat(&wfx)) && wfx) {
        status->sample_rate = (int)wfx->nSamplesPerSec;
        status->channels    = (int)wfx->nChannels;
        if (wfx->wBitsPerSample > 0)
            status->bit_depth = (int)wfx->wBitsPerSample;
        status->estimated_bitrate_kbps =
            (int)(wfx->nAvgBytesPerSec * 8 / 1000);
        CoTaskMemFree(wfx);
        ok = true;
    }
    client->Release();
    return ok;
}

extern "C" int oa2dp_audio_status_query(const char *device_id,
                                        const char *display_name,
                                        OA2DP_DeviceStatus *status)
{
    if (!device_id || !status)
        return -1;

    /* Build address-search string (e.g. "aabbccddeeff"). */
    wchar_t addr_search[32];
    bt_addr_to_search(device_id, addr_search, 32);

    /* Build name-search string (wide UTF-16) if a display name was given. */
    wchar_t name_search[128];
    name_search[0] = L'\0';
    bool have_name = false;
    if (display_name && display_name[0]) {
        if (MultiByteToWideChar(CP_UTF8, 0, display_name, -1,
                                name_search, 128) > 0)
            have_name = true;
    }

    /* Create a fresh enumerator for this call, owned by the calling
     * thread's apartment.  Avoids the cross-apartment-access bug we
     * had when caching one on the main thread and using it from
     * worker threads.  Cost: a few hundred microseconds per call. */
    IMMDeviceEnumerator *enumerator = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void **)&enumerator);
    if (FAILED(hr) || !enumerator)
        return -1;

    /* Enumerate active audio render endpoints. */
    IMMDeviceCollection *collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(
        eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr) || !collection) {
        enumerator->Release();
        return -1;
    }

    UINT count = 0;
    collection->GetCount(&count);

    /* Two-pass strategy: pass 1 by BT address in endpoint ID,
     * pass 2 by display-name substring in friendly name.
     * If both passes miss, log every endpoint we saw so the user
     * can tell us what their stack is calling things. */
    bool found = false;
    wchar_t matched_name[256] = {0};

    /* ── Pass 1: BT address in endpoint ID ─────────────────────── */
    for (UINT i = 0; i < count && !found; i++) {
        IMMDevice *device = nullptr;
        if (FAILED(collection->Item(i, &device)) || !device)
            continue;

        LPWSTR ep_id = nullptr;
        if (SUCCEEDED(device->GetId(&ep_id)) && ep_id) {
            if (wstr_icontains(ep_id, addr_search)) {
                if (fill_mix_format(device, status)) {
                    get_friendly_name(device, matched_name, 256);
                    found = true;
                }
            }
            CoTaskMemFree(ep_id);
        }
        device->Release();
    }

    /* ── Pass 2: display name in PKEY_Device_FriendlyName ──────── */
    if (!found && have_name) {
        for (UINT i = 0; i < count && !found; i++) {
            IMMDevice *device = nullptr;
            if (FAILED(collection->Item(i, &device)) || !device)
                continue;

            wchar_t fname[256];
            if (get_friendly_name(device, fname, 256)) {
                if (wstr_icontains(fname, name_search)) {
                    if (fill_mix_format(device, status)) {
                        wcsncpy_s(matched_name, 256, fname, _TRUNCATE);
                        found = true;
                    }
                }
            }
            device->Release();
        }
    }

    if (found) {
        char nameu[256] = {0};
        WideCharToMultiByte(CP_UTF8, 0, matched_name, -1,
                            nameu, sizeof(nameu), nullptr, nullptr);
        snprintf(status->endpoint_name, sizeof(status->endpoint_name),
                 "%s", nameu);
        oa2dp_log(OA2DP_LOG_DEBUG,
            "audio status: %s -> '%s' %d Hz, %d-bit, %d ch, ~%d kbps",
            device_id, nameu,
            status->sample_rate, status->bit_depth,
            status->channels, status->estimated_bitrate_kbps);
    } else {
        status->endpoint_name[0] = '\0';
        /* Diagnostic dump — list every render endpoint we saw so we
         * can figure out what the user's stack is naming things.
         * Logged at INFO so it shows up by default once. */
        oa2dp_log(OA2DP_LOG_INFO,
            "audio status: no endpoint match for %s ('%s'). Enumerated render endpoints:",
            device_id, display_name ? display_name : "(no name)");

        for (UINT i = 0; i < count; i++) {
            IMMDevice *device = nullptr;
            if (FAILED(collection->Item(i, &device)) || !device)
                continue;

            wchar_t fname[256];
            char fnameu[256] = {0};
            LPWSTR ep_id = nullptr;
            char ep_idu[512] = {0};

            get_friendly_name(device, fname, 256);
            WideCharToMultiByte(CP_UTF8, 0, fname, -1,
                                fnameu, sizeof(fnameu), nullptr, nullptr);

            if (SUCCEEDED(device->GetId(&ep_id)) && ep_id) {
                WideCharToMultiByte(CP_UTF8, 0, ep_id, -1,
                                    ep_idu, sizeof(ep_idu), nullptr, nullptr);
                CoTaskMemFree(ep_id);
            }

            oa2dp_log(OA2DP_LOG_INFO,
                "  [%u] '%s' (%s)", i, fnameu, ep_idu);

            device->Release();
        }
    }

    collection->Release();
    enumerator->Release();
    return found ? 0 : -1;
}
