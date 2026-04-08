/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * test_core.c - Tests for core structs, config, validation, and logging
 *
 * Build:
 *   cl /I..\include /Fe:test_core.exe test_core.c ..\core\config.c
 *      ..\core\validation.c ..\core\log.c
 */

#include "oa2dp_types.h"
#include "oa2dp_config.h"
#include "oa2dp_log.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TEST(name) static void name(void)
#define RUN(name)  do { printf("  %-40s", #name); name(); printf("OK\n"); } while(0)

static const char *TEST_FILE = "test_profile.ini";

/* ── defaults ───────────────────────────────────────────────────────── */

TEST(test_defaults)
{
    OA2DP_DeviceProfile p;
    oa2dp_profile_defaults(&p);

    assert(p.preferred_codec == OA2DP_CODEC_SBC);
    assert(p.allow_stereo == 1);
    assert(p.allow_mono == 1);
    assert(p.allow_44_1khz == 1);
    assert(p.allow_48khz == 1);
    assert(p.allow_16khz == 0);
    assert(p.allow_32khz == 0);
    assert(p.stereo_mode == OA2DP_STEREO_JOINT);
    assert(p.block_size == OA2DP_BLOCK_16);
    assert(p.allocation_method == OA2DP_ALLOC_LOUDNESS);
    assert(p.subbands == OA2DP_SUBBANDS_8);
    assert(p.bitpool == 53);
    assert(p.override_bitpool == 0);
    assert(p.auto_reduce_bitpool == 1);
}

/* ── validation / clamping ──────────────────────────────────────────── */

TEST(test_validate_clamps_bitpool)
{
    OA2DP_DeviceProfile p;
    oa2dp_profile_defaults(&p);

    p.bitpool = 999;
    oa2dp_profile_validate(&p);
    assert(p.bitpool == 250);

    p.bitpool = -5;
    oa2dp_profile_validate(&p);
    assert(p.bitpool == 2);
}

TEST(test_validate_clamps_enum)
{
    OA2DP_DeviceProfile p;
    oa2dp_profile_defaults(&p);

    p.preferred_codec = (OA2DP_CodecType)99;
    oa2dp_profile_validate(&p);
    assert(p.preferred_codec >= 0 && p.preferred_codec < OA2DP_CODEC_COUNT);
}

TEST(test_validate_forces_channel)
{
    OA2DP_DeviceProfile p;
    oa2dp_profile_defaults(&p);

    p.allow_mono = 0;
    p.allow_stereo = 0;
    oa2dp_profile_validate(&p);
    assert(p.allow_stereo == 1);
}

TEST(test_validate_forces_sample_rate)
{
    OA2DP_DeviceProfile p;
    oa2dp_profile_defaults(&p);

    p.allow_16khz = 0;
    p.allow_32khz = 0;
    p.allow_44_1khz = 0;
    p.allow_48khz = 0;
    oa2dp_profile_validate(&p);
    assert(p.allow_44_1khz == 1);
}

/* ── round-trip save / load ─────────────────────────────────────────── */

