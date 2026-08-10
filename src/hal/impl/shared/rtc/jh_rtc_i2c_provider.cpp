#include "hal/hal_target.h"

#include "hal/hal_config.h"

#ifdef HAL_ENABLE_RTC

#include "hal/hal_i2c.h"
#include "hal/hal_serial.h"
#include "hal/impl/shared/rtc/jh_rtc_provider.h"

#ifdef HAL_ENABLE_PCF8563
#include "hal/impl/shared/drivers/pcf8563/pcf8563.h"
#endif
#ifdef HAL_ENABLE_DS3231
#include "hal/impl/shared/drivers/ds3231/ds3231.h"
#include <new>
#endif

#include <cstddef>
#include <cstring>

#ifdef HAL_ENABLE_PCF8563
typedef struct {
  pcf8563_t device;
} jh_rtc_pcf8563_context_t;

static_assert(sizeof(jh_rtc_pcf8563_context_t) <= JH_RTC_PROVIDER_STORAGE_SIZE,
              "PCF8563 provider storage is too small");
static_assert(alignof(jh_rtc_pcf8563_context_t) <= alignof(std::max_align_t),
              "PCF8563 provider alignment is too strict");

static jh_rtc_pcf8563_context_t *pcf_context(void *context) {
  return static_cast<jh_rtc_pcf8563_context_t *>(context);
}

static void rtc_to_pcf_datetime(const hal_rtc_datetime_t *source,
                                pcf8563_datetime_t *destination) {
  destination->second = source->second;
  destination->minute = source->minute;
  destination->hour = source->hour;
  destination->day = source->day;
  destination->weekday = source->weekday;
  destination->month = source->month;
  destination->year = source->year;
  destination->clock_integrity = source->clock_integrity;
}

static void pcf_to_rtc_datetime(const pcf8563_datetime_t *source,
                                hal_rtc_datetime_t *destination) {
  destination->second = source->second;
  destination->minute = source->minute;
  destination->hour = source->hour;
  destination->day = source->day;
  destination->weekday = source->weekday;
  destination->month = source->month;
  destination->year = source->year;
  destination->clock_integrity = source->clock_integrity;
}

static void rtc_to_pcf_alarm(const hal_rtc_alarm_t *source,
                             pcf8563_alarm_t *destination) {
  destination->minute_enabled = source->minute_enabled;
  destination->minute = source->minute;
  destination->hour_enabled = source->hour_enabled;
  destination->hour = source->hour;
  destination->day_enabled = source->day_enabled;
  destination->day = source->day;
  destination->weekday_enabled = source->weekday_enabled;
  destination->weekday = source->weekday;
}

static void pcf_to_rtc_alarm(const pcf8563_alarm_t *source,
                             hal_rtc_alarm_t *destination) {
  destination->minute_enabled = source->minute_enabled;
  destination->minute = source->minute;
  destination->hour_enabled = source->hour_enabled;
  destination->hour = source->hour;
  destination->day_enabled = source->day_enabled;
  destination->day = source->day;
  destination->weekday_enabled = source->weekday_enabled;
  destination->weekday = source->weekday;
}

