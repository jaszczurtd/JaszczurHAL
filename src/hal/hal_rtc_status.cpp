#include "hal_rtc.h"

#ifdef HAL_ENABLE_RTC

/*
 * Backend-agnostic status adapter for the RTC HAL. Each wrapper validates the
 * handle and pointer arguments it can check locally (returning HAL_EINVAL
 * before touching the backend), then delegates to the legacy entry point.
 *
 * hal_rtc_init_ex() produces the handle through an output parameter and maps a
 * NULL result (invalid config / probe failure / pool exhaustion) to HAL_EIO.
 * The remaining bool operations are I2C register accesses, so a residual
 * failure maps to HAL_EIO; the legacy bool API cannot separate a genuine bus
 * error from an unsupported feature (for example DS3231-only temperature).
 */

hal_status_t hal_rtc_init_ex(const hal_rtc_config_t *cfg,
                             hal_rtc_t *out_handle) {
  if (cfg == nullptr || out_handle == nullptr) {
    return HAL_EINVAL;
  }
  *out_handle = hal_rtc_init(cfg);
  return *out_handle != nullptr ? HAL_OK : HAL_EIO;
}

hal_status_t hal_rtc_deinit_ex(hal_rtc_t h) {
  hal_rtc_deinit(h);
  return HAL_OK;
}

hal_status_t hal_rtc_get_datetime_ex(hal_rtc_t h, hal_rtc_datetime_t *out_dt) {
  if (h == nullptr || out_dt == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_rtc_get_datetime(h, out_dt), HAL_EIO);
}

hal_status_t hal_rtc_set_datetime_ex(hal_rtc_t h,
                                     const hal_rtc_datetime_t *dt) {
  if (h == nullptr || dt == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_rtc_set_datetime(h, dt), HAL_EIO);
}

hal_status_t hal_rtc_get_epoch_ex(hal_rtc_t h, uint64_t *out_epoch) {
  if (h == nullptr || out_epoch == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_rtc_get_epoch(h, out_epoch), HAL_EIO);
}

hal_status_t hal_rtc_set_epoch_ex(hal_rtc_t h, uint64_t epoch) {
  if (h == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_rtc_set_epoch(h, epoch), HAL_EIO);
}

hal_status_t hal_rtc_get_clock_integrity_ex(hal_rtc_t h, bool *out_ok) {
  if (h == nullptr || out_ok == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_rtc_get_clock_integrity(h, out_ok), HAL_EIO);
}

hal_status_t hal_rtc_set_interrupt_enable_ex(hal_rtc_t h, uint8_t irq_mask) {
  if (h == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_rtc_set_interrupt_enable(h, irq_mask),
                              HAL_EIO);
}

hal_status_t hal_rtc_get_interrupt_enable_ex(hal_rtc_t h,
                                             uint8_t *out_irq_mask) {
  if (h == nullptr || out_irq_mask == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_rtc_get_interrupt_enable(h, out_irq_mask),
                              HAL_EIO);
}

hal_status_t hal_rtc_get_and_clear_flags_ex(hal_rtc_t h, uint8_t *out_flags) {
  if (h == nullptr || out_flags == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_rtc_get_and_clear_flags(h, out_flags),
                              HAL_EIO);
}

hal_status_t hal_rtc_get_temperature_ex(hal_rtc_t h, float *out_temperature_c) {
  if (h == nullptr || out_temperature_c == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_rtc_get_temperature(h, out_temperature_c),
                              HAL_EIO);
}

hal_status_t hal_rtc_set_clkout_mode_ex(hal_rtc_t h,
                                        hal_rtc_clkout_mode_t mode) {
  if (h == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_rtc_set_clkout_mode(h, mode), HAL_EIO);
}

hal_status_t hal_rtc_get_clkout_mode_ex(hal_rtc_t h,
                                        hal_rtc_clkout_mode_t *out_mode) {
  if (h == nullptr || out_mode == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_rtc_get_clkout_mode(h, out_mode), HAL_EIO);
}

hal_status_t hal_rtc_set_timer_ex(hal_rtc_t h,
                                  hal_rtc_timer_clock_t timer_clock,
                                  uint8_t count) {
  if (h == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_rtc_set_timer(h, timer_clock, count),
                              HAL_EIO);
}

hal_status_t hal_rtc_get_timer_ex(hal_rtc_t h,
                                  hal_rtc_timer_clock_t *out_timer_clock,
                                  uint8_t *out_count) {
  if (h == nullptr || out_timer_clock == nullptr || out_count == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_rtc_get_timer(h, out_timer_clock, out_count),
                              HAL_EIO);
}

hal_status_t hal_rtc_set_alarm_ex(hal_rtc_t h, const hal_rtc_alarm_t *alarm) {
  if (h == nullptr || alarm == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_rtc_set_alarm(h, alarm), HAL_EIO);
}

hal_status_t hal_rtc_get_alarm_ex(hal_rtc_t h, hal_rtc_alarm_t *out_alarm) {
  if (h == nullptr || out_alarm == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_rtc_get_alarm(h, out_alarm), HAL_EIO);
}

#endif /* HAL_ENABLE_RTC */
