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
#include "oa2dp_log.h"

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
    DWORD delay_val = 0;

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
    read_dword(hcur, L"Delay",                &delay_val);

    RegCloseKey(hcur);

    if (delay_val > 0)
        status->latency_tenths_ms = (int)delay_val;

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
    }

    /* Always read the full Capability subkey — the device's max
     * SBC bitpool (safe ceiling), full supported codec/rate
     * bitfields for the capability panel, and fallback AAC
     * bitrate if Current.Bitrate was zero. */
    {
        HKEY hcap = NULL;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, cap_path, 0, KEY_READ, &hcap)
            == ERROR_SUCCESS) {
            DWORD cap_codecs = 0;
            DWORD cap_sbc_chmode = 0, cap_sbc_freq = 0;
            DWORD cap_sbc_min_bp = 0, cap_sbc_max_bp = 0;
            DWORD cap_aac_chmode = 0, cap_aac_freq = 0;
            DWORD cap_aac_bitrate = 0, cap_aac_peak = 0;

            read_dword(hcap, L"Codec",                &cap_codecs);
            read_dword(hcap, L"SbcChannelMode",       &cap_sbc_chmode);
            read_dword(hcap, L"SbcSamplingFrequency", &cap_sbc_freq);
            read_dword(hcap, L"SbcMinimumBitpool",    &cap_sbc_min_bp);
            read_dword(hcap, L"SbcMaximumBitpool",    &cap_sbc_max_bp);
            read_dword(hcap, L"AacChannelMode",       &cap_aac_chmode);
            read_dword(hcap, L"AacSamplingFrequency", &cap_aac_freq);
            read_dword(hcap, L"AacBitrate",           &cap_aac_bitrate);
            read_dword(hcap, L"AacPeakBitrate",       &cap_aac_peak);
            RegCloseKey(hcap);

            status->cap_codecs              = (int)cap_codecs;
            status->cap_sbc_chmode          = (int)cap_sbc_chmode;
            status->cap_sbc_freq            = (int)cap_sbc_freq;
            status->cap_sbc_min_bitpool     = (int)cap_sbc_min_bp;
            status->cap_aac_chmode          = (int)cap_aac_chmode;
            status->cap_aac_freq            = (int)cap_aac_freq;
            if (cap_aac_bitrate > 0)
                status->cap_aac_bitrate_kbps = (int)(cap_aac_bitrate / 1000);
            if (cap_aac_peak > 0)
                status->cap_aac_peak_bitrate_kbps = (int)(cap_aac_peak / 1000);
            if (cap_sbc_max_bp > 0)
                status->sbc_max_bitpool_capability = (int)cap_sbc_max_bp;
            if (live_bitrate == 0 && cap_aac_bitrate > 0 &&
                status->active_codec == OA2DP_CODEC_AAC) {
                status->codec_bitrate_kbps = (int)(cap_aac_bitrate / 1000);
            }
        }
    }

    return 0;
}

/* ── read Next subkey into snapshot ─────────────────────────────────── */

