#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"
#ifdef HAL_ENABLE_RTC

#include "../../hal_i2c.h"
#include "../../hal_rtc.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"

#ifdef HAL_ENABLE_PCF8563
#include "../shared/drivers/pcf8563/pcf8563.h"
#endif
#ifdef HAL_ENABLE_DS3231
#include "../shared/drivers/ds3231/ds3231.h"
#include <new>
#endif

#include <string.h>

struct hal_rtc_impl_s {
  hal_rtc_chip_t chip;
  bool in_use;
  hal_mutex_t mutex;
#ifdef HAL_ENABLE_DS3231
  hal_rtc_clkout_mode_t ds3231_clkout_mode;
#endif
  union {
#ifdef HAL_ENABLE_PCF8563
    pcf8563_t pcf;
#endif
#ifdef HAL_ENABLE_DS3231
    alignas(DS3231) uint8_t ds3231_mem[sizeof(DS3231)];
#endif
  } backend;
};

static hal_rtc_impl_t s_pool[HAL_RTC_MAX_INSTANCES];

static bool rtc_backend_supported(hal_rtc_chip_t chip) {
  switch (chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563:
    return true;
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231:
    return true;
#endif
  default:
    return false;
  }
}

static void rtc_release_pool_slot(hal_rtc_impl_t *h) {
  hal_critical_section_enter();
  h->in_use = false;
  hal_critical_section_exit();
}

static bool rtc_validate_datetime(const hal_rtc_datetime_t *dt) {
  if (!dt) {
    return false;
  }
  if (dt->second > 59u || dt->minute > 59u || dt->hour > 23u) {
    return false;
  }
  if (dt->day < 1u || dt->day > 31u) {
    return false;
  }
  if (dt->weekday > 6u) {
    return false;
  }
  if (dt->month < 1u || dt->month > 12u) {
    return false;
  }
  if (dt->year < 1900u || dt->year > 2099u) {
    return false;
  }
  return true;
}

static bool rtc_validate_alarm(const hal_rtc_alarm_t *alarm) {
  if (!alarm) {
    return false;
  }
  if (alarm->minute_enabled && alarm->minute > 59u) {
    return false;
  }
  if (alarm->hour_enabled && alarm->hour > 23u) {
    return false;
  }
  if (alarm->day_enabled && (alarm->day < 1u || alarm->day > 31u)) {
    return false;
  }
  if (alarm->weekday_enabled && alarm->weekday > 6u) {
    return false;
  }
  return true;
}

static bool rtc_validate_clkout_mode(hal_rtc_clkout_mode_t mode) {
  switch (mode) {
  case HAL_RTC_CLKOUT_DISABLED:
  case HAL_RTC_CLKOUT_1_HZ:
  case HAL_RTC_CLKOUT_32_HZ:
  case HAL_RTC_CLKOUT_1024_HZ:
  case HAL_RTC_CLKOUT_32768_HZ:
    return true;
  default:
    return false;
  }
}

static bool rtc_validate_timer_clock(hal_rtc_timer_clock_t timer_clock) {
  switch (timer_clock) {
  case HAL_RTC_TIMER_DISABLED:
  case HAL_RTC_TIMER_1_60_HZ:
  case HAL_RTC_TIMER_1_HZ:
  case HAL_RTC_TIMER_64_HZ:
  case HAL_RTC_TIMER_4096_HZ:
    return true;
  default:
    return false;
  }
}

static bool rtc_is_leap_year(uint16_t year) {
  return ((year % 4u) == 0u && (year % 100u) != 0u) || ((year % 400u) == 0u);
}

static uint8_t rtc_days_in_month(uint16_t year, uint8_t month) {
  switch (month) {
  case 1u:
    return 31u;
  case 2u:
    return rtc_is_leap_year(year) ? 29u : 28u;
  case 3u:
    return 31u;
  case 4u:
    return 30u;
  case 5u:
    return 31u;
  case 6u:
    return 30u;
  case 7u:
    return 31u;
  case 8u:
    return 31u;
  case 9u:
    return 30u;
  case 10u:
    return 31u;
  case 11u:
    return 30u;
  case 12u:
    return 31u;
  default:
    return 0u;
  }
}

static bool rtc_datetime_to_epoch(const hal_rtc_datetime_t *dt,
                                  uint64_t *out_epoch) {
  if (!dt || !out_epoch || !rtc_validate_datetime(dt) || dt->year < 1970u) {
    return false;
  }

  uint64_t days = 0u;

  for (uint16_t year = 1970u; year < dt->year; ++year) {
    days += rtc_is_leap_year(year) ? 366u : 365u;
  }

  for (uint8_t month = 1u; month < dt->month; ++month) {
    const uint8_t dim = rtc_days_in_month(dt->year, month);
    if (dim == 0u) {
      return false;
    }
    days += dim;
  }

  days += (uint64_t)(dt->day - 1u);

  *out_epoch = days * 86400ull + (uint64_t)dt->hour * 3600ull +
               (uint64_t)dt->minute * 60ull + (uint64_t)dt->second;
  return true;
}

