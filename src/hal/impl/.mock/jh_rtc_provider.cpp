#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_MOCK

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_RTC

#include "hal/rtc/jh_rtc_provider.h"

#include <cstddef>
#include <cstring>

typedef struct {
  hal_rtc_datetime_t datetime;
  hal_rtc_alarm_t alarm;
  hal_rtc_clkout_mode_t clkout_mode;
  hal_rtc_timer_clock_t timer_clock;
  hal_rtc_clock_source_t clock_source;
  uint8_t timer_count;
  uint8_t irq_mask;
  uint8_t flags;
  hal_rtc_wakeup_state_t wakeup;
} jh_rtc_mock_context_t;

static_assert(sizeof(jh_rtc_mock_context_t) <= JH_RTC_PROVIDER_STORAGE_SIZE,
              "RTC mock provider storage is too small");
static_assert(alignof(jh_rtc_mock_context_t) <= alignof(std::max_align_t),
              "RTC mock provider alignment is too strict");

static jh_rtc_mock_context_t *mock_context(void *context) {
  return static_cast<jh_rtc_mock_context_t *>(context);
}

static hal_status_t mock_initialize(void *context,
                                    const hal_rtc_config_t *config) {
  if (context == nullptr || config == nullptr) {
    return HAL_EINVAL;
  }

  jh_rtc_mock_context_t *mock = mock_context(context);
  std::memset(mock, 0, sizeof(*mock));
  mock->datetime.day = 1u;
  mock->datetime.month = 1u;
  mock->datetime.year = 2000u;
  mock->datetime.clock_integrity = true;
  if (config->chip == HAL_RTC_CHIP_INTERNAL) {
    const hal_rtc_clock_source_t requested = config->bus.internal.clock_source;
    if (requested != HAL_RTC_CLOCK_SOURCE_AUTO &&
        requested != HAL_RTC_CLOCK_SOURCE_LSE &&
        requested != HAL_RTC_CLOCK_SOURCE_LSI &&
        requested != HAL_RTC_CLOCK_SOURCE_AON) {
      return HAL_EUNSUPPORTED;
    }
    mock->clock_source = requested == HAL_RTC_CLOCK_SOURCE_AUTO
                             ? HAL_RTC_CLOCK_SOURCE_LSE
                             : requested;
  } else {
    mock->clock_source = HAL_RTC_CLOCK_SOURCE_EXTERNAL;
  }
  return HAL_OK;
}

static void mock_deinitialize(void *context) {
  if (context != nullptr) {
    std::memset(context, 0, sizeof(jh_rtc_mock_context_t));
  }
}

static hal_status_t mock_get_datetime(void *context,
                                      hal_rtc_datetime_t *out_datetime) {
  if (context == nullptr || out_datetime == nullptr) {
    return HAL_EINVAL;
  }
  *out_datetime = mock_context(context)->datetime;
  return HAL_OK;
}

static hal_status_t mock_set_datetime(void *context,
                                      const hal_rtc_datetime_t *datetime) {
  if (context == nullptr || datetime == nullptr) {
    return HAL_EINVAL;
  }

  jh_rtc_mock_context_t *mock = mock_context(context);
  const bool clock_integrity = mock->datetime.clock_integrity;
  mock->datetime = *datetime;
  mock->datetime.clock_integrity = clock_integrity;
  return HAL_OK;
}

static hal_status_t mock_get_clock_integrity(void *context, bool *out_ok) {
  if (context == nullptr || out_ok == nullptr) {
    return HAL_EINVAL;
  }
  *out_ok = mock_context(context)->datetime.clock_integrity;
  return HAL_OK;
}

static hal_status_t mock_get_clock_source(void *context,
                                          hal_rtc_clock_source_t *out_source) {
  if (context == nullptr || out_source == nullptr) {
    return HAL_EINVAL;
  }
  *out_source = mock_context(context)->clock_source;
  return HAL_OK;
}

static hal_status_t mock_set_interrupt_enable(void *context, uint8_t irq_mask) {
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  jh_rtc_mock_context_t *mock = mock_context(context);
  mock->irq_mask = mock->clock_source == HAL_RTC_CLOCK_SOURCE_EXTERNAL
                       ? (uint8_t)(irq_mask & ~HAL_RTC_IRQ_WAKEUP)
                       : irq_mask;
  return HAL_OK;
}

