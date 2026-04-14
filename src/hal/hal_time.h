#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file hal_time.h
 * @brief Time helpers: date/time conversion plus optional system/NTP APIs.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/**
 * @brief Convert local date/time components to Unix epoch seconds.
 *
 * This helper is available even when HAL_DISABLE_TIME is defined.
 *
 * @return Unix epoch seconds, or 0 when components are outside a valid range.
 */
uint32_t hal_time_from_components(int year, int month, int day,
                                  int hour, int minute, int second);

#ifndef HAL_DISABLE_TIME

/**
 * @brief Configure POSIX timezone string (TZ environment variable).
 * @param tz Null-terminated TZ string.
 * @return true on success.
 */
bool hal_time_set_timezone(const char *tz);

/**
 * @brief Start NTP synchronization.
 * @param primary_server Primary NTP server hostname.
 * @param secondary_server Optional secondary NTP server hostname (can be NULL).
 * @return true when request was accepted.
 */
bool hal_time_sync_ntp(const char *primary_server, const char *secondary_server);

/**
 * @brief Return current Unix time in seconds.
 * @return Seconds since Unix epoch (64-bit, Y2038-safe).
 */
uint64_t hal_time_unix(void);

/**
 * @brief Check if current Unix time exceeds a minimum threshold.
 * @param min_unix Minimum acceptable Unix timestamp.
 * @return true if current time is >= min_unix.
 */
bool hal_time_is_synced(uint64_t min_unix);

/**
 * @brief Read local time breakdown.
 * @param out_tm Destination struct.
 * @return true on success.
 */
bool hal_time_get_local(struct tm *out_tm);

/**
 * @brief Format local time into caller buffer.
 * @param out Destination buffer.
 * @param out_size Destination size.
 * @param format strftime-compatible format string.
 * @return true on success.
 */
bool hal_time_format_local(char *out, size_t out_size, const char *format);


#endif /* HAL_DISABLE_TIME */
#ifdef __cplusplus
}
#endif
