#pragma once

#include "hal/rtc/hal_rtc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* LSE: 32768 / (127 + 1) / (255 + 1) = 1 Hz. */
#define JH_G474_RTC_PRER_LSE ((127u << 16) | 255u)
/* LSI nominal: 32000 / (127 + 1) / (249 + 1) = 1 Hz. */
#define JH_G474_RTC_PRER_LSI ((127u << 16) | 249u)
#define JH_G474_RTC_MIN_YEAR 2000u

#define JH_G474_RTC_ALARM_MSK1 (1u << 7)
#define JH_G474_RTC_ALARM_MSK2 (1u << 15)
#define JH_G474_RTC_ALARM_MSK3 (1u << 23)
#define JH_G474_RTC_ALARM_MSK4 (1u << 31)
#define JH_G474_RTC_ALARM_WDSEL (1u << 30)

static inline uint8_t jh_stm32g474_rtc_to_bcd(uint8_t value) {
  return (uint8_t)(((value / 10u) << 4) | (value % 10u));
}

static inline bool jh_stm32g474_rtc_from_bcd(uint8_t bcd, uint8_t maximum,
                                             uint8_t *out_value) {
  if (out_value == NULL || (bcd & 0x0fu) > 9u || ((bcd >> 4) & 0x0fu) > 9u) {
    return false;
  }
  const uint8_t value = (uint8_t)(((bcd >> 4) & 0x0fu) * 10u + (bcd & 0x0fu));
  if (value > maximum) {
    return false;
  }
  *out_value = value;
  return true;
}

static inline uint8_t jh_stm32g474_rtc_days_in_month(uint16_t year,
                                                     uint8_t month) {
  static const uint8_t days[] = {31u, 28u, 31u, 30u, 31u, 30u,
                                 31u, 31u, 30u, 31u, 30u, 31u};
  if (month < 1u || month > 12u) {
    return 0u;
  }
  if (month == 2u && (year % 4u) == 0u) {
    return 29u;
  }
  return days[month - 1u];
}

static inline bool
jh_stm32g474_rtc_datetime_valid(const hal_rtc_datetime_t *datetime) {
  if (datetime == NULL || datetime->year < JH_G474_RTC_MIN_YEAR ||
      datetime->year > HAL_RTC_MAX_YEAR || datetime->month < 1u ||
      datetime->month > 12u || datetime->hour > 23u || datetime->minute > 59u ||
      datetime->second > 59u || datetime->weekday > 6u) {
    return false;
  }
  const uint8_t days =
      jh_stm32g474_rtc_days_in_month(datetime->year, datetime->month);
  return datetime->day >= 1u && datetime->day <= days;
}

static inline bool
jh_stm32g474_rtc_encode_datetime(const hal_rtc_datetime_t *datetime,
                                 uint32_t *out_tr, uint32_t *out_dr) {
  if (!jh_stm32g474_rtc_datetime_valid(datetime) || out_tr == NULL ||
      out_dr == NULL) {
    return false;
  }

  const uint8_t second = jh_stm32g474_rtc_to_bcd(datetime->second);
  const uint8_t minute = jh_stm32g474_rtc_to_bcd(datetime->minute);
  const uint8_t hour = jh_stm32g474_rtc_to_bcd(datetime->hour);
  const uint8_t day = jh_stm32g474_rtc_to_bcd(datetime->day);
  const uint8_t month = jh_stm32g474_rtc_to_bcd(datetime->month);
  const uint8_t year =
      jh_stm32g474_rtc_to_bcd((uint8_t)(datetime->year - 2000u));

  *out_tr =
      ((uint32_t)(second & 0x0fu) << 0) | ((uint32_t)(second & 0x70u) << 0) |
      ((uint32_t)(minute & 0x0fu) << 8) | ((uint32_t)(minute & 0x70u) << 8) |
      ((uint32_t)(hour & 0x0fu) << 16) | ((uint32_t)(hour & 0x30u) << 16);
  *out_dr = ((uint32_t)(day & 0x0fu) << 0) | ((uint32_t)(day & 0x30u) << 0) |
            ((uint32_t)(month & 0x0fu) << 8) |
            ((uint32_t)(month & 0x10u) << 8) |
            ((uint32_t)(datetime->weekday + 1u) << 13) |
            ((uint32_t)(year & 0x0fu) << 16) | ((uint32_t)(year & 0xf0u) << 16);
  return true;
}