static bool rtc_epoch_to_datetime(uint64_t epoch, hal_rtc_datetime_t *out_dt) {
  if (!out_dt) {
    return false;
  }

  const uint64_t epoch_days = epoch / 86400ull;
  uint64_t remaining_days = epoch_days;
  uint32_t sod = (uint32_t)(epoch % 86400ull);

  uint16_t year = 1970u;
  while (year <= 2099u) {
    const uint16_t diy = rtc_is_leap_year(year) ? 366u : 365u;
    if (remaining_days < (uint64_t)diy) {
      break;
    }
    remaining_days -= (uint64_t)diy;
    ++year;
  }

  if (year > 2099u) {
    return false;
  }

  uint8_t month = 1u;
  while (month <= 12u) {
    const uint8_t dim = rtc_days_in_month(year, month);
    if (dim == 0u) {
      return false;
    }
    if (remaining_days < (uint64_t)dim) {
      break;
    }
    remaining_days -= (uint64_t)dim;
    ++month;
  }

  if (month > 12u) {
    return false;
  }

  out_dt->year = year;
  out_dt->month = month;
  out_dt->day = (uint8_t)(remaining_days + 1u);

  out_dt->hour = (uint8_t)(sod / 3600u);
  sod %= 3600u;
  out_dt->minute = (uint8_t)(sod / 60u);
  out_dt->second = (uint8_t)(sod % 60u);

  out_dt->weekday = (uint8_t)((epoch_days + 4ull) % 7ull);
  out_dt->clock_integrity = true;

  return rtc_validate_datetime(out_dt);
}

#ifdef HAL_ENABLE_PCF8563
static void rtc_to_pcf_datetime(const hal_rtc_datetime_t *src,
                                pcf8563_datetime_t *dst) {
  dst->second = src->second;
  dst->minute = src->minute;
  dst->hour = src->hour;
  dst->day = src->day;
  dst->weekday = src->weekday;
  dst->month = src->month;
  dst->year = src->year;
  dst->clock_integrity = src->clock_integrity;
}

static void pcf_to_rtc_datetime(const pcf8563_datetime_t *src,
                                hal_rtc_datetime_t *dst) {
  dst->second = src->second;
  dst->minute = src->minute;
  dst->hour = src->hour;
  dst->day = src->day;
  dst->weekday = src->weekday;
  dst->month = src->month;
  dst->year = src->year;
  dst->clock_integrity = src->clock_integrity;
}

static void rtc_to_pcf_alarm(const hal_rtc_alarm_t *src, pcf8563_alarm_t *dst) {
  dst->minute_enabled = src->minute_enabled;
  dst->minute = src->minute;
  dst->hour_enabled = src->hour_enabled;
  dst->hour = src->hour;
  dst->day_enabled = src->day_enabled;
  dst->day = src->day;
  dst->weekday_enabled = src->weekday_enabled;
  dst->weekday = src->weekday;
}

static void pcf_to_rtc_alarm(const pcf8563_alarm_t *src, hal_rtc_alarm_t *dst) {
  dst->minute_enabled = src->minute_enabled;
  dst->minute = src->minute;
  dst->hour_enabled = src->hour_enabled;
  dst->hour = src->hour;
  dst->day_enabled = src->day_enabled;
  dst->day = src->day;
  dst->weekday_enabled = src->weekday_enabled;
  dst->weekday = src->weekday;
}

static bool rtc_to_pcf_clkout_mode(hal_rtc_clkout_mode_t mode,
                                   pcf8563_clkout_mode_t *out_mode) {
  if (!out_mode) {
    return false;
  }

  switch (mode) {
  case HAL_RTC_CLKOUT_DISABLED:
    *out_mode = PCF8563_CLKOUT_DISABLED;
    return true;
  case HAL_RTC_CLKOUT_1_HZ:
    *out_mode = PCF8563_CLKOUT_1_HZ;
    return true;
  case HAL_RTC_CLKOUT_32_HZ:
    *out_mode = PCF8563_CLKOUT_32_HZ;
    return true;
  case HAL_RTC_CLKOUT_1024_HZ:
    *out_mode = PCF8563_CLKOUT_1024_HZ;
    return true;
  case HAL_RTC_CLKOUT_32768_HZ:
    *out_mode = PCF8563_CLKOUT_32768_HZ;
    return true;
  default:
    return false;
  }
}

static bool pcf_to_rtc_clkout_mode(pcf8563_clkout_mode_t mode,
                                   hal_rtc_clkout_mode_t *out_mode) {
  if (!out_mode) {
    return false;
  }

  switch (mode) {
  case PCF8563_CLKOUT_DISABLED:
    *out_mode = HAL_RTC_CLKOUT_DISABLED;
    return true;
  case PCF8563_CLKOUT_1_HZ:
    *out_mode = HAL_RTC_CLKOUT_1_HZ;
    return true;
  case PCF8563_CLKOUT_32_HZ:
    *out_mode = HAL_RTC_CLKOUT_32_HZ;
    return true;
  case PCF8563_CLKOUT_1024_HZ:
    *out_mode = HAL_RTC_CLKOUT_1024_HZ;
    return true;
  case PCF8563_CLKOUT_32768_HZ:
    *out_mode = HAL_RTC_CLKOUT_32768_HZ;
    return true;
  default:
    return false;
  }
}

