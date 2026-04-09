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
    p->allow_mono           = 1;
    p->allow_stereo         = 1;
    p->allow_16khz          = 0;
    p->allow_32khz          = 0;
    p->allow_44_1khz        = 1;
    p->allow_48khz          = 1;
    p->stereo_mode          = OA2DP_STEREO_JOINT;
    p->block_size           = OA2DP_BLOCK_16;
    p->allocation_method    = OA2DP_ALLOC_LOUDNESS;
    p->subbands             = OA2DP_SUBBANDS_8;
    p->override_bitpool     = 0;
    p->bitpool              = 53;   /* common high-quality SBC default */
    p->auto_reduce_bitpool  = 1;
    p->auto_heal_enabled    = 0;    /* opt-in: user enables per device once verified */
    p->hfp_watchdog_enabled = 0;    /* opt-in: keeps Handsfree disabled for headphones-only devices */
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
    p->allow_mono          = clamp_bool(p->allow_mono);
    p->allow_stereo        = clamp_bool(p->allow_stereo);
    p->allow_16khz         = clamp_bool(p->allow_16khz);
    p->allow_32khz         = clamp_bool(p->allow_32khz);
    p->allow_44_1khz       = clamp_bool(p->allow_44_1khz);
    p->allow_48khz         = clamp_bool(p->allow_48khz);
    p->override_bitpool    = clamp_bool(p->override_bitpool);
    p->auto_reduce_bitpool = clamp_bool(p->auto_reduce_bitpool);
    p->auto_heal_enabled    = clamp_bool(p->auto_heal_enabled);
    p->hfp_watchdog_enabled = clamp_bool(p->hfp_watchdog_enabled);

    /* At least one channel mode must be allowed. */
    if (!p->allow_mono && !p->allow_stereo)
        p->allow_stereo = 1;

    /* At least one sample rate must be allowed. */
    if (!p->allow_16khz && !p->allow_32khz && !p->allow_44_1khz && !p->allow_48khz)
        p->allow_44_1khz = 1;

    /* Bitpool range. */
    p->bitpool = clamp_int(p->bitpool, OA2DP_BITPOOL_MIN, OA2DP_BITPOOL_MAX);
}
