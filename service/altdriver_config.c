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

int oa2dp_altdriver_read_current(const char *device_id,
                                 OA2DP_DeviceStatus *status)
{
    if (!device_id || !status) return -1;

    char addr[20];
    if (format_addr_for_registry(device_id, addr, sizeof(addr)) != 0)
        return -1;

    /* Build the wide registry path. */
    wchar_t path[256];
    _snwprintf_s(path, 256, _TRUNCATE,
        L"SYSTEM\\CurrentControlSet\\Services\\AltA2DP"
        L"\\Parameters\\Devices\\Current\\%hs", addr);

    HKEY h = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &h)
        != ERROR_SUCCESS)
        return -1;

    DWORD codec = 0;
    DWORD sbc_chmode = 0, sbc_freq = 0, sbc_alloc = 0, sbc_subbands = 0;
    DWORD sbc_blocklen = 0, sbc_max_bp = 0;

    read_dword(h, L"Codec",                 &codec);
    read_dword(h, L"SbcChannelMode",        &sbc_chmode);
    read_dword(h, L"SbcSamplingFrequency",  &sbc_freq);
    read_dword(h, L"SbcAllocationMethod",   &sbc_alloc);
    read_dword(h, L"SbcSubbands",           &sbc_subbands);
    read_dword(h, L"SbcBlockLength",        &sbc_blocklen);
    read_dword(h, L"SbcMaximumBitpool",     &sbc_max_bp);

    RegCloseKey(h);

    /* Decode codec. */
    int codec_bit = lowest_set_bit(codec);
    if (codec_bit == 0)      status->active_codec = OA2DP_CODEC_SBC;
    else if (codec_bit == 1) status->active_codec = OA2DP_CODEC_AAC;
    else                     status->active_codec = OA2DP_CODEC_UNKNOWN;

    /* SBC-specific fields are only meaningful when SBC is active. */
    if (status->active_codec == OA2DP_CODEC_SBC) {
        int b;

        b = lowest_set_bit(sbc_chmode);
        if      (b == 0) status->stereo_mode = OA2DP_STEREO_JOINT;
        else if (b == 1) status->stereo_mode = OA2DP_STEREO_STEREO;
        else if (b == 2) status->stereo_mode = OA2DP_STEREO_DUAL_CHANNEL;
        /* bit 3 = mono — no enum slot, channels=1 will signal it */

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

    return 0;
}
