/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * audio_visualizer.cpp - WASAPI loopback capture + Goertzel band split
 *
 * Captures the system default render endpoint in loopback mode
 * (i.e. whatever Windows is mixing for the speakers/headphones,
 * regardless of which device is currently active) and computes
 * OA2DP_VIS_BANDS log-spaced frequency band magnitudes per packet.
 *
 * The capture loop runs on its own background thread with its own
 * COM apartment.  Results are published into a critical-section
 * protected float array that the UI thread snapshots each frame.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <math.h>

#include "oa2dp_audio_visualizer.h"
#include "oa2dp_log.h"

extern "C" {

/* ── shared state ───────────────────────────────────────────────── */

#define VIS_SAMPLE_RING 4096

static HANDLE             g_thread        = NULL;
static volatile LONG      g_running       = 0;
static CRITICAL_SECTION   g_lock;
static int                g_lock_inited   = 0;
static float              g_bands[OA2DP_VIS_BANDS] = {0};

/* L/R sample ring buffers — used by the oscilloscope and
 * vectorscope visualizer modes.  Updated under g_lock from the
 * capture thread, snapshotted under g_lock by the UI thread. */
static float              g_samples_l[VIS_SAMPLE_RING];
static float              g_samples_r[VIS_SAMPLE_RING];
static int                g_samples_head  = 0;
static int                g_samples_count = 0;

/* Log-spaced target frequencies covering the audible range that
 * matters most for music visualizers. */
static const float kBandFreqs[OA2DP_VIS_BANDS] = {
      60.0f,   100.0f,   160.0f,   240.0f,
     360.0f,   540.0f,   800.0f,  1200.0f,
    1800.0f,  2700.0f,  4000.0f,  5700.0f,
    8000.0f, 11000.0f, 14000.0f, 17000.0f
};

/* ── DSP: Goertzel for each band ───────────────────────────────── */

/*
 * For each target frequency f, the Goertzel filter computes the
 * magnitude of that single DFT bin in O(N) time without doing a
 * full FFT.  Cheaper than FFT when you only need a handful of bins.
 *
 *   coeff = 2 * cos(2*pi*f / sr)
 *   q0 = sample + coeff*q1 - q2;  q2 = q1;  q1 = q0;
 *   |X(f)| = sqrt(q1*q1 + q2*q2 - q1*q2*coeff)
 */
static void compute_bands(const float *samples, int n, int sample_rate,
                          float *out_bands)
{
    if (n <= 0 || sample_rate <= 0) {
        for (int b = 0; b < OA2DP_VIS_BANDS; b++) out_bands[b] = 0.0f;
        return;
    }

    const float two_pi = 6.28318530718f;

    for (int b = 0; b < OA2DP_VIS_BANDS; b++) {
        float omega = two_pi * kBandFreqs[b] / (float)sample_rate;
        float coeff = 2.0f * cosf(omega);
        float q1 = 0.0f, q2 = 0.0f;

        for (int i = 0; i < n; i++) {
            float q0 = samples[i] + coeff * q1 - q2;
            q2 = q1;
            q1 = q0;
        }

        float mag = sqrtf(q1 * q1 + q2 * q2 - q1 * q2 * coeff) / (float)n;

        /* Convert to dB and map -60..0 dB to 0..1 for display. */
        float db = 20.0f * log10f(mag + 1e-9f);
        float v  = (db + 60.0f) / 60.0f;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;

        /* Mild perceptual curve to make low-energy content visible. */
        v = sqrtf(v);
        out_bands[b] = v;
    }
}

/* ── capture thread ─────────────────────────────────────────────── */

/*
 * Acquire and start a fresh WASAPI loopback capture against the
 * current default render endpoint.  All four out parameters are
 * set on success; the WAVEFORMATEX is owned by the caller and
 * must be freed with CoTaskMemFree.  Returns 0 on success, -1 on
 * failure (in which case nothing is left to clean up).
 */
static int loopback_acquire(IMMDeviceEnumerator **out_enum,
                            IMMDevice           **out_device,
                            IAudioClient        **out_client,
                            IAudioCaptureClient **out_capture,
                            WAVEFORMATEX        **out_fmt)
{
    *out_enum = NULL; *out_device = NULL; *out_client = NULL;
    *out_capture = NULL; *out_fmt = NULL;

    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice           *device     = NULL;
    IAudioClient        *client     = NULL;
    IAudioCaptureClient *capture    = NULL;
    WAVEFORMATEX        *fmt        = NULL;

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void **)&enumerator);
    if (FAILED(hr)) goto fail;

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) goto fail;

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL,
                          (void **)&client);
    if (FAILED(hr)) goto fail;

    hr = client->GetMixFormat(&fmt);
    if (FAILED(hr) || !fmt) goto fail;

    if (fmt->wBitsPerSample != 32) {
        oa2dp_log(OA2DP_LOG_WARN,
                  "visualizer: unsupported sample format (%u-bit)",
                  fmt->wBitsPerSample);
        goto fail;
    }

    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                             AUDCLNT_STREAMFLAGS_LOOPBACK,
                             10000000, /* 1s buffer */
                             0, fmt, NULL);
    if (FAILED(hr)) goto fail;

    hr = client->GetService(__uuidof(IAudioCaptureClient), (void **)&capture);
    if (FAILED(hr)) goto fail;

    hr = client->Start();
    if (FAILED(hr)) goto fail;

    *out_enum    = enumerator;
    *out_device  = device;
    *out_client  = client;
    *out_capture = capture;
    *out_fmt     = fmt;
    return 0;

