/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_altdriver_config.h - Read/decode the Alternative A2DP Driver
 * registry-stored per-device codec configuration.
 *
 * Schema discovered 2026-04-09 by inspecting Kevin's registry:
 *
 *   HKLM\SYSTEM\CurrentControlSet\Services\AltA2DP\Parameters\Devices\
 *     Capability\<addr>   what the device claims it supports (read-only,
 *                         from AVDTP advertisements)
 *     Current\<addr>      what's currently negotiated this session
 *                         (driver writes during connect)
 *     Next\<addr>         what to use on the next connection — this is
 *                         the user-editable preferences key
 *
 * <addr> is the BT address as 16 lowercase hex chars with leading
 * zeros and no separators (5C:33:7B:64:67:8F -> 00005c337b64678f).
 *
 * The Capability and Next subkeys store fields as bitfields (multiple
 * options simultaneously allowed).  The Current subkey stores them as
 * a single bit indicating what was actually selected.  Lower-numbered
 * bits represent higher-quality options, so the driver picks the
 * lowest set bit in (Capability AND Next).
 *
 * Bit assignments (lowest-bit-first):
 *   Codec:                bit0=SBC, bit1=AAC, bit2=LDAC, bit3=aptX,
 *                         bit4=aptX-HD, bit5=aptX-LL
 *   SbcChannelMode:       bit0=joint, bit1=stereo, bit2=dual, bit3=mono
 *   SbcSamplingFrequency: bit0=48k, bit1=44.1k, bit2=32k, bit3=16k
 *   SbcAllocationMethod:  bit0=loudness, bit1=SNR
 *   SbcSubbands:          bit0=8, bit1=4
 *   SbcBlockLength:       bit0=16, bit1=12, bit2=8, bit3=4
 *
 * AAC bit assignments not yet decoded — Kevin's device was on SBC
 * during the probe so we don't have a known-good Current value.
 *
 * Read works as a normal user.  Write requires elevation (Phase 2).
 */

#ifndef OA2DP_ALTDRIVER_CONFIG_H
#define OA2DP_ALTDRIVER_CONFIG_H

#include "oa2dp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Read the Devices\Current\<addr> subkey for the given device and
 * decode it into status->active_codec / stereo_mode / sample_rate /
 * allocation_method / subbands / block_size / bitpool.
 *
 * Returns 0 on success, -1 if the key doesn't exist (most likely
 * because Alternative A2DP Driver isn't installed or the device
 * isn't in its database).  On failure, status fields are left
 * unchanged.
 *
 * Safe to call from any thread.  Read-only.
 */
int oa2dp_altdriver_read_current(const char *device_id,
                                 OA2DP_DeviceStatus *status);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_ALTDRIVER_CONFIG_H */