int oa2dp_altdriver_read_next(const char *device_id,
                              OA2DP_DeviceStatus *status)
{
    if (!device_id || !status) return -1;

    char addr[20];
    if (format_addr_for_registry(device_id, addr, sizeof(addr)) != 0)
        return -1;

    wchar_t path[256];
    _snwprintf_s(path, 256, _TRUNCATE,
        L"SYSTEM\\CurrentControlSet\\Services\\AltA2DP"
        L"\\Parameters\\Devices\\Next\\%hs", addr);

    HKEY h = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &h)
        != ERROR_SUCCESS)
        return -1;

    DWORD codec = 0, sbc_chmode = 0, sbc_freq = 0, sbc_alloc = 0;
    DWORD sbc_subbands = 0, sbc_blocklen = 0, sbc_max_bp = 0;
    DWORD aac_chmode = 0, aac_freq = 0, aac_bitrate = 0;
    DWORD abr = 0;

    read_dword(h, L"Codec",                &codec);
    read_dword(h, L"SbcChannelMode",       &sbc_chmode);
    read_dword(h, L"SbcSamplingFrequency", &sbc_freq);
    read_dword(h, L"SbcAllocationMethod",  &sbc_alloc);
    read_dword(h, L"SbcSubbands",          &sbc_subbands);
    read_dword(h, L"SbcBlockLength",       &sbc_blocklen);
    read_dword(h, L"SbcMaximumBitpool",    &sbc_max_bp);
    read_dword(h, L"AacChannelMode",       &aac_chmode);
    read_dword(h, L"AacSamplingFrequency", &aac_freq);
    read_dword(h, L"AacBitrate",           &aac_bitrate);
    read_dword(h, L"AbrEnable",            &abr);
    RegCloseKey(h);

    /* Decode codec preference. */
    int cb = lowest_set_bit(codec);
    status->snap_preferred_codec =
        (cb == 1) ? OA2DP_CODEC_AAC : OA2DP_CODEC_SBC;

    /* SBC channel mode (single bit chosen by user / driver). */
    {
        int b = lowest_set_bit(sbc_chmode);
        if      (b == 0) status->snap_stereo_mode = OA2DP_STEREO_JOINT;
        else if (b == 1) status->snap_stereo_mode = OA2DP_STEREO_STEREO;
        else if (b == 2) status->snap_stereo_mode = OA2DP_STEREO_DUAL_CHANNEL;
        else             status->snap_stereo_mode = OA2DP_STEREO_JOINT;
    }
    /* SBC sample-rate bitfield (multi-bit allowed). */
    status->snap_allow_48khz   = (sbc_freq & 0x1) ? 1 : 0;
    status->snap_allow_44_1khz = (sbc_freq & 0x2) ? 1 : 0;
    status->snap_allow_32khz   = (sbc_freq & 0x4) ? 1 : 0;
    status->snap_allow_16khz   = (sbc_freq & 0x8) ? 1 : 0;
    {
        int b = lowest_set_bit(sbc_alloc);
        status->snap_allocation_method =
            (b == 1) ? OA2DP_ALLOC_SNR : OA2DP_ALLOC_LOUDNESS;
    }
    {
        int b = lowest_set_bit(sbc_subbands);
        status->snap_subbands = (b == 1) ? OA2DP_SUBBANDS_4 : OA2DP_SUBBANDS_8;
    }
    {
        int b = lowest_set_bit(sbc_blocklen);
        if      (b == 0) status->snap_block_size = OA2DP_BLOCK_16;
        else if (b == 1) status->snap_block_size = OA2DP_BLOCK_12;
        else if (b == 2) status->snap_block_size = OA2DP_BLOCK_8;
        else if (b == 3) status->snap_block_size = OA2DP_BLOCK_4;
        else             status->snap_block_size = OA2DP_BLOCK_16;
    }
    status->snap_bitpool = (int)sbc_max_bp;

    /* AAC fields. */
    status->snap_aac_allow_stereo = (aac_chmode & (1u << 2)) ? 1 : 0;
    status->snap_aac_allow_mono   = (aac_chmode & (1u << 3)) ? 1 : 0;
    status->snap_aac_allow_48khz   = (aac_freq & (1u << 3)) ? 1 : 0;
    status->snap_aac_allow_44_1khz = (aac_freq & (1u << 4)) ? 1 : 0;

    /* AacBitrate: 0xFFFFFFFE is the "use default" sentinel.  Treat
     * it (and 0) as profile-side bitrate 0. */
    if (aac_bitrate == 0 || aac_bitrate == 0xFFFFFFFE)
        status->snap_aac_bitrate_kbps = 0;
    else
        status->snap_aac_bitrate_kbps = (int)(aac_bitrate / 1000);

    status->snap_abr_enable = (abr != 0) ? 1 : 0;

    status->alt_snapshot_valid = 1;
    return 0;
}

/* ── write Next subkey from profile ─────────────────────────────────── */

static int write_dword(HKEY h, const wchar_t *name, DWORD value)
{
    return (RegSetValueExW(h, name, 0, REG_DWORD,
                           (const BYTE *)&value, sizeof(value))
            == ERROR_SUCCESS) ? 0 : -1;
}

