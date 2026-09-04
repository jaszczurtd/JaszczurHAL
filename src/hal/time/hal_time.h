#pragma once

#include "hal/core/hal_config.h"
#include "hal/core/hal_status.h"

#if defined(HAL_ENABLE_TIME) && defined(HAL_ENABLE_RTC)
#include "hal/rtc/hal_rtc.h"
#endif

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
 * @param year Gregorian year.
 * @param month Gregorian month in range 1..12.
 * @param day Actual day in the selected month.
 * @param hour Hour in range 0..23.
 * @param minute Minute in range 0..59.
 * @param second Second in range 0..59.
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
 * @param now Value to test.
 * @param start Inclusive interval start.
 * @param end Exclusive interval end.
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

/**
 * @brief Return rounded monotonic uptime in seconds.
 *
 * This preserves the established `(hal_millis() + 500) / 1000` behaviour.
 * @return Monotonic uptime rounded to the nearest second.
 */
unsigned long hal_get_seconds(void);

#ifdef HAL_ENABLE_TIME

/** @brief Origin of the currently active runtime wall clock. */
typedef enum {
  HAL_TIME_SOURCE_UNSET = 0,
  HAL_TIME_SOURCE_MANUAL,
  HAL_TIME_SOURCE_RTC,
  HAL_TIME_SOURCE_NTP,
} hal_time_source_t;

/** @brief State of the most recently requested NTP synchronization. */
typedef enum {
  HAL_TIME_NTP_IDLE = 0,
  HAL_TIME_NTP_IN_PROGRESS,
  HAL_TIME_NTP_SYNCHRONIZED,
  HAL_TIME_NTP_FAILED,
} hal_time_ntp_state_t;

/** @brief Atomic snapshot of runtime wall-clock and synchronization state. */
typedef struct {
  bool valid;                     /**< Runtime wall clock has been set. */
  hal_time_source_t source;       /**< Origin of the active wall clock. */
  uint64_t unix_time;             /**< Current Unix seconds when valid. */
  uint32_t micros;                /**< Fractional microseconds in 0..999999. */
  hal_time_ntp_state_t ntp_state; /**< Most recent NTP request state. */
  hal_status_t last_ntp_status;   /**< NTP result, EAGAIN, or HAL_NONE. */
  uint64_t last_ntp_sync_unix;    /**< Unix value accepted from NTP. */
  bool rtc_attached;              /**< An RTC handle is currently attached. */
  hal_status_t last_rtc_status;   /**< Last restore/persistence result. */
} hal_time_status_t;

/** Restore valid RTC time only while the runtime wall clock is unset. */
#define HAL_TIME_RTC_RESTORE_IF_VALID (UINT32_C(1) << 0u)
/** Write each successfully validated NTP result to the attached RTC. */
#define HAL_TIME_RTC_WRITE_AFTER_NTP (UINT32_C(1) << 1u)

/**
 * @brief Set the shared runtime wall clock.
 *
 * This is the single setter used by RTC bootstrap, NTP, and target libc
 * adapters. @p micros must be below one million and @p source must not be
 * HAL_TIME_SOURCE_UNSET.
 *
 * @param unix_time Seconds since the Unix epoch.
 * @param micros Fractional microseconds in range 0..999999.
 * @param source Origin assigned to the new wall-clock value.
 * @return HAL_OK, HAL_EINVAL for invalid input, or a backend error.
 */
hal_status_t hal_time_set_unix_ex(uint64_t unix_time, uint32_t micros,
                                  hal_time_source_t source);

/**
 * @brief Read one coherent wall-clock and synchronization snapshot.
 * @param out_status Receives the current service state.
 * @return HAL_OK, or HAL_EINVAL for a NULL output.
 */
hal_status_t hal_time_get_status_ex(hal_time_status_t *out_status);

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
 *         hal_time_format_local() calls. Use hal_time_get_status_ex() to
 *         distinguish a fresh NTP result from an older valid wall clock.
 */
bool hal_time_sync_ntp(const char *primary_server,
                       const char *secondary_server);

/**
 * @brief Start NTP synchronization and return a detailed status.
 * @param primary_server Primary NTP server hostname.
 * @param secondary_server Optional secondary hostname.
 * @return HAL_OK when accepted, or a validation/service error.
 */
hal_status_t hal_time_sync_ntp_ex(const char *primary_server,
                                  const char *secondary_server);

/**
 * @brief Return current Unix time in seconds.
 * @return Seconds since Unix epoch (64-bit, Y2038-safe).
 */
uint64_t hal_time_unix(void);

/**
 * @brief Check if current Unix time exceeds a minimum threshold.
 * @param min_unix Minimum acceptable Unix timestamp.
 * @return true if the runtime clock is valid and current time is >= min_unix.
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

#ifdef HAL_ENABLE_RTC
/**
 * @brief Attach one RTC to the shared wall-clock service.
 *
 * The caller retains ownership of @p rtc and must keep it initialized until
 * hal_time_detach_rtc_ex() has completed. Attach/detach and RTC deinit are
 * setup operations and must not run concurrently with each other.
 *
 * When HAL_TIME_RTC_RESTORE_IF_VALID is selected, a valid RTC seeds an unset
 * wall clock. An invalid RTC is accepted and reported as HAL_EAGAIN through
 * hal_time_status_t::last_rtc_status so a later NTP result can initialize it.
 * Once attachment succeeds, restore failures are also reported through that
 * field while this function returns HAL_OK and keeps the RTC available for a
 * later NTP write.
 *
 * @param rtc Initialized RTC handle retained by the caller.
 * @param policy_flags Bitwise combination of HAL_TIME_RTC_* policies.
 * @return HAL_OK, or a validation, state, locking, or RTC error.
 */
hal_status_t hal_time_attach_rtc_ex(hal_rtc_t rtc, uint32_t policy_flags);

/**
 * @brief Detach the current RTC without deinitializing its handle.
 * @return HAL_OK, HAL_EUNINIT when none is attached, or a locking error.
 */
hal_status_t hal_time_detach_rtc_ex(void);
#endif

#endif /* HAL_ENABLE_TIME */
#ifdef __cplusplus
}
#endif
