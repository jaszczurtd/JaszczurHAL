#include "jh_calendar.h"

#include <stddef.h>

#define JH_SECONDS_PER_DAY UINT64_C(86400)

static const uint16_t s_days_before_month[] = {
    0u, 31u, 59u, 90u, 120u, 151u, 181u, 212u, 243u, 273u, 304u, 334u,
};

static bool calendar_is_leap_year(uint16_t year) {
  return ((year % 4u) == 0u && (year % 100u) != 0u) || ((year % 400u) == 0u);
}

static uint64_t calendar_days_before_year(uint16_t year) {
  const uint64_t previous_year = (uint64_t)year - 1u;
  return previous_year * 365u + previous_year / 4u - previous_year / 100u +
         previous_year / 400u;
}

hal_status_t jh_calendar_is_leap_year(uint16_t year, bool *out_is_leap) {
  if (year < JH_CALENDAR_MIN_YEAR || out_is_leap == NULL) {
    return HAL_EINVAL;
  }

  *out_is_leap = calendar_is_leap_year(year);
  return HAL_OK;
}

hal_status_t jh_calendar_days_in_month(uint16_t year, uint8_t month,
                                       uint8_t *out_days) {
  if (year < JH_CALENDAR_MIN_YEAR || month < 1u || month > 12u ||
      out_days == NULL) {
    return HAL_EINVAL;
  }

  static const uint8_t days_in_month[] = {
      31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u,
  };
  *out_days = days_in_month[month - 1u];
  if (month == 2u && calendar_is_leap_year(year)) {
    ++(*out_days);
  }
  return HAL_OK;
}

hal_status_t jh_calendar_day_of_week(uint16_t year, uint8_t month, uint8_t day,
                                     uint8_t *out_weekday) {
  if (out_weekday == NULL) {
    return HAL_EINVAL;
  }

  const jh_calendar_datetime_t datetime = {
      year, month, day, 0u, 0u, 0u, 0u,
  };
  if (jh_calendar_validate_datetime(&datetime, JH_CALENDAR_MIN_YEAR,
                                    JH_CALENDAR_MAX_YEAR) != HAL_OK) {
    return HAL_EINVAL;
  }

  uint64_t days = calendar_days_before_year(year);
  days += s_days_before_month[month - 1u];
  if (month > 2u && calendar_is_leap_year(year)) {
    ++days;
  }
  days += (uint64_t)(day - 1u);

  /* Proleptic Gregorian 0001-01-01 was a Monday (Sunday == 0). */
  *out_weekday = (uint8_t)((days + 1u) % 7u);
  return HAL_OK;
}

hal_status_t
jh_calendar_validate_datetime(const jh_calendar_datetime_t *datetime,
                              uint16_t min_year, uint16_t max_year) {
  if (datetime == NULL || min_year < JH_CALENDAR_MIN_YEAR ||
      max_year < min_year) {
    return HAL_EINVAL;
  }
  if (datetime->year < min_year || datetime->year > max_year ||
      datetime->month < 1u || datetime->month > 12u || datetime->hour > 23u ||
      datetime->minute > 59u || datetime->second > 59u ||
      datetime->weekday > 6u) {
    return HAL_EINVAL;
  }

  uint8_t days_in_month = 0u;
  if (jh_calendar_days_in_month(datetime->year, datetime->month,
                                &days_in_month) != HAL_OK ||
      datetime->day < 1u || datetime->day > days_in_month) {
    return HAL_EINVAL;
  }
  return HAL_OK;
}

hal_status_t
jh_calendar_datetime_to_epoch(const jh_calendar_datetime_t *datetime,
                              uint64_t *out_epoch) {
  if (datetime == NULL || out_epoch == NULL) {
    return HAL_EINVAL;
  }
  if (jh_calendar_validate_datetime(datetime, JH_CALENDAR_MIN_YEAR,
                                    JH_CALENDAR_MAX_YEAR) != HAL_OK) {
    return HAL_EINVAL;
  }
  if (datetime->year < JH_CALENDAR_UNIX_EPOCH_YEAR) {
    return HAL_EOVERFLOW;
  }

  uint64_t days = calendar_days_before_year(datetime->year) -
                  calendar_days_before_year(JH_CALENDAR_UNIX_EPOCH_YEAR);
  days += s_days_before_month[datetime->month - 1u];
  if (datetime->month > 2u && calendar_is_leap_year(datetime->year)) {
    ++days;
  }
  days += (uint64_t)(datetime->day - 1u);

  *out_epoch = days * JH_SECONDS_PER_DAY + (uint64_t)datetime->hour * 3600u +
               (uint64_t)datetime->minute * 60u + datetime->second;
  return HAL_OK;
}

hal_status_t
jh_calendar_epoch_to_datetime(uint64_t epoch, uint16_t max_year,
                              jh_calendar_datetime_t *out_datetime) {
  if (out_datetime == NULL || max_year < JH_CALENDAR_UNIX_EPOCH_YEAR) {
    return HAL_EINVAL;
  }

  const jh_calendar_datetime_t maximum = {
      max_year, 12u, 31u, 23u, 59u, 59u, 0u,
  };
  uint64_t maximum_epoch = 0u;
  if (jh_calendar_datetime_to_epoch(&maximum, &maximum_epoch) != HAL_OK) {
    return HAL_EINVAL;
  }
  if (epoch > maximum_epoch) {
    return HAL_EOVERFLOW;
  }

  const uint64_t epoch_days = epoch / JH_SECONDS_PER_DAY;
  uint64_t remaining_days = epoch_days;
  uint32_t year = JH_CALENDAR_UNIX_EPOCH_YEAR;
  while (year <= max_year) {
    const uint16_t current_year = (uint16_t)year;
    const uint16_t days_in_year =
        calendar_is_leap_year(current_year) ? 366u : 365u;
    if (remaining_days < days_in_year) {
      break;
    }
    remaining_days -= days_in_year;
    ++year;
  }
  if (year > max_year) {
    return HAL_EOVERFLOW;
  }

  uint8_t month = 1u;
  while (month <= 12u) {
    uint8_t days_in_month = 0u;
    if (jh_calendar_days_in_month((uint16_t)year, month, &days_in_month) !=
        HAL_OK) {
      return HAL_EINVAL;
    }
    if (remaining_days < days_in_month) {
      break;
    }
    remaining_days -= days_in_month;
    ++month;
  }
  if (month > 12u) {
    return HAL_EOVERFLOW;
  }

  uint32_t seconds_of_day = (uint32_t)(epoch % JH_SECONDS_PER_DAY);
  jh_calendar_datetime_t datetime = {
      (uint16_t)year,
      month,
      (uint8_t)(remaining_days + 1u),
      (uint8_t)(seconds_of_day / 3600u),
      0u,
      0u,
      0u,
  };
  seconds_of_day %= 3600u;
  datetime.minute = (uint8_t)(seconds_of_day / 60u);
  datetime.second = (uint8_t)(seconds_of_day % 60u);
  if (jh_calendar_day_of_week(datetime.year, datetime.month, datetime.day,
                              &datetime.weekday) != HAL_OK) {
    return HAL_EINVAL;
  }

  *out_datetime = datetime;
  return HAL_OK;
}
