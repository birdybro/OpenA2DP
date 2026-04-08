/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * config.c - INI-style profile save / load
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>

#include "oa2dp_config.h"
#include "oa2dp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── config directory ───────────────────────────────────────────────── */

static char g_config_dir[MAX_PATH] = {0};

int oa2dp_config_init(void)
{
    char appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
        oa2dp_log(OA2DP_LOG_ERROR, "config: failed to get APPDATA path");
        return -1;
    }

    snprintf(g_config_dir, sizeof(g_config_dir), "%s\\OpenA2DP", appdata);

    if (!CreateDirectoryA(g_config_dir, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            oa2dp_log(OA2DP_LOG_ERROR, "config: failed to create '%s' (err=%lu)",
                      g_config_dir, err);
            return -1;
        }
    }

    oa2dp_log(OA2DP_LOG_INFO, "config: directory '%s'", g_config_dir);
    return 0;
}

int oa2dp_config_path_for_device(const char *device_id,
                                 char *buf, int buf_size)
{
    if (!g_config_dir[0] || !device_id || !buf)
        return -1;

    /* Replace colons with underscores for a safe filename. */
    char safe_id[256];
    snprintf(safe_id, sizeof(safe_id), "%s", device_id);
    for (char *p = safe_id; *p; p++) {
        if (*p == ':') *p = '_';
    }

    snprintf(buf, buf_size, "%s\\%s.ini", g_config_dir, safe_id);
    return 0;
}

/* ── helpers ────────────────────────────────────────────────────────── */

static const char *codec_to_str(OA2DP_CodecType c)
{
    switch (c) {
    case OA2DP_CODEC_SBC:     return "SBC";
    case OA2DP_CODEC_AAC:     return "AAC";
    default:                  return "unknown";
    }
}

static OA2DP_CodecType str_to_codec(const char *s)
{
    if (_stricmp(s, "SBC") == 0) return OA2DP_CODEC_SBC;
    if (_stricmp(s, "AAC") == 0) return OA2DP_CODEC_AAC;
    return OA2DP_CODEC_UNKNOWN;
}

static const char *stereo_mode_to_str(OA2DP_StereoMode m)
{
    switch (m) {
    case OA2DP_STEREO_JOINT:        return "joint_stereo";
    case OA2DP_STEREO_STEREO:       return "stereo";
    case OA2DP_STEREO_DUAL_CHANNEL: return "dual_channel";
    default:                        return "joint_stereo";
    }
}

static OA2DP_StereoMode str_to_stereo_mode(const char *s)
{
    if (_stricmp(s, "stereo") == 0)       return OA2DP_STEREO_STEREO;
    if (_stricmp(s, "dual_channel") == 0) return OA2DP_STEREO_DUAL_CHANNEL;
    return OA2DP_STEREO_JOINT;
}

static const char *alloc_to_str(OA2DP_AllocMethod a)
{
    switch (a) {
    case OA2DP_ALLOC_SNR:      return "SNR";
    case OA2DP_ALLOC_LOUDNESS: return "loudness";
    default:                   return "loudness";
    }
}

static OA2DP_AllocMethod str_to_alloc(const char *s)
{
    if (_stricmp(s, "SNR") == 0) return OA2DP_ALLOC_SNR;
    return OA2DP_ALLOC_LOUDNESS;
}

static int block_size_to_int(OA2DP_BlockSize b)
{
    switch (b) {
    case OA2DP_BLOCK_4:  return 4;
    case OA2DP_BLOCK_8:  return 8;
    case OA2DP_BLOCK_12: return 12;
    case OA2DP_BLOCK_16: return 16;
    default:             return 16;
    }
}

static OA2DP_BlockSize int_to_block_size(int v)
{
    switch (v) {
    case 4:  return OA2DP_BLOCK_4;
    case 8:  return OA2DP_BLOCK_8;
    case 12: return OA2DP_BLOCK_12;
    default: return OA2DP_BLOCK_16;
    }
}

static int subbands_to_int(OA2DP_Subbands s)
{
    return (s == OA2DP_SUBBANDS_4) ? 4 : 8;
}

static OA2DP_Subbands int_to_subbands(int v)
{
    return (v == 4) ? OA2DP_SUBBANDS_4 : OA2DP_SUBBANDS_8;
}