static hal_status_t mock_get_interrupt_enable(void *context,
                                              uint8_t *out_irq_mask) {
  if (context == nullptr || out_irq_mask == nullptr) {
    return HAL_EINVAL;
  }
  *out_irq_mask = mock_context(context)->irq_mask;
  return HAL_OK;
}

static hal_status_t mock_get_and_clear_flags(void *context,
                                             uint8_t *out_flags) {
  if (context == nullptr || out_flags == nullptr) {
    return HAL_EINVAL;
  }

  jh_rtc_mock_context_t *mock = mock_context(context);
  *out_flags = mock->flags;
  mock->flags = 0u;
  mock->wakeup.pending = false;
  return HAL_OK;
}

static hal_status_t mock_get_temperature(void *context,
                                         float *out_temperature_c) {
  if (context == nullptr || out_temperature_c == nullptr) {
    return HAL_EINVAL;
  }
  return HAL_EUNSUPPORTED;
}

static hal_status_t mock_set_clkout_mode(void *context,
                                         hal_rtc_clkout_mode_t mode) {
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  mock_context(context)->clkout_mode = mode;
  return HAL_OK;
}

static hal_status_t mock_get_clkout_mode(void *context,
                                         hal_rtc_clkout_mode_t *out_mode) {
  if (context == nullptr || out_mode == nullptr) {
    return HAL_EINVAL;
  }
  *out_mode = mock_context(context)->clkout_mode;
  return HAL_OK;
}

static hal_status_t mock_set_timer(void *context,
                                   hal_rtc_timer_clock_t timer_clock,
                                   uint8_t count) {
  if (context == nullptr) {
    return HAL_EINVAL;
  }

  jh_rtc_mock_context_t *mock = mock_context(context);
  mock->timer_clock = timer_clock;
  mock->timer_count = count;
  return HAL_OK;
}

static hal_status_t mock_get_timer(void *context,
                                   hal_rtc_timer_clock_t *out_timer_clock,
                                   uint8_t *out_count) {
  if (context == nullptr || out_timer_clock == nullptr ||
      out_count == nullptr) {
    return HAL_EINVAL;
  }

  const jh_rtc_mock_context_t *mock = mock_context(context);
  *out_timer_clock = mock->timer_clock;
  *out_count = mock->timer_count;
  return HAL_OK;
}

static hal_status_t mock_set_alarm(void *context,
                                   const hal_rtc_alarm_t *alarm) {
  if (context == nullptr || alarm == nullptr) {
    return HAL_EINVAL;
  }
  mock_context(context)->alarm = *alarm;
  return HAL_OK;
}

static hal_status_t mock_get_alarm(void *context, hal_rtc_alarm_t *out_alarm) {
  if (context == nullptr || out_alarm == nullptr) {
    return HAL_EINVAL;
  }
  *out_alarm = mock_context(context)->alarm;
  return HAL_OK;
}

static hal_status_t mock_wakeup_arm(void *context, uint64_t timeout_us,
                                    uint32_t flags) {
  if (context == nullptr || timeout_us == 0u) {
    return HAL_EINVAL;
  }
  if (mock_context(context)->clock_source == HAL_RTC_CLOCK_SOURCE_EXTERNAL) {
    return HAL_EUNSUPPORTED;
  }

  jh_rtc_mock_context_t *mock = mock_context(context);
  mock->wakeup = {
      true, false, timeout_us, timeout_us, 1u, flags,
  };
  mock->irq_mask |= HAL_RTC_IRQ_WAKEUP;
  return HAL_OK;
}

static hal_status_t mock_wakeup_cancel(void *context) {
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  jh_rtc_mock_context_t *mock = mock_context(context);
  mock->wakeup = {};
  mock->flags &= (uint8_t)~HAL_RTC_FLAG_WAKEUP;
  mock->irq_mask &= (uint8_t)~HAL_RTC_IRQ_WAKEUP;
  return mock->clock_source == HAL_RTC_CLOCK_SOURCE_EXTERNAL ? HAL_EUNSUPPORTED
                                                             : HAL_OK;
}

static hal_status_t mock_wakeup_get_state(void *context,
                                          hal_rtc_wakeup_state_t *out_state) {
  if (context == nullptr || out_state == nullptr) {
    return HAL_EINVAL;
  }
  const jh_rtc_mock_context_t *mock = mock_context(context);
  if (mock->clock_source == HAL_RTC_CLOCK_SOURCE_EXTERNAL) {
    return HAL_EUNSUPPORTED;
  }
  *out_state = mock->wakeup;
  return HAL_OK;
}

