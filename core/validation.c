/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * validation.c - Profile defaults and validation / clamping
 */

#include "oa2dp_config.h"

#include <string.h>

/* SBC bitpool range per A2DP spec. */
#define OA2DP_BITPOOL_MIN 2
#define OA2DP_BITPOOL_MAX 250

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int clamp_bool(int v) { return (v != 0) ? 1 : 0; }

void oa2dp_profile_defaults(OA2DP_DeviceProfile *p)
{
    memset(p, 0, sizeof(*p));

    p->preferred_codec      = OA2DP_CODEC_SBC;
    p->allow_16khz          = 0;
    p->allow_32khz          = 0;
    p->allow_44_1khz        = 1;
    p->allow_48khz          = 1;
    p->stereo_mode          = OA2DP_STEREO_JOINT;
    p->block_size           = OA2DP_BLOCK_16;
    p->allocation_method    = OA2DP_ALLOC_LOUDNESS;
    p->subbands             = OA2DP_SUBBANDS_8;
    /* Bitpool default is intentionally high — the actual value used
     * gets clamped to the device's Capability.SbcMaximumBitpool at
     * write time unless the user explicitly enables the override.
     * This means new profiles default to "use device's max", which
     * is the safe and usually-best choice. */
    p->bitpool                  = 53;
    p->sbc_override_device_max  = 0;
    p->auto_heal_enabled    = 0;    /* opt-in: user enables per device once verified */
    p->hfp_watchdog_enabled = 0;    /* opt-in: keeps Handsfree disabled for headphones-only devices */

    /* AAC defaults */
    p->aac_bitrate_kbps     = 256;  /* common high-quality default */
    p->aac_allow_stereo     = 1;
    p->aac_allow_mono       = 0;
    p->aac_allow_44_1khz    = 1;
    p->aac_allow_48khz      = 1;
    p->abr_enable           = 1;    /* most modern devices benefit from ABR */
}

void oa2dp_profile_validate(OA2DP_DeviceProfile *p)
{
    /* Null-terminate strings defensively. */
    p->device_id[sizeof(p->device_id) - 1] = '\0';
    p->display_name[sizeof(p->display_name) - 1] = '\0';

    /* Enum ranges. */
    p->preferred_codec   = (OA2DP_CodecType)clamp_int((int)p->preferred_codec, 0, OA2DP_CODEC_COUNT - 1);
    p->stereo_mode       = (OA2DP_StereoMode)clamp_int((int)p->stereo_mode, 0, OA2DP_STEREO_COUNT - 1);
    p->block_size        = (OA2DP_BlockSize)clamp_int((int)p->block_size, 0, OA2DP_BLOCK_COUNT - 1);
    p->allocation_method = (OA2DP_AllocMethod)clamp_int((int)p->allocation_method, 0, OA2DP_ALLOC_COUNT - 1);
    p->subbands          = (OA2DP_Subbands)clamp_int((int)p->subbands, 0, OA2DP_SUBBANDS_COUNT - 1);

    /* Booleans. */
    p->allow_16khz          = clamp_bool(p->allow_16khz);
    p->allow_32khz          = clamp_bool(p->allow_32khz);
    p->allow_44_1khz        = clamp_bool(p->allow_44_1khz);
    p->allow_48khz          = clamp_bool(p->allow_48khz);
    p->auto_heal_enabled    = clamp_bool(p->auto_heal_enabled);
    p->hfp_watchdog_enabled = clamp_bool(p->hfp_watchdog_enabled);

    /* AAC clamps */
    p->aac_allow_stereo  = clamp_bool(p->aac_allow_stereo);
    p->aac_allow_mono    = clamp_bool(p->aac_allow_mono);
    p->aac_allow_44_1khz = clamp_bool(p->aac_allow_44_1khz);
    p->aac_allow_48khz   = clamp_bool(p->aac_allow_48khz);
    p->abr_enable             = clamp_bool(p->abr_enable);
    p->sbc_override_device_max = clamp_bool(p->sbc_override_device_max);
    if (!p->aac_allow_stereo && !p->aac_allow_mono)
        p->aac_allow_stereo = 1;
    if (!p->aac_allow_44_1khz && !p->aac_allow_48khz)
        p->aac_allow_48khz = 1;
    /* AAC bitrate range: 0 (sentinel) or 64..320 kbps. */
    if (p->aac_bitrate_kbps != 0) {
        if (p->aac_bitrate_kbps < 64)  p->aac_bitrate_kbps = 64;
        if (p->aac_bitrate_kbps > 320) p->aac_bitrate_kbps = 320;
    }

    /* At least one SBC sample rate must be allowed. */
    if (!p->allow_16khz && !p->allow_32khz && !p->allow_44_1khz && !p->allow_48khz)
        p->allow_44_1khz = 1;

    /* Bitpool range. */
    p->bitpool = clamp_int(p->bitpool, OA2DP_BITPOOL_MIN, OA2DP_BITPOOL_MAX);
}