int oa2dp_altdriver_write_next(const char *device_id,
                               const OA2DP_DeviceProfile *profile,
                               int device_cap_max_bitpool)
{
    if (!device_id || !profile) return -1;

    char addr[20];
    if (format_addr_for_registry(device_id, addr, sizeof(addr)) != 0)
        return -1;

    wchar_t path[256];
    _snwprintf_s(path, 256, _TRUNCATE,
        L"SYSTEM\\CurrentControlSet\\Services\\AltA2DP"
        L"\\Parameters\\Devices\\Next\\%hs", addr);

    HKEY h = NULL;
    LONG open_rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0,
                                 KEY_READ | KEY_WRITE, &h);
    if (open_rc != ERROR_SUCCESS) {
        if (open_rc == ERROR_ACCESS_DENIED) {
            oa2dp_log(OA2DP_LOG_ERROR,
                      "altdriver write: ACCESS DENIED for %s — "
                      "relaunch OpenA2DP as Administrator",
                      device_id);
        } else {
            oa2dp_log(OA2DP_LOG_ERROR,
                      "altdriver write: open Next\\%s failed (err=%ld)",
                      device_id, open_rc);
        }
        return -1;
    }

    /* Codec field: bit 0 = SBC, bit 1 = AAC.  Set just the chosen one. */
    DWORD codec_mask = (profile->preferred_codec == OA2DP_CODEC_AAC)
                           ? (1u << 1) : (1u << 0);
    write_dword(h, L"Codec", codec_mask);

    /* SBC channel mode — single bit per enum value. */
    DWORD sbc_chmode = 0;
    switch (profile->stereo_mode) {
    case OA2DP_STEREO_JOINT:        sbc_chmode = 1u << 0; break;
    case OA2DP_STEREO_STEREO:       sbc_chmode = 1u << 1; break;
    case OA2DP_STEREO_DUAL_CHANNEL: sbc_chmode = 1u << 2; break;
    default:                        sbc_chmode = 1u << 0; break;
    }
    write_dword(h, L"SbcChannelMode", sbc_chmode);

    /* SBC sample-rate bitfield: multiple allowed simultaneously. */
    DWORD sbc_freq = 0;
    if (profile->allow_48khz)   sbc_freq |= 1u << 0;
    if (profile->allow_44_1khz) sbc_freq |= 1u << 1;
    if (profile->allow_32khz)   sbc_freq |= 1u << 2;
    if (profile->allow_16khz)   sbc_freq |= 1u << 3;
    if (sbc_freq == 0) sbc_freq = 1u << 0;  /* never write zero */
    write_dword(h, L"SbcSamplingFrequency", sbc_freq);

    /* SBC allocation method (single bit). */
    DWORD sbc_alloc = (profile->allocation_method == OA2DP_ALLOC_SNR)
                          ? (1u << 1) : (1u << 0);
    write_dword(h, L"SbcAllocationMethod", sbc_alloc);

    /* SBC subbands (single bit). */
    DWORD sbc_sub = (profile->subbands == OA2DP_SUBBANDS_4)
                        ? (1u << 1) : (1u << 0);
    write_dword(h, L"SbcSubbands", sbc_sub);

    /* SBC block length (single bit). */
    DWORD sbc_blk = 0;
    switch (profile->block_size) {
    case OA2DP_BLOCK_16: sbc_blk = 1u << 0; break;
    case OA2DP_BLOCK_12: sbc_blk = 1u << 1; break;
    case OA2DP_BLOCK_8:  sbc_blk = 1u << 2; break;
    case OA2DP_BLOCK_4:  sbc_blk = 1u << 3; break;
    default:             sbc_blk = 1u << 0; break;
    }
    write_dword(h, L"SbcBlockLength", sbc_blk);

    /* SBC bitpool max (integer, not bitfield).  Clamped to the
     * device's reported Capability max unless the user explicitly
     * enabled the override flag — exceeding the device's claimed
     * max can produce broken audio or damage cheaper BT chips. */
    int effective_bp = profile->bitpool;
    if (!profile->sbc_override_device_max &&
        device_cap_max_bitpool > 0 &&
        effective_bp > device_cap_max_bitpool) {
        oa2dp_log(OA2DP_LOG_INFO,
                  "altdriver write: clamping SbcMaximumBitpool %d -> %d "
                  "(device's reported max for %s, override disabled)",
                  effective_bp, device_cap_max_bitpool, device_id);
        effective_bp = device_cap_max_bitpool;
    }
    write_dword(h, L"SbcMaximumBitpool", (DWORD)effective_bp);

    /* AAC channel mode bitfield. */
    DWORD aac_chmode = 0;
    if (profile->aac_allow_stereo) aac_chmode |= 1u << 2;
    if (profile->aac_allow_mono)   aac_chmode |= 1u << 3;
    if (aac_chmode == 0) aac_chmode = 1u << 2;
    write_dword(h, L"AacChannelMode", aac_chmode);

    /* AAC sample-rate bitfield (only the two rates Pixel Buds Pro 2
     * supports for now — extending to other rates is just more bits
     * in the same field). */
    DWORD aac_freq = 0;
    if (profile->aac_allow_48khz)   aac_freq |= 1u << 3;
    if (profile->aac_allow_44_1khz) aac_freq |= 1u << 4;
    if (aac_freq == 0) aac_freq = 1u << 3;
    write_dword(h, L"AacSamplingFrequency", aac_freq);

    /* AAC bitrate.  0 in the profile means "use device default" → write
     * the 0xFFFFFFFE sentinel that Alt A2DP Driver expects. */
    DWORD aac_bitrate = (profile->aac_bitrate_kbps == 0)
                            ? 0xFFFFFFFEu
                            : (DWORD)(profile->aac_bitrate_kbps * 1000);
    write_dword(h, L"AacBitrate", aac_bitrate);

    /* ABR enable (applies to both codecs). */
    write_dword(h, L"AbrEnable", profile->abr_enable ? 1u : 0u);

    RegCloseKey(h);

    oa2dp_log(OA2DP_LOG_INFO,
              "altdriver write: pushed codec settings to Next\\%s "
              "(codec=%s, abr=%d, max_bp=%d, aac_kbps=%d)",
              device_id,
              profile->preferred_codec == OA2DP_CODEC_AAC ? "AAC" : "SBC",
              profile->abr_enable, profile->bitpool,
              profile->aac_bitrate_kbps);
    return 0;
}
