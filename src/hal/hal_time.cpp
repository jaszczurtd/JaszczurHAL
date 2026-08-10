#include "hal_time.h"

#include "impl/shared/time/jh_calendar.h"

uint32_t hal_time_from_components(int year, int month, int day, int hour,
                                  int minute, int second) {
  if (year < (int)JH_CALENDAR_UNIX_EPOCH_YEAR ||
      year > (int)JH_CALENDAR_MAX_YEAR || month < 1 || month > 12 || day < 1 ||
      day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59) {
    return 0u;
  }

  const jh_calendar_datetime_t datetime = {
      (uint16_t)year,  (uint8_t)month,  (uint8_t)day, (uint8_t)hour,
      (uint8_t)minute, (uint8_t)second, 0u,
  };
  uint64_t epoch = 0u;
  if (jh_calendar_datetime_to_epoch(&datetime, &epoch) != HAL_OK ||
      epoch > UINT32_MAX) {
    return 0u;
  }
  return (uint32_t)epoch;
}

static bool hal_time_make_datetime(int year, int month, int day, int hour,
                                   int minute,
                                   jh_calendar_datetime_t *out_datetime) {
  if (out_datetime == nullptr || year < (int)JH_CALENDAR_MIN_YEAR ||
      year > (int)JH_CALENDAR_MAX_YEAR || month < 1 || month > 12 || day < 1 ||
      day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    return false;
  }

  const jh_calendar_datetime_t datetime = {
      (uint16_t)year,
      (uint8_t)month,
      (uint8_t)day,
      (uint8_t)hour,
      (uint8_t)minute,
      0u,
      0u,
  };
  if (jh_calendar_validate_datetime(&datetime, JH_CALENDAR_MIN_YEAR,
                                    JH_CALENDAR_MAX_YEAR) != HAL_OK) {
    return false;
  }

  *out_datetime = datetime;
  return true;
}

bool hal_time_is_daylight_saving_time(int year, int month, int day) {
  jh_calendar_datetime_t datetime = {};
  if (!hal_time_make_datetime(year, month, day, 0, 0, &datetime)) {
    return false;
  }
  if (month < 3 || month > 10) {
    return false;
  }
  if (month > 3 && month < 10) {
    return true;
  }

  uint8_t days_in_month = 0u;
  if (jh_calendar_days_in_month(datetime.year, datetime.month,
                                &days_in_month) != HAL_OK) {
    return false;
  }
  uint8_t last_day_weekday = 0u;
  if (jh_calendar_day_of_week(datetime.year, datetime.month, days_in_month,
                              &last_day_weekday) != HAL_OK) {
    return false;
  }
  const int last_sunday = (int)days_in_month - (int)last_day_weekday;
  return month == 3 ? day >= last_sunday : day < last_sunday;
}

void hal_time_adjust_cet_cest(int *year, int *month, int *day, int *hour,
                              int *minute) {
  if (year == nullptr || month == nullptr || day == nullptr ||
      hour == nullptr || minute == nullptr) {
    return;
  }

  jh_calendar_datetime_t datetime = {};
  if (!hal_time_make_datetime(*year, *month, *day, *hour, *minute, &datetime)) {
    return;
  }

  const int offset =
      hal_time_is_daylight_saving_time(*year, *month, *day) ? 2 : 1;
  int adjusted_hour = *hour + offset;
  int adjusted_day = *day;
  int adjusted_month = *month;
  int adjusted_year = *year;

  if (adjusted_hour >= 24) {
    adjusted_hour -= 24;
    ++adjusted_day;

    uint8_t days_in_month = 0u;
    if (jh_calendar_days_in_month(datetime.year, datetime.month,
                                  &days_in_month) != HAL_OK) {
      return;
    }
    if (adjusted_day > (int)days_in_month) {
      adjusted_day = 1;
      ++adjusted_month;
      if (adjusted_month > 12) {
        adjusted_month = 1;
        if (adjusted_year == (int)JH_CALENDAR_MAX_YEAR) {
          return;
        }
        ++adjusted_year;
      }
    }
  }

  *year = adjusted_year;
  *month = adjusted_month;
  *day = adjusted_day;
  *hour = adjusted_hour;
}

bool hal_time_is_in_range(long now, long start, long end) {
  return now >= start && now < end;
}

void hal_time_extract_minutes(long time_in_minutes, int *hours, int *minutes) {
  if (hours != nullptr) {
    *hours = (int)(time_in_minutes / 60);
  }
  if (minutes != nullptr) {
    *minutes = (int)(time_in_minutes % 60);
  }
}
