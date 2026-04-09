/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * altdriver_config.c - Read Alternative A2DP Driver per-device codec
 * config from its registry storage.  See oa2dp_altdriver_config.h
 * for the schema and the bit-encoding key.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "oa2dp_altdriver_config.h"

#include <stdio.h>
#include <string.h>

/* ── address formatting ─────────────────────────────────────────────── */

/* "5C:33:7B:64:67:8F" -> "00005c337b64678f" (16 lowercase hex chars
 * with four leading zeros, no separators).  Returns 0 on success. */
static int format_addr_for_registry(const char *device_id, char *out, int out_size)
{
    if (!device_id || !out || out_size < 17) return -1;

    char stripped[13] = {0};
    int j = 0;
    for (int i = 0; device_id[i] && j < 12; i++) {
        char c = device_id[i];
        if (c == ':') continue;
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        stripped[j++] = c;
    }
    if (j != 12) return -1;
    snprintf(out, out_size, "0000%s", stripped);
    return 0;
}

/* ── bit decoding ───────────────────────────────────────────────────── */

/* Index of the lowest set bit in `mask`, or -1 if `mask` is zero. */
static int lowest_set_bit(DWORD mask)
{
    if (mask == 0) return -1;
    int bit = 0;
    while (!(mask & 1u)) { mask >>= 1; bit++; }
    return bit;
}

/* ── registry value reader ──────────────────────────────────────────── */

static int read_dword(HKEY h, const wchar_t *name, DWORD *out)
{
    DWORD type = 0;
    DWORD size = sizeof(DWORD);
    DWORD val = 0;
    if (RegQueryValueExW(h, name, NULL, &type, (LPBYTE)&val, &size) != ERROR_SUCCESS)
        return -1;
    if (type != REG_DWORD) return -1;
    *out = val;
    return 0;
}

/* ── public API ─────────────────────────────────────────────────────── */

/* Decode an AAC sampling-frequency bit position to Hz.
 * Lowest bit = highest rate. */
static int decode_aac_freq_bit(int bit)
{
    switch (bit) {
    case 0:  return 96000;
    case 1:  return 88200;
    case 2:  return 64000;
    case 3:  return 48000;
    case 4:  return 44100;
    case 5:  return 32000;
    case 6:  return 24000;
    case 7:  return 22050;
    case 8:  return 16000;
    case 9:  return 12000;
    case 10: return 11025;
    case 11: return 8000;
    default: return 0;
    }
}