/* Trim leading/trailing whitespace in place, return pointer into buf. */
static char *trim(char *buf)
{
    while (*buf && isspace((unsigned char)*buf)) buf++;
    char *end = buf + strlen(buf);
    while (end > buf && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return buf;
}

/* ── save ───────────────────────────────────────────────────────────── */

int oa2dp_profile_save(const char *path, const OA2DP_DeviceProfile *p)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        oa2dp_log(OA2DP_LOG_ERROR, "config: failed to open '%s' for writing", path);
        return -1;
    }

    fprintf(f, "[device]\n");
    fprintf(f, "device_id = %s\n",           p->device_id);
    fprintf(f, "display_name = %s\n",        p->display_name);

    fprintf(f, "\n[codec]\n");
    fprintf(f, "preferred_codec = %s\n",     codec_to_str(p->preferred_codec));

    fprintf(f, "\n[channels]\n");
    fprintf(f, "allow_mono = %d\n",          p->allow_mono);
    fprintf(f, "allow_stereo = %d\n",        p->allow_stereo);

    fprintf(f, "\n[sample_rates]\n");
    fprintf(f, "allow_16khz = %d\n",         p->allow_16khz);
    fprintf(f, "allow_32khz = %d\n",         p->allow_32khz);
    fprintf(f, "allow_44_1khz = %d\n",       p->allow_44_1khz);
    fprintf(f, "allow_48khz = %d\n",         p->allow_48khz);

    fprintf(f, "\n[sbc]\n");
    fprintf(f, "stereo_mode = %s\n",         stereo_mode_to_str(p->stereo_mode));
    fprintf(f, "block_size = %d\n",          block_size_to_int(p->block_size));
    fprintf(f, "allocation_method = %s\n",   alloc_to_str(p->allocation_method));
    fprintf(f, "subbands = %d\n",            subbands_to_int(p->subbands));
    fprintf(f, "override_bitpool = %d\n",    p->override_bitpool);
    fprintf(f, "bitpool = %d\n",             p->bitpool);
    fprintf(f, "auto_reduce_bitpool = %d\n", p->auto_reduce_bitpool);

    fclose(f);
    oa2dp_log(OA2DP_LOG_INFO, "config: saved profile for '%s' to '%s'",
              p->display_name, path);
    return 0;
}

/* ── load ───────────────────────────────────────────────────────────── */

int oa2dp_profile_load(const char *path, OA2DP_DeviceProfile *p)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        oa2dp_log(OA2DP_LOG_ERROR, "config: failed to open '%s' for reading", path);
        return -1;
    }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);

        /* Skip blanks, comments, and section headers. */
        if (*s == '\0' || *s == '#' || *s == ';' || *s == '[')
            continue;

        /* Split on '='. */
        char *eq = strchr(s, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        /* Match known keys; silently ignore unknown ones. */
        if      (strcmp(key, "device_id") == 0)
            snprintf(p->device_id, sizeof(p->device_id), "%s", val);
        else if (strcmp(key, "display_name") == 0)
            snprintf(p->display_name, sizeof(p->display_name), "%s", val);
        else if (strcmp(key, "preferred_codec") == 0)
            p->preferred_codec = str_to_codec(val);
        else if (strcmp(key, "allow_mono") == 0)
            p->allow_mono = atoi(val);
        else if (strcmp(key, "allow_stereo") == 0)
            p->allow_stereo = atoi(val);
        else if (strcmp(key, "allow_16khz") == 0)
            p->allow_16khz = atoi(val);
        else if (strcmp(key, "allow_32khz") == 0)
            p->allow_32khz = atoi(val);
        else if (strcmp(key, "allow_44_1khz") == 0)
            p->allow_44_1khz = atoi(val);
        else if (strcmp(key, "allow_48khz") == 0)
            p->allow_48khz = atoi(val);
        else if (strcmp(key, "stereo_mode") == 0)
            p->stereo_mode = str_to_stereo_mode(val);
        else if (strcmp(key, "block_size") == 0)
            p->block_size = int_to_block_size(atoi(val));
        else if (strcmp(key, "allocation_method") == 0)
            p->allocation_method = str_to_alloc(val);
        else if (strcmp(key, "subbands") == 0)
            p->subbands = int_to_subbands(atoi(val));
        else if (strcmp(key, "override_bitpool") == 0)
            p->override_bitpool = atoi(val);
        else if (strcmp(key, "bitpool") == 0)
            p->bitpool = atoi(val);
        else if (strcmp(key, "auto_reduce_bitpool") == 0)
            p->auto_reduce_bitpool = atoi(val);
    }

    fclose(f);

    /* Clamp anything that was out of range. */
    oa2dp_profile_validate(p);

    oa2dp_log(OA2DP_LOG_INFO, "config: loaded profile for '%s' from '%s'",
              p->display_name, path);
    return 0;
}