static bool rtc_to_pcf_timer_clock(hal_rtc_timer_clock_t timer_clock,
                                   pcf8563_timer_clock_t *out_timer_clock) {
  if (!out_timer_clock) {
    return false;
  }

  switch (timer_clock) {
  case HAL_RTC_TIMER_DISABLED:
    *out_timer_clock = PCF8563_TIMER_DISABLED;
    return true;
  case HAL_RTC_TIMER_1_60_HZ:
    *out_timer_clock = PCF8563_TIMER_1_60_HZ;
    return true;
  case HAL_RTC_TIMER_1_HZ:
    *out_timer_clock = PCF8563_TIMER_1_HZ;
    return true;
  case HAL_RTC_TIMER_64_HZ:
    *out_timer_clock = PCF8563_TIMER_64_HZ;
    return true;
  case HAL_RTC_TIMER_4096_HZ:
    *out_timer_clock = PCF8563_TIMER_4096_HZ;
    return true;
  default:
    return false;
  }
}

static bool pcf_to_rtc_timer_clock(pcf8563_timer_clock_t timer_clock,
                                   hal_rtc_timer_clock_t *out_timer_clock) {
  if (!out_timer_clock) {
    return false;
  }

  switch (timer_clock) {
  case PCF8563_TIMER_DISABLED:
    *out_timer_clock = HAL_RTC_TIMER_DISABLED;
    return true;
  case PCF8563_TIMER_1_60_HZ:
    *out_timer_clock = HAL_RTC_TIMER_1_60_HZ;
    return true;
  case PCF8563_TIMER_1_HZ:
    *out_timer_clock = HAL_RTC_TIMER_1_HZ;
    return true;
  case PCF8563_TIMER_64_HZ:
    *out_timer_clock = HAL_RTC_TIMER_64_HZ;
    return true;
  case PCF8563_TIMER_4096_HZ:
    *out_timer_clock = HAL_RTC_TIMER_4096_HZ;
    return true;
  default:
    return false;
  }
}
#endif /* HAL_ENABLE_PCF8563 */

#ifdef HAL_ENABLE_DS3231
static inline DS3231 *as_ds3231(hal_rtc_impl_t *h) {
  return reinterpret_cast<DS3231 *>(h->backend.ds3231_mem);
}

static bool ds3231_probe(uint8_t i2c_bus, uint8_t i2c_addr) {
  hal_i2c_begin_transmission_bus(i2c_bus, i2c_addr);
  if (hal_i2c_write_bus(i2c_bus, 0x00u) != 1u) {
    (void)hal_i2c_end_transmission_bus(i2c_bus);
    return false;
  }
  return hal_i2c_end_transmission_bus(i2c_bus) == 0u;
}

static uint8_t rtc_weekday_to_ds3231(uint8_t weekday) {
  return (uint8_t)(weekday + 1u);
}

static uint8_t ds3231_weekday_to_rtc(uint8_t weekday) {
  return (weekday >= 1u && weekday <= 7u) ? (uint8_t)(weekday - 1u) : 0u;
}

static uint8_t ds3231_hour_to_24h(uint8_t hour, bool h12, bool pm) {
  if (!h12) {
    return hour <= 23u ? hour : (uint8_t)(hour % 24u);
  }

  if (hour == 12u) {
    return pm ? 12u : 0u;
  }

  if (hour > 12u) {
    hour = (uint8_t)(hour % 12u);
  }

  return pm ? (uint8_t)(hour + 12u) : hour;
}

static bool rtc_set_ds3231_clkout(hal_rtc_impl_t *h,
                                  hal_rtc_clkout_mode_t mode) {
  DS3231 *rtc = as_ds3231(h);

  switch (mode) {
  case HAL_RTC_CLKOUT_DISABLED:
    rtc->enable32kHz(false);
    rtc->enableOscillator(false, false, 0u);
    h->ds3231_clkout_mode = mode;
    return true;
  case HAL_RTC_CLKOUT_1_HZ:
    rtc->enable32kHz(false);
    rtc->enableOscillator(true, false, 0u);
    h->ds3231_clkout_mode = mode;
    return true;
  case HAL_RTC_CLKOUT_1024_HZ:
    rtc->enable32kHz(false);
    rtc->enableOscillator(true, false, 1u);
    h->ds3231_clkout_mode = mode;
    return true;
  case HAL_RTC_CLKOUT_32768_HZ:
    rtc->enableOscillator(false, false, 0u);
    rtc->enable32kHz(true);
    h->ds3231_clkout_mode = mode;
    return true;
  case HAL_RTC_CLKOUT_32_HZ:
  default:
    return false;
  }
}

static bool rtc_get_ds3231_temperature(hal_rtc_impl_t *h,
                                       float *out_temperature_c) {
  if (!h || !out_temperature_c) {
    return false;
  }

  *out_temperature_c = as_ds3231(h)->getTemperature();
  return true;
}