int oa2dp_altdriver_read_current(const char *device_id,
                                 OA2DP_DeviceStatus *status)
{
    if (!device_id || !status) return -1;

    char addr[20];
    if (format_addr_for_registry(device_id, addr, sizeof(addr)) != 0)
        return -1;

    /* Open both Current and Capability — Current has live negotiated
     * values, Capability has the device's max-supported AAC bitrate
     * which we use as a fallback when Current.AacBitrate is 0 (it
     * always seems to be on Kevin's setup). */
    wchar_t cur_path[256], cap_path[256];
    _snwprintf_s(cur_path, 256, _TRUNCATE,
        L"SYSTEM\\CurrentControlSet\\Services\\AltA2DP"
        L"\\Parameters\\Devices\\Current\\%hs", addr);
    _snwprintf_s(cap_path, 256, _TRUNCATE,
        L"SYSTEM\\CurrentControlSet\\Services\\AltA2DP"
        L"\\Parameters\\Devices\\Capability\\%hs", addr);

    HKEY hcur = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, cur_path, 0, KEY_READ, &hcur)
        != ERROR_SUCCESS)
        return -1;

    DWORD codec = 0;
    DWORD sbc_chmode = 0, sbc_freq = 0, sbc_alloc = 0, sbc_subbands = 0;
    DWORD sbc_blocklen = 0, sbc_max_bp = 0;
    DWORD aac_chmode = 0, aac_freq = 0;
    DWORD live_bitrate = 0;

    read_dword(hcur, L"Codec",                &codec);
    read_dword(hcur, L"SbcChannelMode",       &sbc_chmode);
    read_dword(hcur, L"SbcSamplingFrequency", &sbc_freq);
    read_dword(hcur, L"SbcAllocationMethod",  &sbc_alloc);
    read_dword(hcur, L"SbcSubbands",          &sbc_subbands);
    read_dword(hcur, L"SbcBlockLength",       &sbc_blocklen);
    read_dword(hcur, L"SbcMaximumBitpool",    &sbc_max_bp);
    read_dword(hcur, L"AacChannelMode",       &aac_chmode);
    read_dword(hcur, L"AacSamplingFrequency", &aac_freq);
    read_dword(hcur, L"Bitrate",              &live_bitrate);

    RegCloseKey(hcur);

    /* Decode codec.  bit 0 = SBC, bit 1 = AAC. */
    int codec_bit = lowest_set_bit(codec);
    if (codec_bit == 0)      status->active_codec = OA2DP_CODEC_SBC;
    else if (codec_bit == 1) status->active_codec = OA2DP_CODEC_AAC;
    else                     status->active_codec = OA2DP_CODEC_UNKNOWN;

    /* SBC-specific decoding. */
    if (status->active_codec == OA2DP_CODEC_SBC) {
        int b;

        b = lowest_set_bit(sbc_chmode);
        if      (b == 0) status->stereo_mode = OA2DP_STEREO_JOINT;
        else if (b == 1) status->stereo_mode = OA2DP_STEREO_STEREO;
        else if (b == 2) status->stereo_mode = OA2DP_STEREO_DUAL_CHANNEL;
        /* bit 3 = mono — no enum slot */

        b = lowest_set_bit(sbc_freq);
        if      (b == 0) status->sample_rate = 48000;
        else if (b == 1) status->sample_rate = 44100;
        else if (b == 2) status->sample_rate = 32000;
        else if (b == 3) status->sample_rate = 16000;

        b = lowest_set_bit(sbc_alloc);
        if      (b == 0) status->allocation_method = OA2DP_ALLOC_LOUDNESS;
        else if (b == 1) status->allocation_method = OA2DP_ALLOC_SNR;

        b = lowest_set_bit(sbc_subbands);
        if      (b == 0) status->subbands = OA2DP_SUBBANDS_8;
        else if (b == 1) status->subbands = OA2DP_SUBBANDS_4;

        b = lowest_set_bit(sbc_blocklen);
        if      (b == 0) status->block_size = OA2DP_BLOCK_16;
        else if (b == 1) status->block_size = OA2DP_BLOCK_12;
        else if (b == 2) status->block_size = OA2DP_BLOCK_8;
        else if (b == 3) status->block_size = OA2DP_BLOCK_4;

        if (sbc_max_bp > 0)
            status->bitpool = (int)sbc_max_bp;
    }

    /* AAC-specific decoding.  We don't have AAC enums on the
     * profile/status structs (they overlap with SBC fields like
     * stereo_mode), but we *can* fill channels and sample_rate
     * if WASAPI didn't already, and decode the bitrate. */
    if (status->active_codec == OA2DP_CODEC_AAC) {
        int b;

        b = lowest_set_bit(aac_chmode);
        if (b == 2 && status->channels == 0) status->channels = 2;
        if (b == 3 && status->channels == 0) status->channels = 1;

        b = lowest_set_bit(aac_freq);
        if (status->sample_rate == 0) {
            int hz = decode_aac_freq_bit(b);
            if (hz > 0) status->sample_rate = hz;
        }
    }

    /* Live over-the-air bitrate.  Current.Bitrate is populated for
     * both SBC and AAC sessions when audio is actively streaming.
     * If it's zero (paused / silent), fall back to Capability for
     * AAC since we know the device's max sustainable rate there. */
    if (live_bitrate > 0) {
        status->codec_bitrate_kbps = (int)(live_bitrate / 1000);
    } else if (status->active_codec == OA2DP_CODEC_AAC) {
        HKEY hcap = NULL;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, cap_path, 0, KEY_READ, &hcap)
            == ERROR_SUCCESS) {
            DWORD cap_aac_bitrate = 0;
            read_dword(hcap, L"AacBitrate", &cap_aac_bitrate);
            RegCloseKey(hcap);
            if (cap_aac_bitrate > 0)
                status->codec_bitrate_kbps = (int)(cap_aac_bitrate / 1000);
        }
    }

    return 0;
}
