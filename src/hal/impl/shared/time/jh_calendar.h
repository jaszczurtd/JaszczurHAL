#pragma once

#include "hal/hal_status.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JH_CALENDAR_MIN_YEAR 1u
#define JH_CALENDAR_UNIX_EPOCH_YEAR 1970u
#define JH_CALENDAR_MAX_YEAR UINT16_MAX

typedef struct {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t weekday;
} jh_calendar_datetime_t;

hal_status_t jh_calendar_is_leap_year(uint16_t year, bool *out_is_leap);
hal_status_t jh_calendar_days_in_month(uint16_t year, uint8_t month,
                                       uint8_t *out_days);
hal_status_t jh_calendar_day_of_week(uint16_t year, uint8_t month, uint8_t day,
                                     uint8_t *out_weekday);
hal_status_t
jh_calendar_validate_datetime(const jh_calendar_datetime_t *datetime,
                              uint16_t min_year, uint16_t max_year);
hal_status_t
jh_calendar_datetime_to_epoch(const jh_calendar_datetime_t *datetime,
                              uint64_t *out_epoch);
hal_status_t
jh_calendar_epoch_to_datetime(uint64_t epoch, uint16_t max_year,
                              jh_calendar_datetime_t *out_datetime);

#ifdef __cplusplus
}
#endif