static bool rtc_to_ds3231_alarm2(const hal_rtc_alarm_t *src, uint8_t *out_day,
                                 uint8_t *out_hour, uint8_t *out_minute,
                                 uint8_t *out_alarm_bits, bool *out_day_mode) {
  if (!src || !out_day || !out_hour || !out_minute || !out_alarm_bits ||
      !out_day_mode) {
    return false;
  }

  if (src->day_enabled && src->weekday_enabled) {
    return false;
  }

  uint8_t bits = 0u;
  if (!src->minute_enabled) {
    bits = (uint8_t)(bits | 0x10u);
  }
  if (!src->hour_enabled) {
    bits = (uint8_t)(bits | 0x20u);
  }

  uint8_t day = 1u;
  bool day_mode = false;
  if (src->day_enabled) {
    day_mode = false;
    day = src->day;
  } else if (src->weekday_enabled) {
    day_mode = true;
    day = rtc_weekday_to_ds3231(src->weekday);
  } else {
    bits = (uint8_t)(bits | 0x40u);
  }

  *out_day = day;
  *out_hour = src->hour;
  *out_minute = src->minute;
  *out_alarm_bits = bits;
  *out_day_mode = day_mode;
  return true;
}

static void ds3231_alarm2_to_rtc(uint8_t day, uint8_t hour, uint8_t minute,
                                 uint8_t alarm_bits, bool day_mode,
                                 hal_rtc_alarm_t *dst) {
  const bool minute_enabled = (alarm_bits & 0x10u) == 0u;
  const bool hour_enabled = (alarm_bits & 0x20u) == 0u;
  const bool day_or_weekday_enabled = (alarm_bits & 0x40u) == 0u;

  dst->minute_enabled = minute_enabled;
  dst->minute = minute_enabled ? minute : 0u;
  dst->hour_enabled = hour_enabled;
  dst->hour = hour_enabled ? hour : 0u;

  if (!day_or_weekday_enabled) {
    dst->day_enabled = false;
    dst->day = 0u;
    dst->weekday_enabled = false;
    dst->weekday = 0u;
    return;
  }

  if (day_mode) {
    dst->day_enabled = false;
    dst->day = 0u;
    dst->weekday_enabled = true;
    dst->weekday = ds3231_weekday_to_rtc(day);
  } else {
    dst->day_enabled = true;
    dst->day = day;
    dst->weekday_enabled = false;
    dst->weekday = 0u;
  }
}
#endif /* HAL_ENABLE_DS3231 */

hal_status_t hal_rtc_init_ex(const hal_rtc_config_t *cfg,
                             hal_rtc_t *out_handle) {
  if (!cfg || !out_handle) {
    return HAL_EINVAL;
  }
  *out_handle = NULL;

  hal_critical_section_enter();
  int slot = -1;
  for (int i = 0; i < HAL_RTC_MAX_INSTANCES; ++i) {
    if (!s_pool[i].in_use) {
      slot = i;
      s_pool[i].in_use = true;
      break;
    }
  }
  hal_critical_section_exit();

  if (slot < 0) {
    return HAL_ENOMEM;
  }

  hal_rtc_impl_t *h = &s_pool[slot];
  memset(h, 0, sizeof(*h));
  h->in_use = true;
  h->chip = cfg->chip;
  h->mutex = hal_mutex_create();

  if (!h->mutex) {
    rtc_release_pool_slot(h);
    return HAL_ENOMEM;
  }

  if (!rtc_backend_supported(cfg->chip)) {
    hal_derr("hal_rtc_init: unsupported RTC backend %d", (int)cfg->chip);
    hal_mutex_destroy(h->mutex);
    h->mutex = NULL;
    rtc_release_pool_slot(h);
    return HAL_EUNSUPPORTED;
  }

  uint8_t default_addr = 0u;
  switch (cfg->chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563:
    default_addr = (uint8_t)HAL_RTC_PCF8563_DEFAULT_I2C_ADDR;
    break;
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231:
    default_addr = (uint8_t)HAL_RTC_DS3231_DEFAULT_I2C_ADDR;
    break;
#endif
  default:
    break;
  }

  const hal_rtc_i2c_cfg_t *ic = &cfg->bus.i2c;
  const uint8_t addr = (ic->i2c_addr != 0u) ? ic->i2c_addr : default_addr;

  if (default_addr == 0u || addr > 0x7Fu || ic->clock_hz == 0u) {
    hal_derr("hal_rtc_init: invalid I2C config (addr=0x%02X, clock=%lu)",
             (unsigned)addr, (unsigned long)ic->clock_hz);
    hal_mutex_destroy(h->mutex);
    h->mutex = NULL;
    rtc_release_pool_slot(h);
    return HAL_EINVAL;
  }

#ifdef HAL_ENABLE_DS3231
  if (cfg->chip == HAL_RTC_CHIP_DS3231 &&
      addr != (uint8_t)HAL_RTC_DS3231_DEFAULT_I2C_ADDR) {
    hal_derr("hal_rtc_init: DS3231 supports only addr=0x%02X (got 0x%02X)",
             (unsigned)HAL_RTC_DS3231_DEFAULT_I2C_ADDR, (unsigned)addr);
    hal_mutex_destroy(h->mutex);
    h->mutex = NULL;
    rtc_release_pool_slot(h);
    return HAL_EINVAL;
  }
#endif

  const hal_status_t i2c_status =
      hal_i2c_init_bus(ic->i2c_bus, ic->sda_pin, ic->scl_pin, ic->clock_hz);
  if (i2c_status != HAL_OK) {
    hal_mutex_destroy(h->mutex);
    h->mutex = NULL;
    rtc_release_pool_slot(h);
    return i2c_status;
  }

  switch (cfg->chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563:
    h->backend.pcf.i2c_bus = ic->i2c_bus;
    h->backend.pcf.i2c_addr = addr;
    if (!pcf8563_probe(&h->backend.pcf)) {
      hal_derr("hal_rtc_init: PCF8563 probe failed (bus=%u addr=0x%02X)",
               (unsigned)h->backend.pcf.i2c_bus,
               (unsigned)h->backend.pcf.i2c_addr);
      hal_mutex_destroy(h->mutex);
      h->mutex = NULL;
      rtc_release_pool_slot(h);
      return HAL_EIO;
    }
    break;
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231:
    if (!ds3231_probe(ic->i2c_bus, addr)) {
      hal_derr("hal_rtc_init: DS3231 probe failed (bus=%u addr=0x%02X)",
               (unsigned)ic->i2c_bus, (unsigned)addr);
      hal_mutex_destroy(h->mutex);
      h->mutex = NULL;
      rtc_release_pool_slot(h);
      return HAL_EIO;
    }

    new (h->backend.ds3231_mem) DS3231(ic->i2c_bus, addr);
    if (!rtc_set_ds3231_clkout(h, HAL_RTC_CLKOUT_DISABLED)) {
      as_ds3231(h)->~DS3231();
      hal_mutex_destroy(h->mutex);
      h->mutex = NULL;
      rtc_release_pool_slot(h);
      return HAL_EIO;
    }
    break;
#endif
  default:
    hal_derr("hal_rtc_init: unsupported RTC backend %d", (int)cfg->chip);
    hal_mutex_destroy(h->mutex);
    h->mutex = NULL;
    rtc_release_pool_slot(h);
    return HAL_EUNSUPPORTED;
  }

  *out_handle = h;
  return HAL_OK;
}

