#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"
#ifdef HAL_ENABLE_RTC

#include "../../hal_i2c.h"
#include "../../hal_rtc.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "hal_mock.h"

#include <string.h>

struct hal_rtc_impl_s {
  hal_rtc_chip_t chip;
  bool in_use;
  hal_mutex_t mutex;
  hal_rtc_datetime_t dt;
  uint8_t irq_mask;
  uint8_t flags;
  hal_rtc_clkout_mode_t clkout_mode;
  hal_rtc_timer_clock_t timer_clock;
  uint8_t timer_count;
  hal_rtc_alarm_t alarm;
  uint8_t i2c_bus;
  uint8_t i2c_addr;
};

static hal_rtc_impl_t s_pool[HAL_RTC_MAX_INSTANCES];

static bool rtc_backend_supported(hal_rtc_chip_t chip) {
  switch (chip) {
  case HAL_RTC_CHIP_PCF8563:
  case HAL_RTC_CHIP_DS3231:
    return true;
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
    hal_serial_println("hal_rtc_init: unsupported RTC backend");
    hal_mutex_destroy(h->mutex);
    h->mutex = NULL;
    rtc_release_pool_slot(h);
    return HAL_EUNSUPPORTED;
  }

  const uint8_t default_addr = (cfg->chip == HAL_RTC_CHIP_DS3231)
                                   ? (uint8_t)HAL_RTC_DS3231_DEFAULT_I2C_ADDR
                                   : (uint8_t)HAL_RTC_PCF8563_DEFAULT_I2C_ADDR;

  const hal_rtc_i2c_cfg_t *ic = &cfg->bus.i2c;
  const uint8_t addr = (ic->i2c_addr != 0u) ? ic->i2c_addr : default_addr;

  if (addr > 0x7Fu || ic->clock_hz == 0u ||
      (cfg->chip == HAL_RTC_CHIP_DS3231 &&
       addr != (uint8_t)HAL_RTC_DS3231_DEFAULT_I2C_ADDR)) {
    hal_mutex_destroy(h->mutex);
    h->mutex = NULL;
    rtc_release_pool_slot(h);
    return HAL_EINVAL;
  }

  h->i2c_bus = ic->i2c_bus;
  h->i2c_addr = addr;

  const hal_status_t i2c_status =
      hal_i2c_init_bus(ic->i2c_bus, ic->sda_pin, ic->scl_pin, ic->clock_hz);
  if (i2c_status != HAL_OK) {
    hal_mutex_destroy(h->mutex);
    h->mutex = NULL;
    rtc_release_pool_slot(h);
    return i2c_status;
  }

  h->dt.second = 0;
  h->dt.minute = 0;
  h->dt.hour = 0;
  h->dt.day = 1;
  h->dt.weekday = 0;
  h->dt.month = 1;
  h->dt.year = 2000;
  h->dt.clock_integrity = true;

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
  h->in_use = false;
  hal_mutex_t m = h->mutex;
  h->mutex = NULL;
  hal_mutex_unlock(m);
  hal_mutex_destroy(m);
}

