/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_types.h - Core enums and struct definitions
 */

#ifndef OA2DP_TYPES_H
#define OA2DP_TYPES_H

/* ── Windows-only guard ─────────────────────────────────────────────────
 *
 * OpenA2DP is built directly on Win32 Bluetooth APIs (BluetoothAPIs.h /
 * bthprops.lib), MMDevice / WASAPI, and Direct3D 11.  None of these are
 * available on Linux, macOS, BSD, or any other platform — there is no
 * portable shim layer and there is no plan to add one.
 *
 * If you are reading this on a non-Windows machine: this project will
 * not build for your OS, will not run under WINE in any meaningful
 * sense (the Bluetooth stack calls would fail), and is not a candidate
 * for a port.  Please do not file issues asking for Linux/macOS support.
 *
 * The Linux equivalent is BlueZ + PulseAudio/PipeWire which already
 * exposes everything OpenA2DP exposes (and far more) — use that
 * instead.
 */
#if !defined(_WIN32)
#  error "OpenA2DP is a Windows-only application. See include/oa2dp_types.h for details."
#endif

#include <stdint.h>
#include <time.h>

/* ── Codec type ─────────────────────────────────────────────────────── */

typedef enum OA2DP_CodecType {
    OA2DP_CODEC_UNKNOWN = 0,
    OA2DP_CODEC_SBC,
    OA2DP_CODEC_AAC,
    OA2DP_CODEC_COUNT
} OA2DP_CodecType;

/* ── SBC stereo mode ────────────────────────────────────────────────── */

typedef enum OA2DP_StereoMode {
    OA2DP_STEREO_JOINT = 0,
    OA2DP_STEREO_STEREO,
    OA2DP_STEREO_DUAL_CHANNEL,
    OA2DP_STEREO_COUNT
} OA2DP_StereoMode;

/* ── SBC allocation method ──────────────────────────────────────────── */

typedef enum OA2DP_AllocMethod {
    OA2DP_ALLOC_SNR = 0,
    OA2DP_ALLOC_LOUDNESS,
    OA2DP_ALLOC_COUNT
} OA2DP_AllocMethod;

/* ── SBC block size ─────────────────────────────────────────────────── */

typedef enum OA2DP_BlockSize {
    OA2DP_BLOCK_4 = 0,
    OA2DP_BLOCK_8,
    OA2DP_BLOCK_12,
    OA2DP_BLOCK_16,
    OA2DP_BLOCK_COUNT
} OA2DP_BlockSize;

/* ── SBC subbands ───────────────────────────────────────────────────── */

typedef enum OA2DP_Subbands {
    OA2DP_SUBBANDS_4 = 0,
    OA2DP_SUBBANDS_8,
    OA2DP_SUBBANDS_COUNT
} OA2DP_Subbands;

/* ── Connection state ───────────────────────────────────────────────── */

typedef enum OA2DP_ConnState {
    OA2DP_CONN_DISCONNECTED = 0,
    OA2DP_CONN_CONNECTING,
    OA2DP_CONN_CONNECTED,
    OA2DP_CONN_COUNT
} OA2DP_ConnState;

/* ── Log severity ───────────────────────────────────────────────────── */

typedef enum OA2DP_LogLevel {
    OA2DP_LOG_DEBUG = 0,
    OA2DP_LOG_INFO,
    OA2DP_LOG_WARN,
    OA2DP_LOG_ERROR,
    OA2DP_LOG_COUNT
} OA2DP_LogLevel;

/* ── Per-device profile (saved to config) ───────────────────────────── */

typedef struct OA2DP_DeviceProfile {
    char device_id[256];
    char display_name[128];
    OA2DP_CodecType preferred_codec;
    int allow_16khz;
    int allow_32khz;
    int allow_44_1khz;
    int allow_48khz;
    OA2DP_StereoMode stereo_mode;
    OA2DP_BlockSize block_size;
    OA2DP_AllocMethod allocation_method;
    OA2DP_Subbands subbands;
    int bitpool;
    int auto_heal_enabled;   /* if set, auto-reconnect when device connects but no audio endpoint appears */
    int hfp_watchdog_enabled; /* if set, periodically re-disable Handsfree to prevent it being turned back on */

    /* AAC parameters (used when preferred_codec is OA2DP_CODEC_AAC and
     * the device is on the Alternative A2DP Driver stack). */
    int aac_bitrate_kbps;    /* 0 = use device default sentinel */
    int aac_allow_stereo;    /* 1 = offer 2-channel in negotiation */
    int aac_allow_mono;      /* 1 = offer 1-channel in negotiation */
    int aac_allow_44_1khz;
    int aac_allow_48khz;

    /* Adaptive Bit Rate flag.  Applies to both SBC and AAC under
     * Alternative A2DP Driver. */
    int abr_enable;

    /* If 1, the SBC bitpool slider is allowed to exceed the device's
     * Capability.SbcMaximumBitpool ceiling.  Defaults to 0 (safe).
     * Toggling on requires Administrator elevation in the UI.
     * Setting bitpool above the device's reported maximum can
     * produce broken audio or damage some Bluetooth chips, hence
     * the safety guard. */
    int sbc_override_device_max;
} OA2DP_DeviceProfile;

/* ── Runtime device status (read-only, from system) ─────────────────── */

