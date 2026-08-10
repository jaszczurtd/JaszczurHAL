#pragma once

#include "hal/hal_rtc.h"

#ifdef HAL_ENABLE_RTC

#include "hal/hal_status.h"
#include "hal/hal_target.h"

#include <stddef.h>

/** Storage reserved in each public RTC handle for the selected provider. */
#define JH_RTC_PROVIDER_STORAGE_SIZE 128u

typedef struct {
  uint8_t default_i2c_addr;
  bool fixed_i2c_addr;
  hal_status_t (*initialize)(void *context, const hal_rtc_config_t *config);
  void (*deinitialize)(void *context);
  hal_status_t (*get_datetime)(void *context, hal_rtc_datetime_t *out_datetime);
  hal_status_t (*set_datetime)(void *context,
                               const hal_rtc_datetime_t *datetime);
  hal_status_t (*get_clock_integrity)(void *context, bool *out_ok);
  hal_status_t (*set_interrupt_enable)(void *context, uint8_t irq_mask);
  hal_status_t (*get_interrupt_enable)(void *context, uint8_t *out_irq_mask);
  hal_status_t (*get_and_clear_flags)(void *context, uint8_t *out_flags);
  hal_status_t (*get_temperature)(void *context, float *out_temperature_c);
  hal_status_t (*set_clkout_mode)(void *context, hal_rtc_clkout_mode_t mode);
  hal_status_t (*get_clkout_mode)(void *context,
                                  hal_rtc_clkout_mode_t *out_mode);
  hal_status_t (*set_timer)(void *context, hal_rtc_timer_clock_t timer_clock,
                            uint8_t count);
  hal_status_t (*get_timer)(void *context,
                            hal_rtc_timer_clock_t *out_timer_clock,
                            uint8_t *out_count);
  hal_status_t (*set_alarm)(void *context, const hal_rtc_alarm_t *alarm);
  hal_status_t (*get_alarm)(void *context, hal_rtc_alarm_t *out_alarm);
} jh_rtc_provider_ops_t;

/** Return the provider selected for a chip on the active target. */
const jh_rtc_provider_ops_t *jh_rtc_provider_get_ops(hal_rtc_chip_t chip);

/** Return the shared hardware-I2C provider for direct provider tests. */
const jh_rtc_provider_ops_t *jh_rtc_i2c_provider_get_ops(hal_rtc_chip_t chip);

#if HAL_TARGET_IS_MOCK
hal_status_t
jh_rtc_mock_provider_set_datetime(void *context,
                                  const hal_rtc_datetime_t *datetime);
hal_status_t jh_rtc_mock_provider_set_clock_integrity(void *context, bool ok);
hal_status_t jh_rtc_mock_provider_set_flags(void *context, uint8_t flags);
#endif

#endif /* HAL_ENABLE_RTC */