TEST(test_save_load_roundtrip)
{
    OA2DP_DeviceProfile orig;
    oa2dp_profile_defaults(&orig);
    snprintf(orig.device_id, sizeof(orig.device_id), "AA:BB:CC:DD:EE:FF");
    snprintf(orig.display_name, sizeof(orig.display_name), "Test Headphones");
    orig.preferred_codec = OA2DP_CODEC_AAC;
    orig.stereo_mode = OA2DP_STEREO_DUAL_CHANNEL;
    orig.block_size = OA2DP_BLOCK_8;
    orig.allocation_method = OA2DP_ALLOC_SNR;
    orig.subbands = OA2DP_SUBBANDS_4;
    orig.bitpool = 40;
    orig.override_bitpool = 1;

    assert(oa2dp_profile_save(TEST_FILE, &orig) == 0);

    OA2DP_DeviceProfile loaded;
    oa2dp_profile_defaults(&loaded);
    assert(oa2dp_profile_load(TEST_FILE, &loaded) == 0);

    assert(strcmp(loaded.device_id, orig.device_id) == 0);
    assert(strcmp(loaded.display_name, orig.display_name) == 0);
    assert(loaded.preferred_codec == orig.preferred_codec);
    assert(loaded.allow_mono == orig.allow_mono);
    assert(loaded.allow_stereo == orig.allow_stereo);
    assert(loaded.allow_44_1khz == orig.allow_44_1khz);
    assert(loaded.stereo_mode == orig.stereo_mode);
    assert(loaded.block_size == orig.block_size);
    assert(loaded.allocation_method == orig.allocation_method);
    assert(loaded.subbands == orig.subbands);
    assert(loaded.bitpool == orig.bitpool);
    assert(loaded.override_bitpool == orig.override_bitpool);
    assert(loaded.auto_reduce_bitpool == orig.auto_reduce_bitpool);

    remove(TEST_FILE);
}

/* ── unknown fields don't crash ─────────────────────────────────────── */

TEST(test_load_unknown_fields)
{
    FILE *f = fopen(TEST_FILE, "w");
    assert(f);
    fprintf(f, "[device]\n");
    fprintf(f, "device_id = XX:YY\n");
    fprintf(f, "some_future_key = whatever\n");
    fprintf(f, "bitpool = 35\n");
    fclose(f);

    OA2DP_DeviceProfile p;
    oa2dp_profile_defaults(&p);
    assert(oa2dp_profile_load(TEST_FILE, &p) == 0);
    assert(strcmp(p.device_id, "XX:YY") == 0);
    assert(p.bitpool == 35);

    remove(TEST_FILE);
}

/* ── log ring buffer ────────────────────────────────────────────────── */

TEST(test_log_basic)
{
    oa2dp_log_init();
    oa2dp_log(OA2DP_LOG_INFO, "hello %d", 42);

    const OA2DP_LogBuffer *buf = oa2dp_log_get_buffer();
    assert(buf->count == 1);
    assert(buf->entries[0].level == OA2DP_LOG_INFO);
    assert(strstr(buf->entries[0].message, "42") != NULL);
}

TEST(test_log_wrap)
{
    oa2dp_log_init();
    for (int i = 0; i < OA2DP_LOG_RING_SIZE + 10; i++)
        oa2dp_log(OA2DP_LOG_DEBUG, "msg %d", i);

    const OA2DP_LogBuffer *buf = oa2dp_log_get_buffer();
    assert(buf->count == OA2DP_LOG_RING_SIZE);
}

TEST(test_log_level_str)
{
    assert(strcmp(oa2dp_log_level_str(OA2DP_LOG_DEBUG), "DEBUG") == 0);
    assert(strcmp(oa2dp_log_level_str(OA2DP_LOG_INFO),  "INFO")  == 0);
    assert(strcmp(oa2dp_log_level_str(OA2DP_LOG_WARN),  "WARN")  == 0);
    assert(strcmp(oa2dp_log_level_str(OA2DP_LOG_ERROR), "ERROR") == 0);
    assert(strcmp(oa2dp_log_level_str((OA2DP_LogLevel)99), "???") == 0);
}

/* ── main ───────────────────────────────────────────────────────────── */

int main(void)
{
    printf("core tests:\n");

    oa2dp_log_init();

    RUN(test_defaults);
    RUN(test_validate_clamps_bitpool);
    RUN(test_validate_clamps_enum);
    RUN(test_validate_forces_channel);
    RUN(test_validate_forces_sample_rate);
    RUN(test_save_load_roundtrip);
    RUN(test_load_unknown_fields);
    RUN(test_log_basic);
    RUN(test_log_wrap);
    RUN(test_log_level_str);

    printf("\nall tests passed.\n");
    return 0;
}
