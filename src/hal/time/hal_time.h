#pragma once

#include "hal/core/hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file hal_time.h
 * @brief Time helpers: date/time conversion plus optional system/NTP APIs.
 *
 * The optional NTP state is protected by short mutex-held snapshots. DNS,
 * socket, UDP service, receive, send, close, and runtime-clock calls execute
 * after releasing the state mutex, so network callbacks may safely re-enter
 * time getters. Calls from concurrent tasks/cores are supported.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/**
 * @brief Convert local date/time components to Unix epoch seconds.
 *
 * This helper is available even when HAL_ENABLE_TIME is not defined.
 *
 * Dates after the last value representable by uint32_t are rejected.
 *
 * @return Unix epoch seconds, or 0 when components are invalid or overflow the
 * 32-bit result. Unix epoch 0 is also a valid result for 1970-01-01 00:00:00.
 */
uint32_t hal_time_from_components(int year, int month, int day, int hour,
                                  int minute, int second);

/**
 * @brief Check the Central European date-based daylight-saving interval.
 *
 * The interval starts on the last Sunday of March (inclusive) and ends on the
 * last Sunday of October (exclusive). Because the API has no time-of-day
 * argument, the transition applies to each boundary date from 00:00.
 *
 * @param year Gregorian year in range 1..65535.
 * @param month Gregorian month in range 1..12.
 * @param day Actual day in the selected month.
 * @return true for a valid date inside the interval; false outside it or when
 * the date is invalid.
 */
bool hal_time_is_daylight_saving_time(int year, int month, int day);

/**
 * @brief Apply the CET/CEST offset to date and time components in place.
 *
 * Adds one hour outside the date-based daylight-saving interval and two hours
 * inside it, normalizing day, month, and year rollover. The minute value is
 * preserved. Invalid input, NULL pointers, and results beyond year 65535 leave
 * all values unchanged.
 *
 * @param year In/out Gregorian year.
 * @param month In/out Gregorian month.
 * @param day In/out actual day in the selected month.
 * @param hour In/out hour in range 0..23.
 * @param minute In/out minute in range 0..59.
 */
void hal_time_adjust_cet_cest(int *year, int *month, int *day, int *hour,
                              int *minute);

/**
 * @brief Check whether a value belongs to a half-open interval.
 * @return true exactly when @p now is in [@p start, @p end).
 */
bool hal_time_is_in_range(long now, long start, long end);

/**
 * @brief Split a minute count into quotient hours and remainder minutes.
 *
 * C signed division semantics are preserved for negative values. Each
 * non-NULL output is written independently.
 *
 * @param time_in_minutes Minute count to split.
 * @param hours Optional output for time_in_minutes / 60.
 * @param minutes Optional output for time_in_minutes % 60.
 */
void hal_time_extract_minutes(long time_in_minutes, int *hours, int *minutes);

#ifdef HAL_ENABLE_TIME

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
 * @return true when request was accepted. Progress is serviced by subsequent
 *         hal_time_unix(), hal_time_is_synced(), hal_time_get_local(), or
 *         hal_time_format_local() calls.
 */
bool hal_time_sync_ntp(const char *primary_server,
                       const char *secondary_server);

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

#endif /* HAL_ENABLE_TIME */
#ifdef __cplusplus
}
#endif
