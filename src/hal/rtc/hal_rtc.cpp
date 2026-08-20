#include "hal/rtc/hal_rtc.h"

#ifdef HAL_ENABLE_RTC

#ifdef HAL_ENABLE_I2C
#include "hal/i2c/hal_i2c.h"
#endif
#include "hal/rtc/jh_rtc_provider.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_sync.h"
#include "hal/time/jh_calendar.h"

#if HAL_TARGET_IS_MOCK
#include "hal/impl/.mock/hal_mock.h"
#endif

#include <cstddef>
#include <cstring>

struct hal_rtc_impl_s {
  bool in_use;
  bool provider_active;
  hal_mutex_t mutex;
  const jh_rtc_provider_ops_t *provider;
  alignas(std::max_align_t) uint8_t
      provider_context[JH_RTC_PROVIDER_STORAGE_SIZE];
};

static hal_rtc_impl_t s_pool[HAL_RTC_MAX_INSTANCES];

static bool rtc_provider_valid(const jh_rtc_provider_ops_t *provider) {
  const bool valid_bus =
      provider != nullptr && (provider->bus == JH_RTC_PROVIDER_BUS_I2C ||
                              provider->bus == JH_RTC_PROVIDER_BUS_INTERNAL);
  const bool valid_address =
      provider != nullptr && (provider->bus != JH_RTC_PROVIDER_BUS_I2C ||
                              provider->default_i2c_addr <= 0x7fu);
  return valid_bus && valid_address && provider->initialize != nullptr &&
         provider->deinitialize != nullptr &&
         provider->get_datetime != nullptr &&
         provider->set_datetime != nullptr &&
         provider->get_clock_integrity != nullptr &&
         provider->get_clock_source != nullptr &&
         provider->set_interrupt_enable != nullptr &&
         provider->get_interrupt_enable != nullptr &&
         provider->get_and_clear_flags != nullptr &&
         provider->get_temperature != nullptr &&
         provider->set_clkout_mode != nullptr &&
         provider->get_clkout_mode != nullptr &&
         provider->set_timer != nullptr && provider->get_timer != nullptr &&
         provider->set_alarm != nullptr && provider->get_alarm != nullptr &&
         provider->wakeup_arm != nullptr &&
         provider->wakeup_cancel != nullptr &&
         provider->wakeup_get_state != nullptr;
}

static bool rtc_handle_valid(hal_rtc_t handle) {
  return handle != nullptr && handle->in_use && handle->provider_active &&
         handle->mutex != nullptr && rtc_provider_valid(handle->provider);
}

static void rtc_release_pool_slot(hal_rtc_impl_t *handle) {
  hal_critical_section_enter();
  handle->in_use = false;
  hal_critical_section_exit();
}

static void rtc_discard_handle(hal_rtc_impl_t *handle) {
  if (handle->provider_active) {
    handle->provider->deinitialize(handle->provider_context);
    handle->provider_active = false;
  }
  if (handle->mutex != nullptr) {
    hal_mutex_destroy(handle->mutex);
    handle->mutex = nullptr;
  }
  handle->provider = nullptr;
  rtc_release_pool_slot(handle);
}

static bool rtc_validate_datetime(const hal_rtc_datetime_t *datetime) {
  if (datetime == nullptr) {
    return false;
  }

  const jh_calendar_datetime_t calendar = {
      datetime->year,   datetime->month,  datetime->day,     datetime->hour,
      datetime->minute, datetime->second, datetime->weekday,
  };
  return jh_calendar_validate_datetime(&calendar, HAL_RTC_MIN_YEAR,
                                       HAL_RTC_MAX_YEAR) == HAL_OK;
}