static inline bool
jh_stm32g474_rtc_decode_datetime(uint32_t tr, uint32_t dr,
                                 hal_rtc_datetime_t *out_datetime) {
  if (out_datetime == NULL || (tr & (1u << 22)) != 0u) {
    return false;
  }

  hal_rtc_datetime_t value = {0u, 0u, 0u, 0u, 0u, 0u, 0u, false};
  uint8_t year = 0u;
  const uint8_t second_bcd = (uint8_t)(((tr >> 4) & 0x07u) << 4 | (tr & 0x0fu));
  const uint8_t minute_bcd =
      (uint8_t)(((tr >> 12) & 0x07u) << 4 | ((tr >> 8) & 0x0fu));
  const uint8_t hour_bcd =
      (uint8_t)(((tr >> 20) & 0x03u) << 4 | ((tr >> 16) & 0x0fu));
  const uint8_t day_bcd = (uint8_t)(((dr >> 4) & 0x03u) << 4 | (dr & 0x0fu));
  const uint8_t month_bcd =
      (uint8_t)(((dr >> 12) & 0x01u) << 4 | ((dr >> 8) & 0x0fu));
  const uint8_t year_bcd =
      (uint8_t)(((dr >> 20) & 0x0fu) << 4 | ((dr >> 16) & 0x0fu));
  const uint8_t weekday = (uint8_t)((dr >> 13) & 0x07u);

  if (!jh_stm32g474_rtc_from_bcd(second_bcd, 59u, &value.second) ||
      !jh_stm32g474_rtc_from_bcd(minute_bcd, 59u, &value.minute) ||
      !jh_stm32g474_rtc_from_bcd(hour_bcd, 23u, &value.hour) ||
      !jh_stm32g474_rtc_from_bcd(day_bcd, 31u, &value.day) ||
      !jh_stm32g474_rtc_from_bcd(month_bcd, 12u, &value.month) ||
      !jh_stm32g474_rtc_from_bcd(year_bcd, 99u, &year) || weekday < 1u ||
      weekday > 7u) {
    return false;
  }
  value.weekday = (uint8_t)(weekday - 1u);
  value.year = (uint16_t)(2000u + year);
  if (!jh_stm32g474_rtc_datetime_valid(&value)) {
    return false;
  }
  *out_datetime = value;
  return true;
}

static inline bool
jh_stm32g474_rtc_alarm_enabled(const hal_rtc_alarm_t *alarm) {
  return alarm != NULL && (alarm->minute_enabled || alarm->hour_enabled ||
                           alarm->day_enabled || alarm->weekday_enabled);
}

static inline bool jh_stm32g474_rtc_encode_alarm(const hal_rtc_alarm_t *alarm,
                                                 uint32_t *out_register) {
  if (alarm == NULL || out_register == NULL ||
      (alarm->minute_enabled && alarm->minute > 59u) ||
      (alarm->hour_enabled && alarm->hour > 23u) ||
      (alarm->day_enabled && (alarm->day < 1u || alarm->day > 31u)) ||
      (alarm->weekday_enabled && alarm->weekday > 6u) ||
      (alarm->day_enabled && alarm->weekday_enabled)) {
    return false;
  }

  uint32_t value = JH_G474_RTC_ALARM_MSK1;
  if (alarm->minute_enabled) {
    const uint8_t minute = jh_stm32g474_rtc_to_bcd(alarm->minute);
    value |=
        ((uint32_t)(minute & 0x0fu) << 8) | ((uint32_t)(minute & 0x70u) << 8);
  } else {
    value |= JH_G474_RTC_ALARM_MSK2;
  }
  if (alarm->hour_enabled) {
    const uint8_t hour = jh_stm32g474_rtc_to_bcd(alarm->hour);
    value |=
        ((uint32_t)(hour & 0x0fu) << 16) | ((uint32_t)(hour & 0x30u) << 16);
  } else {
    value |= JH_G474_RTC_ALARM_MSK3;
  }
  if (alarm->day_enabled) {
    const uint8_t day = jh_stm32g474_rtc_to_bcd(alarm->day);
    value |= ((uint32_t)(day & 0x0fu) << 24) | ((uint32_t)(day & 0x30u) << 24);
  } else if (alarm->weekday_enabled) {
    value |= JH_G474_RTC_ALARM_WDSEL | ((uint32_t)(alarm->weekday + 1u) << 24);
  } else {
    value |= JH_G474_RTC_ALARM_MSK4;
  }
  *out_register = value;
  return true;
}

static inline bool jh_stm32g474_rtc_decode_alarm(uint32_t reg,
                                                 hal_rtc_alarm_t *out_alarm) {
  if (out_alarm == NULL) {
    return false;
  }

  hal_rtc_alarm_t alarm = {false, 0u, false, 0u, false, 0u, false, 0u};
  if ((reg & JH_G474_RTC_ALARM_MSK2) == 0u) {
    const uint8_t minute_bcd =
        (uint8_t)(((reg >> 12) & 0x07u) << 4 | ((reg >> 8) & 0x0fu));
    if (!jh_stm32g474_rtc_from_bcd(minute_bcd, 59u, &alarm.minute)) {
      return false;
    }
    alarm.minute_enabled = true;
  }
  if ((reg & JH_G474_RTC_ALARM_MSK3) == 0u) {
    const uint8_t hour_bcd =
        (uint8_t)(((reg >> 20) & 0x03u) << 4 | ((reg >> 16) & 0x0fu));
    if (!jh_stm32g474_rtc_from_bcd(hour_bcd, 23u, &alarm.hour)) {
      return false;
    }
    alarm.hour_enabled = true;
  }
  if ((reg & JH_G474_RTC_ALARM_MSK4) == 0u) {
    if ((reg & JH_G474_RTC_ALARM_WDSEL) != 0u) {
      const uint8_t weekday = (uint8_t)((reg >> 24) & 0x0fu);
      if (weekday < 1u || weekday > 7u) {
        return false;
      }
      alarm.weekday_enabled = true;
      alarm.weekday = (uint8_t)(weekday - 1u);
    } else {
      const uint8_t day_bcd =
          (uint8_t)(((reg >> 28) & 0x03u) << 4 | ((reg >> 24) & 0x0fu));
      if (!jh_stm32g474_rtc_from_bcd(day_bcd, 31u, &alarm.day) ||
          alarm.day < 1u) {
        return false;
      }
      alarm.day_enabled = true;
    }
  }
  *out_alarm = alarm;
  return true;
}