hal_rtc_t hal_rtc_init(const hal_rtc_config_t *cfg) {
  hal_rtc_t h = NULL;
  (void)hal_rtc_init_ex(cfg, &h);
  return h;
}

void hal_rtc_deinit(hal_rtc_t h) {
  if (!h) {
    return;
  }

  hal_mutex_lock(h->mutex);
#ifdef HAL_ENABLE_DS3231
  if (h->chip == HAL_RTC_CHIP_DS3231) {
    as_ds3231(h)->~DS3231();
  }
#endif
  hal_mutex_t m = h->mutex;
  h->mutex = NULL;
  hal_mutex_unlock(m);
  hal_mutex_destroy(m);
  rtc_release_pool_slot(h);
}

hal_status_t hal_rtc_get_datetime_ex(hal_rtc_t h, hal_rtc_datetime_t *out_dt) {
  if (!h || !out_dt) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(h->mutex);
  bool ok = false;

  switch (h->chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563: {
    pcf8563_datetime_t pcf_dt = {};
    ok = pcf8563_get_datetime(&h->backend.pcf, &pcf_dt);
    if (ok) {
      pcf_to_rtc_datetime(&pcf_dt, out_dt);
      ok = rtc_validate_datetime(out_dt);
    }
    break;
  }
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231: {
    DS3231 *rtc = as_ds3231(h);
    DateTime now = RTClib::now(*rtc);

    out_dt->second = (uint8_t)now.second();
    out_dt->minute = (uint8_t)now.minute();
    out_dt->hour = (uint8_t)now.hour();
    out_dt->day = (uint8_t)now.day();
    out_dt->weekday = (uint8_t)now.dayOfTheWeek();
    out_dt->month = (uint8_t)now.month();
    out_dt->year = (uint16_t)now.year();
    out_dt->clock_integrity = rtc->oscillatorCheck();

    ok = rtc_validate_datetime(out_dt);
    break;
  }
#endif
  default:
    ok = false;
    break;
  }

  hal_mutex_unlock(h->mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_rtc_get_datetime(hal_rtc_t h, hal_rtc_datetime_t *out_dt) {
  return hal_status_to_bool(hal_rtc_get_datetime_ex(h, out_dt));
}

hal_status_t hal_rtc_set_datetime_ex(hal_rtc_t h,
                                     const hal_rtc_datetime_t *dt) {
  if (!h || !dt || !rtc_validate_datetime(dt)) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(h->mutex);
  bool ok = false;

  switch (h->chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563: {
    pcf8563_datetime_t pcf_dt = {};
    rtc_to_pcf_datetime(dt, &pcf_dt);
    ok = pcf8563_set_datetime(&h->backend.pcf, &pcf_dt);
    break;
  }
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231: {
    DateTime ds_dt(dt->year, dt->month, dt->day, dt->hour, dt->minute,
                   dt->second);
    as_ds3231(h)->adjust(ds_dt);
    ok = true;
    break;
  }
#endif
  default:
    ok = false;
    break;
  }

  hal_mutex_unlock(h->mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_rtc_set_datetime(hal_rtc_t h, const hal_rtc_datetime_t *dt) {
  return hal_status_to_bool(hal_rtc_set_datetime_ex(h, dt));
}

hal_status_t hal_rtc_get_epoch_ex(hal_rtc_t h, uint64_t *out_epoch) {
  if (!h || !out_epoch) {
    return HAL_EINVAL;
  }

  hal_rtc_datetime_t dt = {};
  const hal_status_t status = hal_rtc_get_datetime_ex(h, &dt);
  if (status != HAL_OK) {
    return status;
  }

  return rtc_datetime_to_epoch(&dt, out_epoch) ? HAL_OK : HAL_EOVERFLOW;
}

bool hal_rtc_get_epoch(hal_rtc_t h, uint64_t *out_epoch) {
  return hal_status_to_bool(hal_rtc_get_epoch_ex(h, out_epoch));
}

hal_status_t hal_rtc_set_epoch_ex(hal_rtc_t h, uint64_t epoch) {
  if (!h) {
    return HAL_EINVAL;
  }

  hal_rtc_datetime_t dt = {};
  if (!rtc_epoch_to_datetime(epoch, &dt)) {
    return HAL_EOVERFLOW;
  }

  return hal_rtc_set_datetime_ex(h, &dt);
}

bool hal_rtc_set_epoch(hal_rtc_t h, uint64_t epoch) {
  return hal_status_to_bool(hal_rtc_set_epoch_ex(h, epoch));
}

hal_status_t hal_rtc_get_clock_integrity_ex(hal_rtc_t h, bool *out_ok) {
  if (!h || !out_ok) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(h->mutex);
  bool ok = false;

  switch (h->chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563:
    ok = pcf8563_get_clock_integrity(&h->backend.pcf, out_ok);
    break;
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231:
    *out_ok = as_ds3231(h)->oscillatorCheck();
    ok = true;
    break;
#endif
  default:
    ok = false;
    break;
  }

  hal_mutex_unlock(h->mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_rtc_get_clock_integrity(hal_rtc_t h, bool *out_ok) {
  return hal_status_to_bool(hal_rtc_get_clock_integrity_ex(h, out_ok));
}

hal_status_t hal_rtc_set_interrupt_enable_ex(hal_rtc_t h, uint8_t irq_mask) {
  if (!h) {
    return HAL_EINVAL;
  }
#ifdef HAL_ENABLE_DS3231
  if (h->chip == HAL_RTC_CHIP_DS3231 && (irq_mask & HAL_RTC_IRQ_TIMER) != 0u) {
    return HAL_EUNSUPPORTED;
  }
#endif

  hal_mutex_lock(h->mutex);
  bool ok = false;

  switch (h->chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563:
    ok = pcf8563_set_interrupt_enable(&h->backend.pcf,
                                      (irq_mask & HAL_RTC_IRQ_ALARM) != 0u,
                                      (irq_mask & HAL_RTC_IRQ_TIMER) != 0u);
    break;
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231: {
    if ((irq_mask & HAL_RTC_IRQ_TIMER) != 0u) {
      ok = false;
      break;
    }

    DS3231 *rtc = as_ds3231(h);
    if ((irq_mask & HAL_RTC_IRQ_ALARM) != 0u) {
      rtc->turnOnAlarm(1);
      rtc->turnOnAlarm(2);
    } else {
      rtc->turnOffAlarm(1);
      rtc->turnOffAlarm(2);
    }
    ok = true;
    break;
  }
#endif
  default:
    ok = false;
    break;
  }

  hal_mutex_unlock(h->mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_rtc_set_interrupt_enable(hal_rtc_t h, uint8_t irq_mask) {
  return hal_status_to_bool(hal_rtc_set_interrupt_enable_ex(h, irq_mask));
}

hal_status_t hal_rtc_get_interrupt_enable_ex(hal_rtc_t h,
                                             uint8_t *out_irq_mask) {
  if (!h || !out_irq_mask) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(h->mutex);
  bool ok = false;

  switch (h->chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563: {
    bool alarm_irq_enabled = false;
    bool timer_irq_enabled = false;
    ok = pcf8563_get_interrupt_enable(&h->backend.pcf, &alarm_irq_enabled,
                                      &timer_irq_enabled);
    if (ok) {
      uint8_t irq_mask = 0;
      if (alarm_irq_enabled) {
        irq_mask = (uint8_t)(irq_mask | HAL_RTC_IRQ_ALARM);
      }
      if (timer_irq_enabled) {
        irq_mask = (uint8_t)(irq_mask | HAL_RTC_IRQ_TIMER);
      }
      *out_irq_mask = irq_mask;
    }
    break;
  }
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231: {
    DS3231 *rtc = as_ds3231(h);
    const bool alarm_irq_enabled =
        rtc->checkAlarmEnabled(1) || rtc->checkAlarmEnabled(2);
    uint8_t irq_mask = 0u;
    if (alarm_irq_enabled) {
      irq_mask = (uint8_t)(irq_mask | HAL_RTC_IRQ_ALARM);
    }
    *out_irq_mask = irq_mask;
    ok = true;
    break;
  }
#endif
  default:
    ok = false;
    break;
  }

  hal_mutex_unlock(h->mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_rtc_get_interrupt_enable(hal_rtc_t h, uint8_t *out_irq_mask) {
  return hal_status_to_bool(hal_rtc_get_interrupt_enable_ex(h, out_irq_mask));
}

hal_status_t hal_rtc_get_and_clear_flags_ex(hal_rtc_t h, uint8_t *out_flags) {
  if (!h || !out_flags) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(h->mutex);
  bool ok = false;

  switch (h->chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563: {
    bool alarm_flag = false;
    bool timer_flag = false;
    ok = pcf8563_get_and_clear_flags(&h->backend.pcf, &alarm_flag, &timer_flag);
    if (ok) {
      uint8_t flags = 0;
      if (alarm_flag) {
        flags = (uint8_t)(flags | HAL_RTC_FLAG_ALARM);
      }
      if (timer_flag) {
        flags = (uint8_t)(flags | HAL_RTC_FLAG_TIMER);
      }
      *out_flags = flags;
    }
    break;
  }
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231: {
    const bool alarm1_flag = as_ds3231(h)->checkIfAlarm(1, true);
    const bool alarm2_flag = as_ds3231(h)->checkIfAlarm(2, true);
    uint8_t flags = 0u;
    if (alarm1_flag || alarm2_flag) {
      flags = (uint8_t)(flags | HAL_RTC_FLAG_ALARM);
    }
    *out_flags = flags;
    ok = true;
    break;
  }
#endif
  default:
    ok = false;
    break;
  }

  hal_mutex_unlock(h->mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_rtc_get_and_clear_flags(hal_rtc_t h, uint8_t *out_flags) {
  return hal_status_to_bool(hal_rtc_get_and_clear_flags_ex(h, out_flags));
}

hal_status_t hal_rtc_get_temperature_ex(hal_rtc_t h, float *out_temperature_c) {
  if (!h || !out_temperature_c) {
    return HAL_EINVAL;
  }
#ifdef HAL_ENABLE_PCF8563
  if (h->chip == HAL_RTC_CHIP_PCF8563) {
    return HAL_EUNSUPPORTED;
  }
#endif

  hal_mutex_lock(h->mutex);
  bool ok = false;

  switch (h->chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563:
    ok = false;
    break;
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231:
    ok = rtc_get_ds3231_temperature(h, out_temperature_c);
    break;
#endif
  default:
    ok = false;
    break;
  }

  hal_mutex_unlock(h->mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_rtc_get_temperature(hal_rtc_t h, float *out_temperature_c) {
  return hal_status_to_bool(hal_rtc_get_temperature_ex(h, out_temperature_c));
}

hal_status_t hal_rtc_set_clkout_mode_ex(hal_rtc_t h,
                                        hal_rtc_clkout_mode_t mode) {
  if (!h || !rtc_validate_clkout_mode(mode)) {
    return HAL_EINVAL;
  }
#ifdef HAL_ENABLE_DS3231
  if (h->chip == HAL_RTC_CHIP_DS3231 && mode == HAL_RTC_CLKOUT_32_HZ) {
    return HAL_EUNSUPPORTED;
  }
#endif

  hal_mutex_lock(h->mutex);
  bool ok = false;

  switch (h->chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563: {
    pcf8563_clkout_mode_t pcf_mode = PCF8563_CLKOUT_DISABLED;
    if (rtc_to_pcf_clkout_mode(mode, &pcf_mode)) {
      ok = pcf8563_set_clkout_mode(&h->backend.pcf, pcf_mode);
    } else {
      ok = false;
    }
    break;
  }
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231:
    ok = rtc_set_ds3231_clkout(h, mode);
    break;
#endif
  default:
    ok = false;
    break;
  }

  hal_mutex_unlock(h->mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_rtc_set_clkout_mode(hal_rtc_t h, hal_rtc_clkout_mode_t mode) {
  return hal_status_to_bool(hal_rtc_set_clkout_mode_ex(h, mode));
}

hal_status_t hal_rtc_get_clkout_mode_ex(hal_rtc_t h,
                                        hal_rtc_clkout_mode_t *out_mode) {
  if (!h || !out_mode) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(h->mutex);
  bool ok = false;

  switch (h->chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563: {
    pcf8563_clkout_mode_t pcf_mode = PCF8563_CLKOUT_DISABLED;
    ok = pcf8563_get_clkout_mode(&h->backend.pcf, &pcf_mode);
    if (ok) {
      ok = pcf_to_rtc_clkout_mode(pcf_mode, out_mode);
    }
    break;
  }
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231:
    *out_mode = h->ds3231_clkout_mode;
    ok = true;
    break;
#endif
  default:
    ok = false;
    break;
  }

  hal_mutex_unlock(h->mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_rtc_get_clkout_mode(hal_rtc_t h, hal_rtc_clkout_mode_t *out_mode) {
  return hal_status_to_bool(hal_rtc_get_clkout_mode_ex(h, out_mode));
}

hal_status_t hal_rtc_set_timer_ex(hal_rtc_t h,
                                  hal_rtc_timer_clock_t timer_clock,
                                  uint8_t count) {
  if (!h || !rtc_validate_timer_clock(timer_clock)) {
    return HAL_EINVAL;
  }
#ifdef HAL_ENABLE_DS3231
  if (h->chip == HAL_RTC_CHIP_DS3231) {
    return HAL_EUNSUPPORTED;
  }
#endif

  hal_mutex_lock(h->mutex);
  bool ok = false;

  switch (h->chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563: {
    pcf8563_timer_clock_t pcf_timer_clock = PCF8563_TIMER_DISABLED;
    if (rtc_to_pcf_timer_clock(timer_clock, &pcf_timer_clock)) {
      ok = pcf8563_set_timer(&h->backend.pcf, pcf_timer_clock, count);
    } else {
      ok = false;
    }
    break;
  }
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231:
    (void)timer_clock;
    (void)count;
    ok = false;
    break;
#endif
  default:
    ok = false;
    break;
  }

  hal_mutex_unlock(h->mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_rtc_set_timer(hal_rtc_t h, hal_rtc_timer_clock_t timer_clock,
                       uint8_t count) {
  return hal_status_to_bool(hal_rtc_set_timer_ex(h, timer_clock, count));
}

hal_status_t hal_rtc_get_timer_ex(hal_rtc_t h,
                                  hal_rtc_timer_clock_t *out_timer_clock,
                                  uint8_t *out_count) {
  if (!h || !out_timer_clock || !out_count) {
    return HAL_EINVAL;
  }
#ifdef HAL_ENABLE_DS3231
  if (h->chip == HAL_RTC_CHIP_DS3231) {
    return HAL_EUNSUPPORTED;
  }
#endif

  hal_mutex_lock(h->mutex);
  bool ok = false;

  switch (h->chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563: {
    pcf8563_timer_clock_t pcf_timer_clock = PCF8563_TIMER_DISABLED;
    ok = pcf8563_get_timer(&h->backend.pcf, &pcf_timer_clock, out_count);
    if (ok) {
      ok = pcf_to_rtc_timer_clock(pcf_timer_clock, out_timer_clock);
    }
    break;
  }
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231:
    ok = false;
    break;
#endif
  default:
    ok = false;
    break;
  }

  hal_mutex_unlock(h->mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_rtc_get_timer(hal_rtc_t h, hal_rtc_timer_clock_t *out_timer_clock,
                       uint8_t *out_count) {
  return hal_status_to_bool(
      hal_rtc_get_timer_ex(h, out_timer_clock, out_count));
}

hal_status_t hal_rtc_set_alarm_ex(hal_rtc_t h, const hal_rtc_alarm_t *alarm) {
  if (!h || !alarm || !rtc_validate_alarm(alarm)) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(h->mutex);
  bool ok = false;

  switch (h->chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563: {
    pcf8563_alarm_t pcf_alarm = {};
    rtc_to_pcf_alarm(alarm, &pcf_alarm);
    ok = pcf8563_set_alarm(&h->backend.pcf, &pcf_alarm);
    break;
  }
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231: {
    uint8_t day = 1u;
    uint8_t hour = 0u;
    uint8_t minute = 0u;
    uint8_t alarm_bits = 0u;
    bool day_mode = false;

    ok = rtc_to_ds3231_alarm2(alarm, &day, &hour, &minute, &alarm_bits,
                              &day_mode);
    if (ok) {
      as_ds3231(h)->setA2Time(day, hour, minute, alarm_bits, day_mode, false,
                              false);
      ok = true;
    }
    break;
  }
#endif
  default:
    ok = false;
    break;
  }

  hal_mutex_unlock(h->mutex);
  if (ok) {
    return HAL_OK;
  }
#ifdef HAL_ENABLE_DS3231
  return h->chip == HAL_RTC_CHIP_DS3231 ? HAL_EUNSUPPORTED : HAL_EIO;
#else
  return HAL_EIO;
#endif
}

bool hal_rtc_set_alarm(hal_rtc_t h, const hal_rtc_alarm_t *alarm) {
  return hal_status_to_bool(hal_rtc_set_alarm_ex(h, alarm));
}

hal_status_t hal_rtc_get_alarm_ex(hal_rtc_t h, hal_rtc_alarm_t *out_alarm) {
  if (!h || !out_alarm) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(h->mutex);
  bool ok = false;

  switch (h->chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563: {
    pcf8563_alarm_t pcf_alarm = {};
    ok = pcf8563_get_alarm(&h->backend.pcf, &pcf_alarm);
    if (ok) {
      pcf_to_rtc_alarm(&pcf_alarm, out_alarm);
      ok = rtc_validate_alarm(out_alarm);
    }
    break;
  }
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231: {
    uint8_t day = 0u;
    uint8_t hour = 0u;
    uint8_t minute = 0u;
    uint8_t alarm_bits = 0u;
    bool day_mode = false;
    bool h12 = false;
    bool pm = false;

    as_ds3231(h)->getA2Time(day, hour, minute, alarm_bits, day_mode, h12, pm,
                            true);

    if (day_mode && (alarm_bits & 0x40u) == 0u && (day < 1u || day > 7u)) {
      ok = false;
      break;
    }

    const uint8_t hour_24 = ds3231_hour_to_24h(hour, h12, pm);
    ds3231_alarm2_to_rtc(day, hour_24, minute, alarm_bits, day_mode, out_alarm);
    ok = rtc_validate_alarm(out_alarm);
    break;
  }
#endif
  default:
    ok = false;
    break;
  }

  hal_mutex_unlock(h->mutex);
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_rtc_get_alarm(hal_rtc_t h, hal_rtc_alarm_t *out_alarm) {
  return hal_status_to_bool(hal_rtc_get_alarm_ex(h, out_alarm));
}

#endif /* HAL_ENABLE_RTC */
#endif // HAL_TARGET_IS_RP2040