hal_status_t hal_rtc_get_datetime_ex(hal_rtc_t h, hal_rtc_datetime_t *out_dt) {
  if (!h || !out_dt) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(h->mutex);
  *out_dt = h->dt;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
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
  h->dt.second = dt->second;
  h->dt.minute = dt->minute;
  h->dt.hour = dt->hour;
  h->dt.day = dt->day;
  h->dt.weekday = dt->weekday;
  h->dt.month = dt->month;
  h->dt.year = dt->year;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
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
  *out_ok = h->dt.clock_integrity;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

bool hal_rtc_get_clock_integrity(hal_rtc_t h, bool *out_ok) {
  return hal_status_to_bool(hal_rtc_get_clock_integrity_ex(h, out_ok));
}

hal_status_t hal_rtc_set_interrupt_enable_ex(hal_rtc_t h, uint8_t irq_mask) {
  if (!h) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(h->mutex);
  h->irq_mask = (uint8_t)(irq_mask & (HAL_RTC_IRQ_ALARM | HAL_RTC_IRQ_TIMER));
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
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
  *out_irq_mask = h->irq_mask;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

bool hal_rtc_get_interrupt_enable(hal_rtc_t h, uint8_t *out_irq_mask) {
  return hal_status_to_bool(hal_rtc_get_interrupt_enable_ex(h, out_irq_mask));
}

hal_status_t hal_rtc_get_and_clear_flags_ex(hal_rtc_t h, uint8_t *out_flags) {
  if (!h || !out_flags) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(h->mutex);
  *out_flags = h->flags;
  h->flags = 0;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

bool hal_rtc_get_and_clear_flags(hal_rtc_t h, uint8_t *out_flags) {
  return hal_status_to_bool(hal_rtc_get_and_clear_flags_ex(h, out_flags));
}

hal_status_t hal_rtc_get_temperature_ex(hal_rtc_t h, float *out_temperature_c) {
  if (!h || !out_temperature_c) {
    return HAL_EINVAL;
  }

  return HAL_EUNSUPPORTED;
}

bool hal_rtc_get_temperature(hal_rtc_t h, float *out_temperature_c) {
  return hal_status_to_bool(hal_rtc_get_temperature_ex(h, out_temperature_c));
}

hal_status_t hal_rtc_set_clkout_mode_ex(hal_rtc_t h,
                                        hal_rtc_clkout_mode_t mode) {
  if (!h || !rtc_validate_clkout_mode(mode)) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(h->mutex);
  h->clkout_mode = mode;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
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
  *out_mode = h->clkout_mode;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
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

  hal_mutex_lock(h->mutex);
  h->timer_clock = timer_clock;
  h->timer_count = count;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
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

  hal_mutex_lock(h->mutex);
  *out_timer_clock = h->timer_clock;
  *out_count = h->timer_count;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
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
  h->alarm = *alarm;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

bool hal_rtc_set_alarm(hal_rtc_t h, const hal_rtc_alarm_t *alarm) {
  return hal_status_to_bool(hal_rtc_set_alarm_ex(h, alarm));
}

hal_status_t hal_rtc_get_alarm_ex(hal_rtc_t h, hal_rtc_alarm_t *out_alarm) {
  if (!h || !out_alarm) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(h->mutex);
  *out_alarm = h->alarm;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

bool hal_rtc_get_alarm(hal_rtc_t h, hal_rtc_alarm_t *out_alarm) {
  return hal_status_to_bool(hal_rtc_get_alarm_ex(h, out_alarm));
}

void hal_mock_rtc_set_datetime(hal_rtc_t h, const hal_rtc_datetime_t *dt) {
  if (!h || !rtc_validate_datetime(dt)) {
    return;
  }

  hal_mutex_lock(h->mutex);
  h->dt.second = dt->second;
  h->dt.minute = dt->minute;
  h->dt.hour = dt->hour;
  h->dt.day = dt->day;
  h->dt.weekday = dt->weekday;
  h->dt.month = dt->month;
  h->dt.year = dt->year;
  hal_mutex_unlock(h->mutex);
}

void hal_mock_rtc_set_clock_integrity(hal_rtc_t h, bool ok) {
  if (!h) {
    return;
  }

  hal_mutex_lock(h->mutex);
  h->dt.clock_integrity = ok;
  hal_mutex_unlock(h->mutex);
}

void hal_mock_rtc_set_flags(hal_rtc_t h, uint8_t flags) {
  if (!h) {
    return;
  }

  hal_mutex_lock(h->mutex);
  h->flags = (uint8_t)(flags & (HAL_RTC_FLAG_ALARM | HAL_RTC_FLAG_TIMER));
  hal_mutex_unlock(h->mutex);
}

#endif /* HAL_ENABLE_RTC */
#endif // HAL_TARGET_IS_MOCK
