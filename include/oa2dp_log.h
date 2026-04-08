/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_log.h - Logging ring buffer API
 */

#ifndef OA2DP_LOG_H
#define OA2DP_LOG_H

#include "oa2dp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize / reset the global log buffer. */
void oa2dp_log_init(void);

/* Write a message at the given severity level. */
void oa2dp_log(OA2DP_LogLevel level, const char *fmt, ...);

/* Convenience macros. */
#define OA2DP_LOG_DEBUG(fmt, ...) oa2dp_log(OA2DP_LOG_DEBUG, (fmt), ##__VA_ARGS__)
#define OA2DP_LOG_INFO(fmt, ...)  oa2dp_log(OA2DP_LOG_INFO,  (fmt), ##__VA_ARGS__)
#define OA2DP_LOG_WARN(fmt, ...)  oa2dp_log(OA2DP_LOG_WARN,  (fmt), ##__VA_ARGS__)
#define OA2DP_LOG_ERROR(fmt, ...) oa2dp_log(OA2DP_LOG_ERROR, (fmt), ##__VA_ARGS__)

/* Clear all entries from the log buffer. */
void oa2dp_log_clear(void);

/* Return a pointer to the global log buffer (read-only for UI). */
const OA2DP_LogBuffer *oa2dp_log_get_buffer(void);

/* Return the human-readable label for a log level. */
const char *oa2dp_log_level_str(OA2DP_LogLevel level);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_LOG_H */