typedef struct OA2DP_DeviceStatus {
    char device_id[256];
    OA2DP_ConnState connection;
    OA2DP_CodecType active_codec;
    int sample_rate;
    int bit_depth;
    int channels;
    OA2DP_StereoMode stereo_mode;
    OA2DP_BlockSize block_size;
    OA2DP_AllocMethod allocation_method;
    OA2DP_Subbands subbands;
    int bitpool;
    int estimated_bitrate_kbps;

    /* WASAPI endpoint we matched (PKEY_Device_FriendlyName).  Empty
     * if no endpoint was found (i.e. status panel shows the
     * "connected but silent" warning). */
    char endpoint_name[256];

    /* Per-device service registration flags from
     * BluetoothEnumerateInstalledServices.  These reflect whether
     * the service is *installed* on the device record, not whether
     * it's currently the active route — Windows does not expose the
     * latter in user mode.  -1 means "not yet probed". */
    int audio_sink_installed;
    int handsfree_installed;

    /* Battery percentage from DEVPKEY_Bluetooth_Battery, populated
     * by the background probe.  -1 = not probed yet, -2 = device
     * doesn't expose a battery property to Windows. */
    int battery_pct;

    /* Live over-the-air codec bitrate in kbps from Alternative A2DP
     * Driver's Current.Bitrate registry value (or Capability ceiling
     * for AAC, since the driver doesn't populate AacBitrate in
     * Current).  0 = unknown / not on Alt A2DP Driver / device
     * silent.  Distinct from estimated_bitrate_kbps which is the
     * post-decode WASAPI mix-format rate. */
    int codec_bitrate_kbps;

    /* Device's reported maximum SBC bitpool from
     * Capability\<addr>\SbcMaximumBitpool.  This is the safe ceiling
     * — exceeding it can produce broken audio or damage cheaper
     * Bluetooth chips, so the settings panel uses this as the
     * slider's max unless the user explicitly enables override
     * (which requires admin).  0 = not yet probed / unknown. */
    int sbc_max_bitpool_capability;

    /* The full Capability\<addr> snapshot — what the device claims it
     * supports.  Populated by oa2dp_altdriver_read_current alongside
     * Current.  All fields are 0 until the probe runs (or always 0
     * if the device isn't in Alt A2DP Driver's database). */
    int cap_codecs;              /* Codec bitfield: bit 0=SBC, 1=AAC, ... */
    int cap_sbc_chmode;          /* bitfield, see header schema */
    int cap_sbc_freq;            /* bitfield */
    int cap_sbc_min_bitpool;     /* device's minimum bitpool */
    int cap_aac_chmode;          /* bitfield */
    int cap_aac_freq;            /* bitfield */
    int cap_aac_bitrate_kbps;    /* device's max sustainable AAC bitrate */
    int cap_aac_peak_bitrate_kbps; /* device's peak AAC bitrate */

    /* Negotiated audio latency from Current\<addr>\Delay, in 1/10 ms
     * units (so 2800 = 280 ms).  0 = not yet probed / unknown. */
    int latency_tenths_ms;

    /* Number of consecutive status refreshes where this device was
     * connected but oa2dp_audio_status_query failed to find an
     * endpoint.  Reset to 0 on success.  The status panel only
     * shows the "connected but silent" warning when this counter
     * passes a small threshold so transient races during codec
     * switches don't false-trigger the warning. */
    int endpoint_miss_count;

    /* Snapshot of the codec-relevant profile fields the LAST time we
     * read or wrote Alternative A2DP Driver's Devices\Next\<addr>
     * subkey.  Used by the settings panel to detect "unsaved
     * changes" — a profile field that differs from this snapshot
     * means the user has edited it but we haven't pushed the change
     * to the driver yet.
     *
     * alt_snapshot_valid is 1 once we've successfully read Next at
     * least once for this device.  Stays 0 on machines without
     * Alternative A2DP Driver, in which case the dirty check is
     * never meaningful (the codec settings are unenforceable on the
     * Microsoft stack anyway). */
    int alt_snapshot_valid;
    int snap_preferred_codec;
    int snap_allow_44_1khz, snap_allow_48khz, snap_allow_32khz, snap_allow_16khz;
    int snap_stereo_mode, snap_block_size, snap_allocation_method, snap_subbands;
    int snap_bitpool;
    int snap_aac_bitrate_kbps;
    int snap_aac_allow_stereo, snap_aac_allow_mono;
    int snap_aac_allow_44_1khz, snap_aac_allow_48khz;
    int snap_abr_enable;
} OA2DP_DeviceStatus;

/* ── Single log entry ───────────────────────────────────────────────── */

#define OA2DP_LOG_MSG_MAX 512

typedef struct OA2DP_LogEntry {
    time_t          timestamp;
    OA2DP_LogLevel  level;
    char            message[OA2DP_LOG_MSG_MAX];
} OA2DP_LogEntry;

/* ── Log ring buffer ────────────────────────────────────────────────── */

#define OA2DP_LOG_RING_SIZE 1024

typedef struct OA2DP_LogBuffer {
    OA2DP_LogEntry entries[OA2DP_LOG_RING_SIZE];
    int head;   /* next write position */
    int count;  /* entries currently stored (max OA2DP_LOG_RING_SIZE) */
} OA2DP_LogBuffer;

#endif /* OA2DP_TYPES_H */
