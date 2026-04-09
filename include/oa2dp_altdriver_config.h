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
 * AAC bit assignments confirmed 2026-04-09 against Kevin's
 * Pixel Buds Pro 2 once we got him to actually disconnect/reconnect
 * for the AAC switch to apply:
 *
 *   AacChannelMode:       bit2=stereo (2ch), bit3=mono (1ch)
 *                         (note: SBC uses bits 0-3 for joint/stereo/
 *                          dual/mono, so AAC's 2-channel sits at
 *                          bit 2 — not bit 0 or 1)
 *   AacSamplingFrequency: 12-bit field, lowest bit = highest rate:
 *                         bit0=96k, bit1=88.2k, bit2=64k, bit3=48k,
 *                         bit4=44.1k, bit5=32k, bit6=24k, bit7=22.05k,
 *                         bit8=16k, bit9=12k, bit10=11.025k, bit11=8k
 *
 * The general Current.Bitrate field holds the live over-the-air bps
 * for whichever codec is active (SBC or AAC).  Current.AacBitrate
 * appears to always be 0 — the driver only populates the SBC-named
 * Bitrate field for both codecs.
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

/*
 * Read the Devices\Next\<addr> subkey for the given device and
 * decode it into the snap_* fields of `status`.  Sets
 * status->alt_snapshot_valid = 1 on success.  Used by the settings
 * panel as the "last applied" baseline for dirty detection.
 *
 * Returns 0 on success, -1 if the key doesn't exist.  Safe from
 * any thread.  Read-only.
 */
int oa2dp_altdriver_read_next(const char *device_id,
                              OA2DP_DeviceStatus *status);

/*
 * Write the codec-relevant fields of `profile` to the
 * Devices\Next\<addr> subkey.  Encodes enums back to the bitfield
 * positions documented at the top of this header.  Per-key writes
 * (RegSetValueExW), so unrelated values like LDAC, aptX, and
 * VolumeLevel in the same subkey are preserved.
 *
 * After this call the user must physically reconnect the device
 * (turn off and on, or call oa2dp_action_reconnect) for the new
 * Next values to take effect — the driver only consults Next at
 * AVDTP negotiation time.
 *
 * device_cap_max_bitpool: device's reported Capability.SbcMaximumBitpool
 *   (status->sbc_max_bitpool_capability).  Used to clamp the written
 *   bitpool value down to the device's safe maximum unless
 *   profile->sbc_override_device_max is set.  Pass 0 to skip clamping.
 *
 * Requires the process to be running elevated; non-admin writes
 * will fail with ERROR_ACCESS_DENIED, which is logged with an
 * explicit hint.
 *
 * Returns 0 on success, -1 on any failure.
 */
int oa2dp_altdriver_write_next(const char *device_id,
                               const OA2DP_DeviceProfile *profile,
                               int device_cap_max_bitpool);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_ALTDRIVER_CONFIG_H */