fail:
    if (capture)    capture->Release();
    if (client)     client->Release();
    if (fmt)        CoTaskMemFree(fmt);
    if (device)     device->Release();
    if (enumerator) enumerator->Release();
    return -1;
}

static void loopback_release(IMMDeviceEnumerator *enumerator,
                             IMMDevice           *device,
                             IAudioClient        *client,
                             IAudioCaptureClient *capture,
                             WAVEFORMATEX        *fmt)
{
    if (client)     client->Stop();
    if (capture)    capture->Release();
    if (client)     client->Release();
    if (fmt)        CoTaskMemFree(fmt);
    if (device)     device->Release();
    if (enumerator) enumerator->Release();
}

static DWORD WINAPI capture_thread(LPVOID)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        oa2dp_log(OA2DP_LOG_ERROR, "visualizer: CoInitializeEx failed (0x%lx)",
                  (unsigned long)hr);
        return 1;
    }

    static float mono_buf[16384];

    /* Outer reconnect loop.  Each iteration acquires a fresh
     * loopback capture against the current default render endpoint
     * and runs the inner capture loop until something fails.  When
     * the inner loop breaks (typically AUDCLNT_E_DEVICE_INVALIDATED
     * after a reconnect cycle changes the default endpoint), we
     * release everything and re-acquire — so the visualizer
     * recovers automatically without restarting the app. */
    while (InterlockedCompareExchange(&g_running, 1, 1) == 1) {
        IMMDeviceEnumerator *enumerator = NULL;
        IMMDevice           *device     = NULL;
        IAudioClient        *client     = NULL;
        IAudioCaptureClient *capture    = NULL;
        WAVEFORMATEX        *fmt        = NULL;

        if (loopback_acquire(&enumerator, &device, &client,
                             &capture, &fmt) != 0) {
            /* No default endpoint right now (e.g. no audio device
             * is connected at all).  Wait a bit and retry. */
            Sleep(1000);
            continue;
        }

        /* Cache the endpoint ID we just bound to, so we can detect
         * the default render endpoint silently changing under us
         * (which doesn't trigger any WASAPI error — the old handle
         * just sits there returning zero packets indefinitely). */
        LPWSTR bound_endpoint_id = NULL;
        device->GetId(&bound_endpoint_id);
        DWORD last_default_check = GetTickCount();
        const DWORD DEFAULT_CHECK_INTERVAL_MS = 1000;

        oa2dp_log(OA2DP_LOG_INFO,
                  "visualizer: loopback capture started (%lu Hz, %u channels)",
                  (unsigned long)fmt->nSamplesPerSec,
                  (unsigned)fmt->nChannels);

        /* Inner capture loop. */
        bool default_changed = false;
        while (InterlockedCompareExchange(&g_running, 1, 1) == 1) {
            /* Periodically check if the system default render
             * endpoint has changed.  If it has, the audio our
             * loopback was capturing has been re-routed and we
             * need to re-acquire against the new default. */
            DWORD now = GetTickCount();
            if (bound_endpoint_id &&
                now - last_default_check >= DEFAULT_CHECK_INTERVAL_MS) {
                last_default_check = now;
                IMMDevice *cur = NULL;
                if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(
                        eRender, eConsole, &cur)) && cur) {
                    LPWSTR cur_id = NULL;
                    if (SUCCEEDED(cur->GetId(&cur_id)) && cur_id) {
                        if (wcscmp(cur_id, bound_endpoint_id) != 0) {
                            oa2dp_log(OA2DP_LOG_INFO,
                                "visualizer: default render endpoint "
                                "changed, re-acquiring");
                            default_changed = true;
                        }
                        CoTaskMemFree(cur_id);
                    }
                    cur->Release();
                }
                if (default_changed) break;
            }

            UINT32 packet_size = 0;
            hr = capture->GetNextPacketSize(&packet_size);
            if (FAILED(hr)) {
                oa2dp_log(OA2DP_LOG_INFO,
                    "visualizer: GetNextPacketSize failed (0x%08x), "
                    "re-acquiring", (unsigned)hr);
                break;
            }

            if (packet_size == 0) {
                Sleep(20);
                /* Decay the displayed bands while idle so they
                 * fall back to zero when audio stops. */
                EnterCriticalSection(&g_lock);
                for (int b = 0; b < OA2DP_VIS_BANDS; b++)
                    g_bands[b] *= 0.75f;
                LeaveCriticalSection(&g_lock);
                continue;
            }

            BYTE   *data   = NULL;
            UINT32  frames = 0;
            DWORD   flags  = 0;
            hr = capture->GetBuffer(&data, &frames, &flags, NULL, NULL);
            if (FAILED(hr)) {
                oa2dp_log(OA2DP_LOG_INFO,
                    "visualizer: GetBuffer failed (0x%08x), re-acquiring",
                    (unsigned)hr);
                break;
            }

            int n = (int)frames;
            if (n > (int)(sizeof(mono_buf) / sizeof(mono_buf[0])))
                n = (int)(sizeof(mono_buf) / sizeof(mono_buf[0]));

            const float *src = (const float *)data;
            int ch = fmt->nChannels;
            float inv_ch = 1.0f / (float)ch;

            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                for (int i = 0; i < n; i++) mono_buf[i] = 0.0f;
            } else {
                for (int i = 0; i < n; i++) {
                    float sum = 0.0f;
                    for (int c = 0; c < ch; c++)
                        sum += src[i * ch + c];
                    mono_buf[i] = sum * inv_ch;
                }
            }

            capture->ReleaseBuffer(frames);

            float new_bands[OA2DP_VIS_BANDS];
            compute_bands(mono_buf, n, (int)fmt->nSamplesPerSec, new_bands);

            /* One lock acquire updates BOTH the band envelope and
             * the raw L/R sample ring buffer. */
            EnterCriticalSection(&g_lock);

            for (int b = 0; b < OA2DP_VIS_BANDS; b++) {
                if (new_bands[b] > g_bands[b])
                    g_bands[b] = new_bands[b];
                else
                    g_bands[b] = g_bands[b] * 0.85f + new_bands[b] * 0.15f;
            }

            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                for (int i = 0; i < n; i++) {
                    int slot = g_samples_head;
                    g_samples_l[slot] = 0.0f;
                    g_samples_r[slot] = 0.0f;
                    g_samples_head = (g_samples_head + 1) % VIS_SAMPLE_RING;
                    if (g_samples_count < VIS_SAMPLE_RING) g_samples_count++;
                }
            } else {
                for (int i = 0; i < n; i++) {
                    float l = (ch >= 1) ? src[i * ch + 0] : 0.0f;
                    float r = (ch >= 2) ? src[i * ch + 1] : l;
                    int slot = g_samples_head;
                    g_samples_l[slot] = l;
                    g_samples_r[slot] = r;
                    g_samples_head = (g_samples_head + 1) % VIS_SAMPLE_RING;
                    if (g_samples_count < VIS_SAMPLE_RING) g_samples_count++;
                }
            }

            LeaveCriticalSection(&g_lock);
        }

        /* Inner loop exited — either the worker is shutting down,
         * WASAPI invalidated our handles, or the default endpoint
         * changed.  Tear down and either exit (shutdown) or
         * re-acquire. */
        if (bound_endpoint_id) CoTaskMemFree(bound_endpoint_id);
        loopback_release(enumerator, device, client, capture, fmt);

        if (InterlockedCompareExchange(&g_running, 1, 1) != 1)
            break;

        /* Brief delay before retry to avoid hot-spinning if the
         * default endpoint is briefly unavailable mid-reconnect. */
        Sleep(300);
    }

    oa2dp_log(OA2DP_LOG_INFO, "visualizer: capture stopped");
    CoUninitialize();
    return 0;
}