static bool rtc_to_pcf_clkout_mode(hal_rtc_clkout_mode_t mode,
                                   pcf8563_clkout_mode_t *out_mode) {
  if (out_mode == nullptr) {
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
  if (out_mode == nullptr) {
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
  if (out_timer_clock == nullptr) {
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
  if (out_timer_clock == nullptr) {
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

static hal_status_t pcf_initialize(void *context,
                                   const hal_rtc_config_t *config) {
  if (context == nullptr || config == nullptr) {
    return HAL_EINVAL;
  }

  jh_rtc_pcf8563_context_t *pcf = pcf_context(context);
  pcf->device.i2c_bus = config->bus.i2c.i2c_bus;
  pcf->device.i2c_addr = config->bus.i2c.i2c_addr;
  if (!pcf8563_probe(&pcf->device)) {
    hal_derr("hal_rtc_init: PCF8563 probe failed (bus=%u addr=0x%02X)",
             (unsigned)pcf->device.i2c_bus, (unsigned)pcf->device.i2c_addr);
    return HAL_EIO;
  }
  return HAL_OK;
}

static void pcf_deinitialize(void *context) {
  if (context != nullptr) {
    std::memset(context, 0, sizeof(jh_rtc_pcf8563_context_t));
  }
}

static hal_status_t pcf_get_datetime(void *context,
                                     hal_rtc_datetime_t *out_datetime) {
  if (context == nullptr || out_datetime == nullptr) {
    return HAL_EINVAL;
  }

  pcf8563_datetime_t datetime = {};
  if (!pcf8563_get_datetime(&pcf_context(context)->device, &datetime)) {
    return HAL_EIO;
  }
  pcf_to_rtc_datetime(&datetime, out_datetime);
  return HAL_OK;
}

static hal_status_t pcf_set_datetime(void *context,
                                     const hal_rtc_datetime_t *datetime) {
  if (context == nullptr || datetime == nullptr) {
    return HAL_EINVAL;
  }

  pcf8563_datetime_t pcf_datetime = {};
  rtc_to_pcf_datetime(datetime, &pcf_datetime);
  return hal_status_from_bool(
      pcf8563_set_datetime(&pcf_context(context)->device, &pcf_datetime),
      HAL_EIO);
}

static hal_status_t pcf_get_clock_integrity(void *context, bool *out_ok) {
  if (context == nullptr || out_ok == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(
      pcf8563_get_clock_integrity(&pcf_context(context)->device, out_ok),
      HAL_EIO);
}

static hal_status_t pcf_set_interrupt_enable(void *context, uint8_t irq_mask) {
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(
      pcf8563_set_interrupt_enable(&pcf_context(context)->device,
                                   (irq_mask & HAL_RTC_IRQ_ALARM) != 0u,
                                   (irq_mask & HAL_RTC_IRQ_TIMER) != 0u),
      HAL_EIO);
}

static hal_status_t pcf_get_interrupt_enable(void *context,
                                             uint8_t *out_irq_mask) {
  if (context == nullptr || out_irq_mask == nullptr) {
    return HAL_EINVAL;
  }

  bool alarm_enabled = false;
  bool timer_enabled = false;
  if (!pcf8563_get_interrupt_enable(&pcf_context(context)->device,
                                    &alarm_enabled, &timer_enabled)) {
    return HAL_EIO;
  }

  uint8_t irq_mask = 0u;
  if (alarm_enabled) {
    irq_mask = (uint8_t)(irq_mask | HAL_RTC_IRQ_ALARM);
  }
  if (timer_enabled) {
    irq_mask = (uint8_t)(irq_mask | HAL_RTC_IRQ_TIMER);
  }
  *out_irq_mask = irq_mask;
  return HAL_OK;
}

static hal_status_t pcf_get_and_clear_flags(void *context, uint8_t *out_flags) {
  if (context == nullptr || out_flags == nullptr) {
    return HAL_EINVAL;
  }

  bool alarm_flag = false;
  bool timer_flag = false;
  if (!pcf8563_get_and_clear_flags(&pcf_context(context)->device, &alarm_flag,
                                   &timer_flag)) {
    return HAL_EIO;
  }

  uint8_t flags = 0u;
  if (alarm_flag) {
    flags = (uint8_t)(flags | HAL_RTC_FLAG_ALARM);
  }
  if (timer_flag) {
    flags = (uint8_t)(flags | HAL_RTC_FLAG_TIMER);
  }
  *out_flags = flags;
  return HAL_OK;
}

static hal_status_t pcf_get_temperature(void *context,
                                        float *out_temperature_c) {
  if (context == nullptr || out_temperature_c == nullptr) {
    return HAL_EINVAL;
  }
  return HAL_EUNSUPPORTED;
}

static hal_status_t pcf_set_clkout_mode(void *context,
                                        hal_rtc_clkout_mode_t mode) {
  if (context == nullptr) {
    return HAL_EINVAL;
  }

  pcf8563_clkout_mode_t pcf_mode = PCF8563_CLKOUT_DISABLED;
  if (!rtc_to_pcf_clkout_mode(mode, &pcf_mode)) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(
      pcf8563_set_clkout_mode(&pcf_context(context)->device, pcf_mode),
      HAL_EIO);
}

static hal_status_t pcf_get_clkout_mode(void *context,
                                        hal_rtc_clkout_mode_t *out_mode) {
  if (context == nullptr || out_mode == nullptr) {
    return HAL_EINVAL;
  }

  pcf8563_clkout_mode_t pcf_mode = PCF8563_CLKOUT_DISABLED;
  if (!pcf8563_get_clkout_mode(&pcf_context(context)->device, &pcf_mode)) {
    return HAL_EIO;
  }
  return pcf_to_rtc_clkout_mode(pcf_mode, out_mode) ? HAL_OK : HAL_EIO;
}

static hal_status_t
pcf_set_timer(void *context, hal_rtc_timer_clock_t timer_clock, uint8_t count) {
  if (context == nullptr) {
    return HAL_EINVAL;
  }

  pcf8563_timer_clock_t pcf_timer_clock = PCF8563_TIMER_DISABLED;
  if (!rtc_to_pcf_timer_clock(timer_clock, &pcf_timer_clock)) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(
      pcf8563_set_timer(&pcf_context(context)->device, pcf_timer_clock, count),
      HAL_EIO);
}

static hal_status_t pcf_get_timer(void *context,
                                  hal_rtc_timer_clock_t *out_timer_clock,
                                  uint8_t *out_count) {
  if (context == nullptr || out_timer_clock == nullptr ||
      out_count == nullptr) {
    return HAL_EINVAL;
  }

  pcf8563_timer_clock_t pcf_timer_clock = PCF8563_TIMER_DISABLED;
  if (!pcf8563_get_timer(&pcf_context(context)->device, &pcf_timer_clock,
                         out_count)) {
    return HAL_EIO;
  }
  return pcf_to_rtc_timer_clock(pcf_timer_clock, out_timer_clock) ? HAL_OK
                                                                  : HAL_EIO;
}

static hal_status_t pcf_set_alarm(void *context, const hal_rtc_alarm_t *alarm) {
  if (context == nullptr || alarm == nullptr) {
    return HAL_EINVAL;
  }

  pcf8563_alarm_t pcf_alarm = {};
  rtc_to_pcf_alarm(alarm, &pcf_alarm);
  return hal_status_from_bool(
      pcf8563_set_alarm(&pcf_context(context)->device, &pcf_alarm), HAL_EIO);
}

static hal_status_t pcf_get_alarm(void *context, hal_rtc_alarm_t *out_alarm) {
  if (context == nullptr || out_alarm == nullptr) {
    return HAL_EINVAL;
  }

  pcf8563_alarm_t pcf_alarm = {};
  if (!pcf8563_get_alarm(&pcf_context(context)->device, &pcf_alarm)) {
    return HAL_EIO;
  }
  pcf_to_rtc_alarm(&pcf_alarm, out_alarm);
  return HAL_OK;
}

static const jh_rtc_provider_ops_t s_pcf8563_provider = {
    HAL_RTC_PCF8563_DEFAULT_I2C_ADDR,
    false,
    pcf_initialize,
    pcf_deinitialize,
    pcf_get_datetime,
    pcf_set_datetime,
    pcf_get_clock_integrity,
    pcf_set_interrupt_enable,
    pcf_get_interrupt_enable,
    pcf_get_and_clear_flags,
    pcf_get_temperature,
    pcf_set_clkout_mode,
    pcf_get_clkout_mode,
    pcf_set_timer,
    pcf_get_timer,
    pcf_set_alarm,
    pcf_get_alarm,
};
#endif /* HAL_ENABLE_PCF8563 */

#ifdef HAL_ENABLE_DS3231
typedef struct {
  hal_rtc_clkout_mode_t clkout_mode;
  alignas(DS3231) uint8_t device_storage[sizeof(DS3231)];
} jh_rtc_ds3231_context_t;

static_assert(sizeof(jh_rtc_ds3231_context_t) <= JH_RTC_PROVIDER_STORAGE_SIZE,
              "DS3231 provider storage is too small");
static_assert(alignof(jh_rtc_ds3231_context_t) <= alignof(std::max_align_t),
              "DS3231 provider alignment is too strict");

static jh_rtc_ds3231_context_t *ds_context(void *context) {
  return static_cast<jh_rtc_ds3231_context_t *>(context);
}

static DS3231 *ds_device(void *context) {
  return reinterpret_cast<DS3231 *>(ds_context(context)->device_storage);
}

static bool ds_probe(uint8_t i2c_bus, uint8_t i2c_addr) {
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
  return (uint8_t)(weekday - 1u);
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

static hal_status_t ds_set_clkout(void *context, hal_rtc_clkout_mode_t mode) {
  if (context == nullptr) {
    return HAL_EINVAL;
  }

  DS3231 *rtc = ds_device(context);
  switch (mode) {
  case HAL_RTC_CLKOUT_DISABLED:
    rtc->enable32kHz(false);
    rtc->enableOscillator(false, false, 0u);
    break;
  case HAL_RTC_CLKOUT_1_HZ:
    rtc->enable32kHz(false);
    rtc->enableOscillator(true, false, 0u);
    break;
  case HAL_RTC_CLKOUT_1024_HZ:
    rtc->enable32kHz(false);
    rtc->enableOscillator(true, false, 1u);
    break;
  case HAL_RTC_CLKOUT_32768_HZ:
    rtc->enableOscillator(false, false, 0u);
    rtc->enable32kHz(true);
    break;
  case HAL_RTC_CLKOUT_32_HZ:
    return HAL_EUNSUPPORTED;
  default:
    return HAL_EINVAL;
  }

  ds_context(context)->clkout_mode = mode;
  return HAL_OK;
}

static hal_status_t rtc_to_ds3231_alarm2(const hal_rtc_alarm_t *source,
                                         uint8_t *out_day, uint8_t *out_hour,
                                         uint8_t *out_minute,
                                         uint8_t *out_alarm_bits,
                                         bool *out_day_mode) {
  if (source->day_enabled && source->weekday_enabled) {
    return HAL_EUNSUPPORTED;
  }

  uint8_t bits = 0u;
  if (!source->minute_enabled) {
    bits = (uint8_t)(bits | 0x10u);
  }
  if (!source->hour_enabled) {
    bits = (uint8_t)(bits | 0x20u);
  }

  uint8_t day = 1u;
  bool day_mode = false;
  if (source->day_enabled) {
    day = source->day;
  } else if (source->weekday_enabled) {
    day_mode = true;
    day = rtc_weekday_to_ds3231(source->weekday);
  } else {
    bits = (uint8_t)(bits | 0x40u);
  }

  *out_day = day;
  *out_hour = source->hour;
  *out_minute = source->minute;
  *out_alarm_bits = bits;
  *out_day_mode = day_mode;
  return HAL_OK;
}

static void ds3231_alarm2_to_rtc(uint8_t day, uint8_t hour, uint8_t minute,
                                 uint8_t alarm_bits, bool day_mode,
                                 hal_rtc_alarm_t *destination) {
  destination->minute_enabled = (alarm_bits & 0x10u) == 0u;
  destination->minute = destination->minute_enabled ? minute : 0u;
  destination->hour_enabled = (alarm_bits & 0x20u) == 0u;
  destination->hour = destination->hour_enabled ? hour : 0u;

  const bool day_or_weekday_enabled = (alarm_bits & 0x40u) == 0u;
  destination->day_enabled = day_or_weekday_enabled && !day_mode;
  destination->day = destination->day_enabled ? day : 0u;
  destination->weekday_enabled = day_or_weekday_enabled && day_mode;
  destination->weekday =
      destination->weekday_enabled ? ds3231_weekday_to_rtc(day) : 0u;
}

static hal_status_t ds_initialize(void *context,
                                  const hal_rtc_config_t *config) {
  if (context == nullptr || config == nullptr) {
    return HAL_EINVAL;
  }

  const hal_rtc_i2c_cfg_t *i2c = &config->bus.i2c;
  if (!ds_probe(i2c->i2c_bus, i2c->i2c_addr)) {
    hal_derr("hal_rtc_init: DS3231 probe failed (bus=%u addr=0x%02X)",
             (unsigned)i2c->i2c_bus, (unsigned)i2c->i2c_addr);
    return HAL_EIO;
  }

  new (ds_context(context)->device_storage) DS3231(i2c->i2c_bus, i2c->i2c_addr);
  return ds_set_clkout(context, HAL_RTC_CLKOUT_DISABLED);
}

static void ds_deinitialize(void *context) {
  if (context != nullptr) {
    ds_device(context)->~DS3231();
    std::memset(context, 0, sizeof(jh_rtc_ds3231_context_t));
  }
}

static hal_status_t ds_get_datetime(void *context,
                                    hal_rtc_datetime_t *out_datetime) {
  if (context == nullptr || out_datetime == nullptr) {
    return HAL_EINVAL;
  }

  DS3231 *rtc = ds_device(context);
  const DateTime now = RTClib::now(*rtc);
  *out_datetime = {
      (uint8_t)now.second(), (uint8_t)now.minute(),       (uint8_t)now.hour(),
      (uint8_t)now.day(),    (uint8_t)now.dayOfTheWeek(), (uint8_t)now.month(),
      (uint16_t)now.year(),  rtc->oscillatorCheck(),
  };
  return HAL_OK;
}

static hal_status_t ds_set_datetime(void *context,
                                    const hal_rtc_datetime_t *datetime) {
  if (context == nullptr || datetime == nullptr) {
    return HAL_EINVAL;
  }

  const DateTime ds_datetime(datetime->year, datetime->month, datetime->day,
                             datetime->hour, datetime->minute,
                             datetime->second);
  ds_device(context)->adjust(ds_datetime);
  return HAL_OK;
}

static hal_status_t ds_get_clock_integrity(void *context, bool *out_ok) {
  if (context == nullptr || out_ok == nullptr) {
    return HAL_EINVAL;
  }
  *out_ok = ds_device(context)->oscillatorCheck();
  return HAL_OK;
}

static hal_status_t ds_set_interrupt_enable(void *context, uint8_t irq_mask) {
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  if ((irq_mask & HAL_RTC_IRQ_TIMER) != 0u) {
    return HAL_EUNSUPPORTED;
  }

  DS3231 *rtc = ds_device(context);
  if ((irq_mask & HAL_RTC_IRQ_ALARM) != 0u) {
    rtc->turnOnAlarm(1u);
    rtc->turnOnAlarm(2u);
  } else {
    rtc->turnOffAlarm(1u);
    rtc->turnOffAlarm(2u);
  }
  return HAL_OK;
}

static hal_status_t ds_get_interrupt_enable(void *context,
                                            uint8_t *out_irq_mask) {
  if (context == nullptr || out_irq_mask == nullptr) {
    return HAL_EINVAL;
  }

  DS3231 *rtc = ds_device(context);
  *out_irq_mask = rtc->checkAlarmEnabled(1u) || rtc->checkAlarmEnabled(2u)
                      ? HAL_RTC_IRQ_ALARM
                      : 0u;
  return HAL_OK;
}

static hal_status_t ds_get_and_clear_flags(void *context, uint8_t *out_flags) {
  if (context == nullptr || out_flags == nullptr) {
    return HAL_EINVAL;
  }

  DS3231 *rtc = ds_device(context);
  const bool alarm1 = rtc->checkIfAlarm(1u, true);
  const bool alarm2 = rtc->checkIfAlarm(2u, true);
  *out_flags = alarm1 || alarm2 ? HAL_RTC_FLAG_ALARM : 0u;
  return HAL_OK;
}

static hal_status_t ds_get_temperature(void *context,
                                       float *out_temperature_c) {
  if (context == nullptr || out_temperature_c == nullptr) {
    return HAL_EINVAL;
  }
  *out_temperature_c = ds_device(context)->getTemperature();
  return HAL_OK;
}

static hal_status_t ds_get_clkout_mode(void *context,
                                       hal_rtc_clkout_mode_t *out_mode) {
  if (context == nullptr || out_mode == nullptr) {
    return HAL_EINVAL;
  }
  *out_mode = ds_context(context)->clkout_mode;
  return HAL_OK;
}

static hal_status_t
ds_set_timer(void *context, hal_rtc_timer_clock_t timer_clock, uint8_t count) {
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  (void)timer_clock;
  (void)count;
  return HAL_EUNSUPPORTED;
}

static hal_status_t ds_get_timer(void *context,
                                 hal_rtc_timer_clock_t *out_timer_clock,
                                 uint8_t *out_count) {
  if (context == nullptr || out_timer_clock == nullptr ||
      out_count == nullptr) {
    return HAL_EINVAL;
  }
  return HAL_EUNSUPPORTED;
}

static hal_status_t ds_set_alarm(void *context, const hal_rtc_alarm_t *alarm) {
  if (context == nullptr || alarm == nullptr) {
    return HAL_EINVAL;
  }

  uint8_t day = 1u;
  uint8_t hour = 0u;
  uint8_t minute = 0u;
  uint8_t alarm_bits = 0u;
  bool day_mode = false;
  const hal_status_t status =
      rtc_to_ds3231_alarm2(alarm, &day, &hour, &minute, &alarm_bits, &day_mode);
  if (status != HAL_OK) {
    return status;
  }

  ds_device(context)->setA2Time(day, hour, minute, alarm_bits, day_mode, false,
                                false);
  return HAL_OK;
}

static hal_status_t ds_get_alarm(void *context, hal_rtc_alarm_t *out_alarm) {
  if (context == nullptr || out_alarm == nullptr) {
    return HAL_EINVAL;
  }

  uint8_t day = 0u;
  uint8_t hour = 0u;
  uint8_t minute = 0u;
  uint8_t alarm_bits = 0u;
  bool day_mode = false;
  bool h12 = false;
  bool pm = false;
  ds_device(context)->getA2Time(day, hour, minute, alarm_bits, day_mode, h12,
                                pm, true);
  if (day_mode && (alarm_bits & 0x40u) == 0u && (day < 1u || day > 7u)) {
    return HAL_EIO;
  }

  ds3231_alarm2_to_rtc(day, ds3231_hour_to_24h(hour, h12, pm), minute,
                       alarm_bits, day_mode, out_alarm);
  return HAL_OK;
}

static const jh_rtc_provider_ops_t s_ds3231_provider = {
    HAL_RTC_DS3231_DEFAULT_I2C_ADDR,
    true,
    ds_initialize,
    ds_deinitialize,
    ds_get_datetime,
    ds_set_datetime,
    ds_get_clock_integrity,
    ds_set_interrupt_enable,
    ds_get_interrupt_enable,
    ds_get_and_clear_flags,
    ds_get_temperature,
    ds_set_clkout,
    ds_get_clkout_mode,
    ds_set_timer,
    ds_get_timer,
    ds_set_alarm,
    ds_get_alarm,
};
#endif /* HAL_ENABLE_DS3231 */

const jh_rtc_provider_ops_t *jh_rtc_i2c_provider_get_ops(hal_rtc_chip_t chip) {
  switch (chip) {
#ifdef HAL_ENABLE_PCF8563
  case HAL_RTC_CHIP_PCF8563:
    return &s_pcf8563_provider;
#endif
#ifdef HAL_ENABLE_DS3231
  case HAL_RTC_CHIP_DS3231:
    return &s_ds3231_provider;
#endif
  default:
    return nullptr;
  }
}

#if !HAL_TARGET_IS_MOCK
const jh_rtc_provider_ops_t *jh_rtc_provider_get_ops(hal_rtc_chip_t chip) {
  return jh_rtc_i2c_provider_get_ops(chip);
}
#endif

#endif /* HAL_ENABLE_RTC */