static bool rtc_validate_alarm(const hal_rtc_alarm_t *alarm) {
  return alarm != nullptr && (!alarm->minute_enabled || alarm->minute <= 59u) &&
         (!alarm->hour_enabled || alarm->hour <= 23u) &&
         (!alarm->day_enabled || (alarm->day >= 1u && alarm->day <= 31u)) &&
         (!alarm->weekday_enabled || alarm->weekday <= 6u);
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

static hal_status_t rtc_datetime_to_epoch(const hal_rtc_datetime_t *datetime,
                                          uint64_t *out_epoch) {
  if (datetime == nullptr || out_epoch == nullptr) {
    return HAL_EINVAL;
  }

  const jh_calendar_datetime_t calendar = {
      datetime->year,   datetime->month,  datetime->day,     datetime->hour,
      datetime->minute, datetime->second, datetime->weekday,
  };
  return jh_calendar_datetime_to_epoch(&calendar, out_epoch);
}

static hal_status_t rtc_epoch_to_datetime(uint64_t epoch,
                                          hal_rtc_datetime_t *out_datetime) {
  if (out_datetime == nullptr) {
    return HAL_EINVAL;
  }

  jh_calendar_datetime_t calendar = {};
  const hal_status_t status =
      jh_calendar_epoch_to_datetime(epoch, HAL_RTC_MAX_YEAR, &calendar);
  if (status != HAL_OK) {
    return status;
  }

  *out_datetime = {
      calendar.second,  calendar.minute, calendar.hour, calendar.day,
      calendar.weekday, calendar.month,  calendar.year, true,
  };
  return HAL_OK;
}

hal_status_t hal_rtc_init_ex(const hal_rtc_config_t *config,
                             hal_rtc_t *out_handle) {
  if (config == nullptr || out_handle == nullptr) {
    return HAL_EINVAL;
  }
  *out_handle = nullptr;

  const jh_rtc_provider_ops_t *provider = jh_rtc_provider_get_ops(config->chip);
  if (!rtc_provider_valid(provider)) {
    hal_derr("hal_rtc_init: unsupported RTC backend %d", (int)config->chip);
    return HAL_EUNSUPPORTED;
  }

  hal_critical_section_enter();
  int slot = -1;
  for (int index = 0; index < HAL_RTC_MAX_INSTANCES; ++index) {
    if (!s_pool[index].in_use) {
      slot = index;
      s_pool[index].in_use = true;
      break;
    }
  }
  hal_critical_section_exit();
  if (slot < 0) {
    return HAL_ENOMEM;
  }

  hal_rtc_impl_t *handle = &s_pool[slot];
  std::memset(handle, 0, sizeof(*handle));
  handle->in_use = true;
  handle->provider = provider;
  handle->mutex = hal_mutex_create();
  if (handle->mutex == nullptr) {
    rtc_discard_handle(handle);
    return HAL_ENOMEM;
  }

  hal_rtc_config_t normalized = *config;
  if (provider->bus == JH_RTC_PROVIDER_BUS_I2C) {
#ifdef HAL_ENABLE_I2C
    hal_rtc_i2c_cfg_t *i2c = &normalized.bus.i2c;
    if (i2c->i2c_addr == 0u) {
      i2c->i2c_addr = provider->default_i2c_addr;
    }
    if (provider->default_i2c_addr == 0u || i2c->i2c_addr > 0x7fu ||
        i2c->clock_hz == 0u ||
        (provider->fixed_i2c_addr &&
         i2c->i2c_addr != provider->default_i2c_addr)) {
      hal_derr("hal_rtc_init: invalid I2C config (addr=0x%02X, clock=%lu)",
               (unsigned)i2c->i2c_addr, (unsigned long)i2c->clock_hz);
      rtc_discard_handle(handle);
      return HAL_EINVAL;
    }

    const hal_status_t i2c_status = hal_i2c_init_bus(
        i2c->i2c_bus, i2c->sda_pin, i2c->scl_pin, i2c->clock_hz);
    if (i2c_status != HAL_OK) {
      rtc_discard_handle(handle);
      return i2c_status;
    }
#else
    rtc_discard_handle(handle);
    return HAL_EUNSUPPORTED;
#endif
  }

  const hal_status_t provider_status =
      provider->initialize(handle->provider_context, &normalized);
  if (provider_status != HAL_OK) {
    rtc_discard_handle(handle);
    return provider_status;
  }
  handle->provider_active = true;
  *out_handle = handle;
  return HAL_OK;
}

hal_rtc_t hal_rtc_init(const hal_rtc_config_t *config) {
  hal_rtc_t handle = nullptr;
  (void)hal_rtc_init_ex(config, &handle);
  return handle;
}

void hal_rtc_deinit(hal_rtc_t handle) {
  if (!rtc_handle_valid(handle)) {
    return;
  }

  hal_mutex_lock(handle->mutex);
  handle->provider->deinitialize(handle->provider_context);
  handle->provider_active = false;
  hal_mutex_t mutex = handle->mutex;
  handle->mutex = nullptr;
  handle->provider = nullptr;
  hal_mutex_unlock(mutex);
  hal_mutex_destroy(mutex);
  rtc_release_pool_slot(handle);
}

hal_status_t hal_rtc_get_datetime_ex(hal_rtc_t handle,
                                     hal_rtc_datetime_t *out_datetime) {
  if (!rtc_handle_valid(handle) || out_datetime == nullptr) {
    return HAL_EINVAL;
  }

  hal_rtc_datetime_t datetime = {};
  hal_mutex_lock(handle->mutex);
  hal_status_t status =
      handle->provider->get_datetime(handle->provider_context, &datetime);
  hal_mutex_unlock(handle->mutex);
  if (status == HAL_OK && !rtc_validate_datetime(&datetime)) {
    status = HAL_EIO;
  }
  if (status == HAL_OK) {
    *out_datetime = datetime;
  }
  return status;
}

bool hal_rtc_get_datetime(hal_rtc_t handle, hal_rtc_datetime_t *out_datetime) {
  return hal_status_to_bool(hal_rtc_get_datetime_ex(handle, out_datetime));
}

hal_status_t hal_rtc_set_datetime_ex(hal_rtc_t handle,
                                     const hal_rtc_datetime_t *datetime) {
  if (!rtc_handle_valid(handle) || !rtc_validate_datetime(datetime)) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->set_datetime(handle->provider_context, datetime);
  hal_mutex_unlock(handle->mutex);
  return status;
}

bool hal_rtc_set_datetime(hal_rtc_t handle,
                          const hal_rtc_datetime_t *datetime) {
  return hal_status_to_bool(hal_rtc_set_datetime_ex(handle, datetime));
}

hal_status_t hal_rtc_get_epoch_ex(hal_rtc_t handle, uint64_t *out_epoch) {
  if (!rtc_handle_valid(handle) || out_epoch == nullptr) {
    return HAL_EINVAL;
  }

  hal_rtc_datetime_t datetime = {};
  const hal_status_t status = hal_rtc_get_datetime_ex(handle, &datetime);
  return status == HAL_OK ? rtc_datetime_to_epoch(&datetime, out_epoch)
                          : status;
}

bool hal_rtc_get_epoch(hal_rtc_t handle, uint64_t *out_epoch) {
  return hal_status_to_bool(hal_rtc_get_epoch_ex(handle, out_epoch));
}

hal_status_t hal_rtc_set_epoch_ex(hal_rtc_t handle, uint64_t epoch) {
  if (!rtc_handle_valid(handle)) {
    return HAL_EINVAL;
  }

  hal_rtc_datetime_t datetime = {};
  const hal_status_t status = rtc_epoch_to_datetime(epoch, &datetime);
  return status == HAL_OK ? hal_rtc_set_datetime_ex(handle, &datetime) : status;
}

bool hal_rtc_set_epoch(hal_rtc_t handle, uint64_t epoch) {
  return hal_status_to_bool(hal_rtc_set_epoch_ex(handle, epoch));
}

hal_status_t hal_rtc_get_clock_integrity_ex(hal_rtc_t handle, bool *out_ok) {
  if (!rtc_handle_valid(handle) || out_ok == nullptr) {
    return HAL_EINVAL;
  }

  bool ok = false;
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->get_clock_integrity(handle->provider_context, &ok);
  hal_mutex_unlock(handle->mutex);
  if (status == HAL_OK) {
    *out_ok = ok;
  }
  return status;
}

bool hal_rtc_get_clock_integrity(hal_rtc_t handle, bool *out_ok) {
  return hal_status_to_bool(hal_rtc_get_clock_integrity_ex(handle, out_ok));
}

hal_status_t hal_rtc_get_clock_source_ex(hal_rtc_t handle,
                                         hal_rtc_clock_source_t *out_source) {
  if (!rtc_handle_valid(handle) || out_source == nullptr) {
    return HAL_EINVAL;
  }

  hal_rtc_clock_source_t source = HAL_RTC_CLOCK_SOURCE_AUTO;
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->get_clock_source(handle->provider_context, &source);
  hal_mutex_unlock(handle->mutex);
  if (status == HAL_OK) {
    *out_source = source;
  }
  return status;
}

bool hal_rtc_get_clock_source(hal_rtc_t handle,
                              hal_rtc_clock_source_t *out_source) {
  return hal_status_to_bool(hal_rtc_get_clock_source_ex(handle, out_source));
}

hal_status_t hal_rtc_set_interrupt_enable_ex(hal_rtc_t handle,
                                             uint8_t irq_mask) {
  if (!rtc_handle_valid(handle)) {
    return HAL_EINVAL;
  }

  const uint8_t normalized_mask =
      (uint8_t)(irq_mask &
                (HAL_RTC_IRQ_ALARM | HAL_RTC_IRQ_TIMER | HAL_RTC_IRQ_WAKEUP));
  hal_mutex_lock(handle->mutex);
  const hal_status_t status = handle->provider->set_interrupt_enable(
      handle->provider_context, normalized_mask);
  hal_mutex_unlock(handle->mutex);
  return status;
}

bool hal_rtc_set_interrupt_enable(hal_rtc_t handle, uint8_t irq_mask) {
  return hal_status_to_bool(hal_rtc_set_interrupt_enable_ex(handle, irq_mask));
}

hal_status_t hal_rtc_get_interrupt_enable_ex(hal_rtc_t handle,
                                             uint8_t *out_irq_mask) {
  if (!rtc_handle_valid(handle) || out_irq_mask == nullptr) {
    return HAL_EINVAL;
  }

  uint8_t irq_mask = 0u;
  hal_mutex_lock(handle->mutex);
  const hal_status_t status = handle->provider->get_interrupt_enable(
      handle->provider_context, &irq_mask);
  hal_mutex_unlock(handle->mutex);
  if (status == HAL_OK) {
    *out_irq_mask =
        (uint8_t)(irq_mask &
                  (HAL_RTC_IRQ_ALARM | HAL_RTC_IRQ_TIMER | HAL_RTC_IRQ_WAKEUP));
  }
  return status;
}

bool hal_rtc_get_interrupt_enable(hal_rtc_t handle, uint8_t *out_irq_mask) {
  return hal_status_to_bool(
      hal_rtc_get_interrupt_enable_ex(handle, out_irq_mask));
}

hal_status_t hal_rtc_get_and_clear_flags_ex(hal_rtc_t handle,
                                            uint8_t *out_flags) {
  if (!rtc_handle_valid(handle) || out_flags == nullptr) {
    return HAL_EINVAL;
  }

  uint8_t flags = 0u;
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->get_and_clear_flags(handle->provider_context, &flags);
  hal_mutex_unlock(handle->mutex);
  if (status == HAL_OK) {
    *out_flags = (uint8_t)(flags & (HAL_RTC_FLAG_ALARM | HAL_RTC_FLAG_TIMER |
                                    HAL_RTC_FLAG_WAKEUP));
  }
  return status;
}

bool hal_rtc_get_and_clear_flags(hal_rtc_t handle, uint8_t *out_flags) {
  return hal_status_to_bool(hal_rtc_get_and_clear_flags_ex(handle, out_flags));
}

hal_status_t hal_rtc_get_temperature_ex(hal_rtc_t handle,
                                        float *out_temperature_c) {
  if (!rtc_handle_valid(handle) || out_temperature_c == nullptr) {
    return HAL_EINVAL;
  }

  float temperature_c = 0.0f;
  hal_mutex_lock(handle->mutex);
  const hal_status_t status = handle->provider->get_temperature(
      handle->provider_context, &temperature_c);
  hal_mutex_unlock(handle->mutex);
  if (status == HAL_OK) {
    *out_temperature_c = temperature_c;
  }
  return status;
}

bool hal_rtc_get_temperature(hal_rtc_t handle, float *out_temperature_c) {
  return hal_status_to_bool(
      hal_rtc_get_temperature_ex(handle, out_temperature_c));
}

hal_status_t hal_rtc_set_clkout_mode_ex(hal_rtc_t handle,
                                        hal_rtc_clkout_mode_t mode) {
  if (!rtc_handle_valid(handle) || !rtc_validate_clkout_mode(mode)) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->set_clkout_mode(handle->provider_context, mode);
  hal_mutex_unlock(handle->mutex);
  return status;
}

bool hal_rtc_set_clkout_mode(hal_rtc_t handle, hal_rtc_clkout_mode_t mode) {
  return hal_status_to_bool(hal_rtc_set_clkout_mode_ex(handle, mode));
}

hal_status_t hal_rtc_get_clkout_mode_ex(hal_rtc_t handle,
                                        hal_rtc_clkout_mode_t *out_mode) {
  if (!rtc_handle_valid(handle) || out_mode == nullptr) {
    return HAL_EINVAL;
  }

  hal_rtc_clkout_mode_t mode = HAL_RTC_CLKOUT_DISABLED;
  hal_mutex_lock(handle->mutex);
  hal_status_t status =
      handle->provider->get_clkout_mode(handle->provider_context, &mode);
  hal_mutex_unlock(handle->mutex);
  if (status == HAL_OK && !rtc_validate_clkout_mode(mode)) {
    status = HAL_EIO;
  }
  if (status == HAL_OK) {
    *out_mode = mode;
  }
  return status;
}

bool hal_rtc_get_clkout_mode(hal_rtc_t handle,
                             hal_rtc_clkout_mode_t *out_mode) {
  return hal_status_to_bool(hal_rtc_get_clkout_mode_ex(handle, out_mode));
}

hal_status_t hal_rtc_set_timer_ex(hal_rtc_t handle,
                                  hal_rtc_timer_clock_t timer_clock,
                                  uint8_t count) {
  if (!rtc_handle_valid(handle) || !rtc_validate_timer_clock(timer_clock)) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->set_timer(handle->provider_context, timer_clock, count);
  hal_mutex_unlock(handle->mutex);
  return status;
}

bool hal_rtc_set_timer(hal_rtc_t handle, hal_rtc_timer_clock_t timer_clock,
                       uint8_t count) {
  return hal_status_to_bool(hal_rtc_set_timer_ex(handle, timer_clock, count));
}

hal_status_t hal_rtc_get_timer_ex(hal_rtc_t handle,
                                  hal_rtc_timer_clock_t *out_timer_clock,
                                  uint8_t *out_count) {
  if (!rtc_handle_valid(handle) || out_timer_clock == nullptr ||
      out_count == nullptr) {
    return HAL_EINVAL;
  }

  hal_rtc_timer_clock_t timer_clock = HAL_RTC_TIMER_DISABLED;
  uint8_t count = 0u;
  hal_mutex_lock(handle->mutex);
  hal_status_t status = handle->provider->get_timer(handle->provider_context,
                                                    &timer_clock, &count);
  hal_mutex_unlock(handle->mutex);
  if (status == HAL_OK && !rtc_validate_timer_clock(timer_clock)) {
    status = HAL_EIO;
  }
  if (status == HAL_OK) {
    *out_timer_clock = timer_clock;
    *out_count = count;
  }
  return status;
}

bool hal_rtc_get_timer(hal_rtc_t handle, hal_rtc_timer_clock_t *out_timer_clock,
                       uint8_t *out_count) {
  return hal_status_to_bool(
      hal_rtc_get_timer_ex(handle, out_timer_clock, out_count));
}

hal_status_t hal_rtc_set_alarm_ex(hal_rtc_t handle,
                                  const hal_rtc_alarm_t *alarm) {
  if (!rtc_handle_valid(handle) || !rtc_validate_alarm(alarm)) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->set_alarm(handle->provider_context, alarm);
  hal_mutex_unlock(handle->mutex);
  return status;
}

bool hal_rtc_set_alarm(hal_rtc_t handle, const hal_rtc_alarm_t *alarm) {
  return hal_status_to_bool(hal_rtc_set_alarm_ex(handle, alarm));
}

hal_status_t hal_rtc_get_alarm_ex(hal_rtc_t handle,
                                  hal_rtc_alarm_t *out_alarm) {
  if (!rtc_handle_valid(handle) || out_alarm == nullptr) {
    return HAL_EINVAL;
  }

  hal_rtc_alarm_t alarm = {};
  hal_mutex_lock(handle->mutex);
  hal_status_t status =
      handle->provider->get_alarm(handle->provider_context, &alarm);
  hal_mutex_unlock(handle->mutex);
  if (status == HAL_OK && !rtc_validate_alarm(&alarm)) {
    status = HAL_EIO;
  }
  if (status == HAL_OK) {
    *out_alarm = alarm;
  }
  return status;
}

bool hal_rtc_get_alarm(hal_rtc_t handle, hal_rtc_alarm_t *out_alarm) {
  return hal_status_to_bool(hal_rtc_get_alarm_ex(handle, out_alarm));
}

hal_status_t hal_rtc_wakeup_arm_ex(hal_rtc_t handle, uint64_t timeout_us,
                                   uint32_t flags) {
  if (!rtc_handle_valid(handle) || timeout_us == 0u ||
      (flags & ~HAL_RTC_WAKEUP_LOW_POWER) != 0u) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->wakeup_arm(handle->provider_context, timeout_us, flags);
  hal_mutex_unlock(handle->mutex);
  return status;
}

hal_status_t hal_rtc_wakeup_cancel_ex(hal_rtc_t handle) {
  if (!rtc_handle_valid(handle)) {
    return HAL_EINVAL;
  }

  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->wakeup_cancel(handle->provider_context);
  hal_mutex_unlock(handle->mutex);
  return status;
}

hal_status_t hal_rtc_wakeup_get_state_ex(hal_rtc_t handle,
                                         hal_rtc_wakeup_state_t *out_state) {
  if (!rtc_handle_valid(handle) || out_state == nullptr) {
    return HAL_EINVAL;
  }

  hal_rtc_wakeup_state_t state = {};
  hal_mutex_lock(handle->mutex);
  const hal_status_t status =
      handle->provider->wakeup_get_state(handle->provider_context, &state);
  hal_mutex_unlock(handle->mutex);
  if (status == HAL_OK) {
    *out_state = state;
  }
  return status;
}

#if HAL_TARGET_IS_MOCK
void hal_mock_rtc_set_datetime(hal_rtc_t handle,
                               const hal_rtc_datetime_t *datetime) {
  if (!rtc_handle_valid(handle) || !rtc_validate_datetime(datetime)) {
    return;
  }

  hal_mutex_lock(handle->mutex);
  (void)jh_rtc_mock_provider_set_datetime(handle->provider_context, datetime);
  hal_mutex_unlock(handle->mutex);
}

void hal_mock_rtc_set_clock_integrity(hal_rtc_t handle, bool ok) {
  if (!rtc_handle_valid(handle)) {
    return;
  }

  hal_mutex_lock(handle->mutex);
  (void)jh_rtc_mock_provider_set_clock_integrity(handle->provider_context, ok);
  hal_mutex_unlock(handle->mutex);
}

void hal_mock_rtc_set_flags(hal_rtc_t handle, uint8_t flags) {
  if (!rtc_handle_valid(handle)) {
    return;
  }

  hal_mutex_lock(handle->mutex);
  (void)jh_rtc_mock_provider_set_flags(handle->provider_context, flags);
  hal_mutex_unlock(handle->mutex);
}

void hal_mock_rtc_fire_wakeup(hal_rtc_t handle) {
  if (!rtc_handle_valid(handle)) {
    return;
  }

  hal_mutex_lock(handle->mutex);
  (void)jh_rtc_mock_provider_fire_wakeup(handle->provider_context);
  hal_mutex_unlock(handle->mutex);
}
#endif

#endif /* HAL_ENABLE_RTC */