/* ── public API ─────────────────────────────────────────────────── */

int oa2dp_audio_visualizer_init(void)
{
    if (InterlockedCompareExchange(&g_running, 0, 0) != 0)
        return 0; /* already running */

    if (!g_lock_inited) {
        InitializeCriticalSection(&g_lock);
        g_lock_inited = 1;
    }

    InterlockedExchange(&g_running, 1);
    g_thread = CreateThread(NULL, 0, capture_thread, NULL, 0, NULL);
    if (!g_thread) {
        InterlockedExchange(&g_running, 0);
        oa2dp_log(OA2DP_LOG_ERROR, "visualizer: CreateThread failed");
        return -1;
    }
    return 0;
}

void oa2dp_audio_visualizer_shutdown(void)
{
    InterlockedExchange(&g_running, 0);
    if (g_thread) {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    if (g_lock_inited) {
        DeleteCriticalSection(&g_lock);
        g_lock_inited = 0;
    }
}

void oa2dp_audio_visualizer_get_bands(float *out, int count)
{
    if (!out || count <= 0) return;
    if (count > OA2DP_VIS_BANDS) count = OA2DP_VIS_BANDS;

    if (!g_lock_inited) {
        for (int i = 0; i < count; i++) out[i] = 0.0f;
        return;
    }
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < count; i++) out[i] = g_bands[i];
    LeaveCriticalSection(&g_lock);
}

void oa2dp_audio_visualizer_get_samples(float *out_l, float *out_r,
                                        int max_samples, int *out_count)
{
    if (out_count) *out_count = 0;
    if (!out_l || !out_r || !out_count || max_samples <= 0) return;
    if (!g_lock_inited) return;

    EnterCriticalSection(&g_lock);
    int n = g_samples_count;
    if (n > max_samples) n = max_samples;

    /* Copy the most recent n samples into out_l/out_r in
     * chronological order (oldest first). */
    int start = (g_samples_head - n + VIS_SAMPLE_RING) % VIS_SAMPLE_RING;
    for (int i = 0; i < n; i++) {
        int slot = (start + i) % VIS_SAMPLE_RING;
        out_l[i] = g_samples_l[slot];
        out_r[i] = g_samples_r[slot];
    }
    *out_count = n;
    LeaveCriticalSection(&g_lock);
}

} /* extern "C" */