static const jh_rtc_provider_ops_t s_pcf8563_mock_provider = {
    JH_RTC_PROVIDER_BUS_I2C,
    HAL_RTC_PCF8563_DEFAULT_I2C_ADDR,
    false,
    mock_initialize,
    mock_deinitialize,
    mock_get_datetime,
    mock_set_datetime,
    mock_get_clock_integrity,
    mock_get_clock_source,
    mock_set_interrupt_enable,
    mock_get_interrupt_enable,
    mock_get_and_clear_flags,
    mock_get_temperature,
    mock_set_clkout_mode,
    mock_get_clkout_mode,
    mock_set_timer,
    mock_get_timer,
    mock_set_alarm,
    mock_get_alarm,
    mock_wakeup_arm,
    mock_wakeup_cancel,
    mock_wakeup_get_state,
};

static const jh_rtc_provider_ops_t s_ds3231_mock_provider = {
    JH_RTC_PROVIDER_BUS_I2C,
    HAL_RTC_DS3231_DEFAULT_I2C_ADDR,
    true,
    mock_initialize,
    mock_deinitialize,
    mock_get_datetime,
    mock_set_datetime,
    mock_get_clock_integrity,
    mock_get_clock_source,
    mock_set_interrupt_enable,
    mock_get_interrupt_enable,
    mock_get_and_clear_flags,
    mock_get_temperature,
    mock_set_clkout_mode,
    mock_get_clkout_mode,
    mock_set_timer,
    mock_get_timer,
    mock_set_alarm,
    mock_get_alarm,
    mock_wakeup_arm,
    mock_wakeup_cancel,
    mock_wakeup_get_state,
};

#ifdef HAL_ENABLE_INTERNAL_RTC
static const jh_rtc_provider_ops_t s_internal_mock_provider = {
    JH_RTC_PROVIDER_BUS_INTERNAL,
    0u,
    false,
    mock_initialize,
    mock_deinitialize,
    mock_get_datetime,
    mock_set_datetime,
    mock_get_clock_integrity,
    mock_get_clock_source,
    mock_set_interrupt_enable,
    mock_get_interrupt_enable,
    mock_get_and_clear_flags,
    mock_get_temperature,
    mock_set_clkout_mode,
    mock_get_clkout_mode,
    mock_set_timer,
    mock_get_timer,
    mock_set_alarm,
    mock_get_alarm,
    mock_wakeup_arm,
    mock_wakeup_cancel,
    mock_wakeup_get_state,
};
#endif

const jh_rtc_provider_ops_t *jh_rtc_provider_get_ops(hal_rtc_chip_t chip) {
  switch (chip) {
  case HAL_RTC_CHIP_PCF8563:
    return &s_pcf8563_mock_provider;
  case HAL_RTC_CHIP_DS3231:
    return &s_ds3231_mock_provider;
#ifdef HAL_ENABLE_INTERNAL_RTC
  case HAL_RTC_CHIP_INTERNAL:
    return &s_internal_mock_provider;
#endif
  default:
    return nullptr;
  }
}

hal_status_t
jh_rtc_mock_provider_set_datetime(void *context,
                                  const hal_rtc_datetime_t *datetime) {
  return mock_set_datetime(context, datetime);
}

hal_status_t jh_rtc_mock_provider_set_clock_integrity(void *context, bool ok) {
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  mock_context(context)->datetime.clock_integrity = ok;
  return HAL_OK;
}

hal_status_t jh_rtc_mock_provider_set_flags(void *context, uint8_t flags) {
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  mock_context(context)->flags =
      (uint8_t)(flags & (HAL_RTC_FLAG_ALARM | HAL_RTC_FLAG_TIMER |
                         HAL_RTC_FLAG_WAKEUP));
  return HAL_OK;
}

hal_status_t jh_rtc_mock_provider_fire_wakeup(void *context) {
  if (context == nullptr) {
    return HAL_EINVAL;
  }
  jh_rtc_mock_context_t *mock = mock_context(context);
  if (!mock->wakeup.armed) {
    return HAL_ESTATE;
  }
  mock->wakeup.armed = false;
  mock->wakeup.pending = true;
  mock->flags |= HAL_RTC_FLAG_WAKEUP;
  return HAL_OK;
}

#endif /* HAL_ENABLE_RTC */
#endif /* HAL_TARGET_IS_MOCK */
