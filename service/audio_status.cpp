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

/* ── COM pointers ───────────────────────────────────────────────────── */

static IMMDeviceEnumerator *g_enumerator = nullptr;
static bool g_com_init = false;

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
    g_com_init = true;

    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void **)&g_enumerator);
    if (FAILED(hr) || !g_enumerator) {
        oa2dp_log(OA2DP_LOG_ERROR,
                  "audio status: failed to create MMDeviceEnumerator (0x%08X)",
                  (unsigned)hr);
        return -1;
    }

    oa2dp_log(OA2DP_LOG_INFO, "audio status: initialized");
    return 0;
}

extern "C" void oa2dp_audio_status_shutdown(void)
{
    if (g_enumerator) {
        g_enumerator->Release();
        g_enumerator = nullptr;
    }
    if (g_com_init) {
        CoUninitialize();
        g_com_init = false;
    }
    oa2dp_log(OA2DP_LOG_INFO, "audio status: shut down");
}

extern "C" int oa2dp_audio_status_query(const char *device_id,
                                        OA2DP_DeviceStatus *status)
{
    if (!g_enumerator || !device_id || !status)
        return -1;

    /* Build the search string from the BT address. */
    wchar_t search[32];
    bt_addr_to_search(device_id, search, 32);

    /* Enumerate active audio render endpoints. */
    IMMDeviceCollection *collection = nullptr;
    HRESULT hr = g_enumerator->EnumAudioEndpoints(
        eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr) || !collection)
        return -1;

    UINT count = 0;
    collection->GetCount(&count);

    bool found = false;

    for (UINT i = 0; i < count && !found; i++) {
        IMMDevice *device = nullptr;
        if (FAILED(collection->Item(i, &device)) || !device)
            continue;

        /* Get the endpoint ID string. */
        LPWSTR ep_id = nullptr;
        if (SUCCEEDED(device->GetId(&ep_id)) && ep_id) {
            if (wstr_icontains(ep_id, search)) {
                found = true;

                /* Query the mix format for sample rate / bit depth / channels. */
                IAudioClient *client = nullptr;
                hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                      nullptr, (void **)&client);
                if (SUCCEEDED(hr) && client) {
                    WAVEFORMATEX *wfx = nullptr;
                    if (SUCCEEDED(client->GetMixFormat(&wfx)) && wfx) {
                        status->sample_rate = (int)wfx->nSamplesPerSec;
                        status->channels    = (int)wfx->nChannels;

                        /* Bit depth: use wBitsPerSample, but for float
                         * formats report the container size. */
                        if (wfx->wBitsPerSample > 0)
                            status->bit_depth = (int)wfx->wBitsPerSample;

                        /* Estimate bitrate for PCM output path. */
                        status->estimated_bitrate_kbps =
                            (int)(wfx->nAvgBytesPerSec * 8 / 1000);

                        oa2dp_log(OA2DP_LOG_DEBUG,
                            "audio status: %s -> %d Hz, %d-bit, %d ch, ~%d kbps",
                            device_id,
                            status->sample_rate,
                            status->bit_depth,
                            status->channels,
                            status->estimated_bitrate_kbps);

                        CoTaskMemFree(wfx);
                    }
                    client->Release();
                }

                /* Also try to get the friendly name for logging. */
                IPropertyStore *props = nullptr;
                if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && props) {
                    PROPVARIANT pv;
                    PropVariantInit(&pv);
                    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv))) {
                        if (pv.vt == VT_LPWSTR && pv.pwszVal) {
                            char name[256];
                            WideCharToMultiByte(CP_UTF8, 0, pv.pwszVal, -1,
                                                name, sizeof(name), nullptr, nullptr);
                            oa2dp_log(OA2DP_LOG_DEBUG,
                                "audio status: matched endpoint '%s'", name);
                        }
                    }
                    PropVariantClear(&pv);
                    props->Release();
                }
            }
            CoTaskMemFree(ep_id);
        }
        device->Release();
    }

    collection->Release();
    return found ? 0 : -1;
}
