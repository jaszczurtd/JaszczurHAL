#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/i2c/hal_i2c.h>
#include <hal/power/hal_power.h>
#include <hal/rtc/hal_rtc.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

#include <stdbool.h>
#include <stdint.h>

#if HAL_TARGET_IS_RP
#define EXAMPLE_I2C_SDA 4u
#define EXAMPLE_I2C_SCL 5u
#define INTERNAL_RTC_NAME "RP AON"
#else
/* STM32 pin id = port * 16 + pin: PB9/PB8. */
#define EXAMPLE_I2C_SDA 25u
#define EXAMPLE_I2C_SCL 24u
#define INTERNAL_RTC_NAME "STM32 internal"
#endif

static hal_rtc_t s_pcf8563 = NULL;
static hal_rtc_t s_ds3231 = NULL;
static hal_rtc_t s_internal = NULL;
static bool s_power_exercised = false;
static bool s_resumed_from_power_down = false;
static const char *s_power_transition_name = "power";
static uint32_t s_last_report_ms = 0u;
static const hal_rtc_datetime_t s_seed_datetime = {
    .second = 50u,
    .minute = 34u,
    .hour = 12u,
    .day = 20u,
    .weekday = 4u,
    .month = 8u,
    .year = 2026u,
    .clock_integrity = true,
};

#ifndef HAL_EXAMPLE_RTC_POWER_DOWN_TEST
#define HAL_EXAMPLE_RTC_POWER_DOWN_TEST 0
#endif

static const char *clock_source_name(hal_rtc_clock_source_t source) {
  switch (source) {
  case HAL_RTC_CLOCK_SOURCE_EXTERNAL:
    return "external";
  case HAL_RTC_CLOCK_SOURCE_LSE:
    return "LSE";
  case HAL_RTC_CLOCK_SOURCE_LSI:
    return "LSI";
  case HAL_RTC_CLOCK_SOURCE_HSE_DIV32:
    return "HSE/32";
  case HAL_RTC_CLOCK_SOURCE_AON:
    return "AON";
  default:
    return "auto/unknown";
  }
}

static void print_datetime(const char *name, hal_rtc_t rtc) {
  if (rtc == NULL) {
    return;
  }

  hal_rtc_datetime_t value = {0};
  const hal_status_t status = hal_rtc_get_datetime_ex(rtc, &value);
  if (status != HAL_OK) {
    derr("%s datetime read failed: %s", name, hal_status_to_string(status));
    return;
  }

  deb("%s %04u-%02u-%02u %02u:%02u:%02u integrity=%u", name,
      (unsigned)value.year, (unsigned)value.month, (unsigned)value.day,
      (unsigned)value.hour, (unsigned)value.minute, (unsigned)value.second,
      value.clock_integrity ? 1u : 0u);
}

static void exercise_epoch(const char *name, hal_rtc_t rtc) {
  uint64_t epoch = 0u;
  hal_status_t status = hal_rtc_get_epoch_ex(rtc, &epoch);
  if (status != HAL_OK) {
    derr("%s epoch read failed: %s", name, hal_status_to_string(status));
    return;
  }

  status = hal_rtc_set_epoch_ex(rtc, epoch);
  if (status != HAL_OK) {
    derr("%s epoch same-value write failed: %s", name,
         hal_status_to_string(status));
    return;
  }

  uint64_t readback = 0u;
  status = hal_rtc_get_epoch_ex(rtc, &readback);
  if (status == HAL_OK) {
    deb("%s epoch=%lu same-value readback=%lu", name, (unsigned long)epoch,
        (unsigned long)readback);
  } else {
    derr("%s epoch readback failed: %s", name, hal_status_to_string(status));
  }
}

static void exercise_alarm(const char *name, hal_rtc_t rtc) {
  hal_rtc_datetime_t now = {0};
  hal_status_t status = hal_rtc_get_datetime_ex(rtc, &now);
  if (status != HAL_OK) {
    derr("%s alarm time read failed: %s", name, hal_status_to_string(status));
    return;
  }

  const hal_rtc_alarm_t requested = {
      .minute_enabled = true,
      .minute = (uint8_t)((now.minute + 1u) % 60u),
  };
  status = hal_rtc_set_alarm_ex(rtc, &requested);
  if (status != HAL_OK) {
    derr("%s alarm write failed: %s", name, hal_status_to_string(status));
    return;
  }

  hal_rtc_alarm_t readback = {0};
  status = hal_rtc_get_alarm_ex(rtc, &readback);
  if (status == HAL_OK) {
    deb("%s alarm minute_enabled=%u minute=%u", name,
        readback.minute_enabled ? 1u : 0u, (unsigned)readback.minute);
  } else {
    derr("%s alarm readback failed: %s", name, hal_status_to_string(status));
  }
}

static void exercise_clkout(const char *name, hal_rtc_t rtc) {
  hal_status_t status =
      hal_rtc_set_clkout_mode_ex(rtc, HAL_RTC_CLKOUT_DISABLED);
  if (status != HAL_OK) {
    derr("%s CLKOUT write failed: %s", name, hal_status_to_string(status));
    return;
  }

  hal_rtc_clkout_mode_t readback = HAL_RTC_CLKOUT_DISABLED;
  status = hal_rtc_get_clkout_mode_ex(rtc, &readback);
  if (status == HAL_OK) {
    deb("%s CLKOUT mode=%u", name, (unsigned)readback);
  } else {
    derr("%s CLKOUT readback failed: %s", name, hal_status_to_string(status));
  }
}

static void exercise_pcf8563_timer(hal_rtc_t rtc) {
  hal_status_t status = hal_rtc_set_timer_ex(rtc, HAL_RTC_TIMER_1_HZ, 5u);
  if (status != HAL_OK) {
    derr("PCF8563 timer write failed: %s", hal_status_to_string(status));
    return;
  }

  hal_rtc_timer_clock_t clock = HAL_RTC_TIMER_DISABLED;
  uint8_t count = 0u;
  status = hal_rtc_get_timer_ex(rtc, &clock, &count);
  if (status == HAL_OK) {
    deb("PCF8563 timer clock=%u count=%u", (unsigned)clock, (unsigned)count);
  } else {
    derr("PCF8563 timer readback failed: %s", hal_status_to_string(status));
  }
}

static hal_status_t seed_datetime(const char *name, hal_rtc_t rtc) {
  const hal_status_t status = hal_rtc_set_datetime_ex(rtc, &s_seed_datetime);
  if (status == HAL_OK) {
    deb("%s seeded 2026-08-20 12:34:50", name);
  } else {
    derr("%s seed failed: %s", name, hal_status_to_string(status));
  }
  return status;
}

static bool ensure_external_datetime(const char *name, hal_rtc_t rtc) {
  hal_rtc_datetime_t value = {0};
  const hal_status_t read_status = hal_rtc_get_datetime_ex(rtc, &value);
  if (read_status == HAL_OK && value.clock_integrity) {
    return true;
  }

  if (read_status == HAL_OK) {
    deb("%s clock integrity lost; seeding test time", name);
  } else {
    deb("%s calendar unavailable (%s); seeding test time", name,
        hal_status_to_string(read_status));
  }
  if (seed_datetime(name, rtc) != HAL_OK) {
    return false;
  }

  const hal_status_t verify_status = hal_rtc_get_datetime_ex(rtc, &value);
  if (verify_status != HAL_OK) {
    derr("%s seed readback failed: %s", name,
         hal_status_to_string(verify_status));
    return false;
  }
  deb("%s seed readback integrity=%u", name, value.clock_integrity ? 1u : 0u);
  return true;
}

static hal_rtc_t init_rtc(hal_rtc_chip_t chip, uint8_t address,
                          const char *name) {
  hal_rtc_config_t config = {.chip = chip,
                             .bus = {.i2c = {.sda_pin = EXAMPLE_I2C_SDA,
                                             .scl_pin = EXAMPLE_I2C_SCL,
                                             .clock_hz = HAL_I2C_CLOCK_FAST_HZ,
                                             .i2c_bus = 0u,
                                             .i2c_addr = address}}};
  hal_rtc_t rtc = NULL;
  const hal_status_t status = hal_rtc_init_ex(&config, &rtc);
  if (status != HAL_OK) {
    derr("%s not detected: %s", name, hal_status_to_string(status));
    return NULL;
  }

  if (!ensure_external_datetime(name, rtc)) {
    hal_rtc_deinit(rtc);
    return NULL;
  }
  exercise_epoch(name, rtc);
  exercise_alarm(name, rtc);
  exercise_clkout(name, rtc);
  deb("%s ready", name);
  return rtc;
}

static hal_rtc_t init_internal_rtc(void) {
  const hal_rtc_config_t config = {
      .chip = HAL_RTC_CHIP_INTERNAL,
      .bus = {.internal = {.clock_source = HAL_RTC_CLOCK_SOURCE_AUTO}},
  };
  hal_rtc_t rtc = NULL;
  hal_status_t status = hal_rtc_init_ex(&config, &rtc);
  if (status != HAL_OK) {
    derr("%s RTC init failed: %s", INTERNAL_RTC_NAME,
         hal_status_to_string(status));
    return NULL;
  }

  hal_rtc_clock_source_t source = HAL_RTC_CLOCK_SOURCE_AUTO;
  bool integrity = false;
  status = hal_rtc_get_clock_source_ex(rtc, &source);
  if (status == HAL_OK) {
    status = hal_rtc_get_clock_integrity_ex(rtc, &integrity);
  }
  if (status != HAL_OK) {
    derr("%s RTC diagnostics failed: %s", INTERNAL_RTC_NAME,
         hal_status_to_string(status));
    hal_rtc_deinit(rtc);
    return NULL;
  }
  deb("%s source=%s integrity=%u", INTERNAL_RTC_NAME, clock_source_name(source),
      integrity ? 1u : 0u);

  if (!integrity) {
    status = seed_datetime(INTERNAL_RTC_NAME, rtc);
    if (status != HAL_OK) {
      hal_rtc_deinit(rtc);
      return NULL;
    }
  } else {
    deb("%s retained time", INTERNAL_RTC_NAME);
  }

  exercise_epoch(INTERNAL_RTC_NAME, rtc);
#if HAL_TARGET_IS_STM32G474
  exercise_clkout(INTERNAL_RTC_NAME, rtc);
#endif
  deb("%s ready", INTERNAL_RTC_NAME);
  return rtc;
}

static const char *wake_reason_name(hal_power_wake_reason_t reason) {
  switch (reason) {
  case HAL_POWER_WAKE_REASON_RTC:
    return "RTC";
  case HAL_POWER_WAKE_REASON_INTERRUPT:
    return "interrupt";
  default:
    return "unknown";
  }
}

static hal_status_t prepare_power(hal_power_state_t state, void *user_data) {
  (void)user_data;
  deb("%s entering state=%u", s_power_transition_name, (unsigned)state);
  return HAL_OK;
}

static void resume_power(const hal_power_result_t *result, void *user_data) {
  (void)user_data;
  deb("%s resumed reason=%s", s_power_transition_name,
      wake_reason_name(result->reason));
}

static void exercise_power_state(const char *name, hal_power_state_t state,
                                 hal_power_policy_t policy,
                                 uint64_t timeout_us) {
  hal_power_capabilities_t capabilities = {0};
  hal_status_t status = hal_power_get_capabilities_ex(state, &capabilities);
  const uint32_t policy_mask = policy == HAL_POWER_POLICY_FAST_WAKE
                                   ? HAL_POWER_POLICY_MASK_FAST_WAKE
                                   : HAL_POWER_POLICY_MASK_LOWEST_POWER;
  if (status != HAL_OK || !capabilities.supported ||
      (capabilities.supported_policies & policy_mask) == 0u ||
      (capabilities.wake_sources & HAL_POWER_WAKE_SOURCE_RTC) == 0u) {
    deb("%s unsupported on this runtime", name);
    return;
  }

  const hal_power_request_t request = {
      .state = state,
      .policy = policy,
      .wake_sources = HAL_POWER_WAKE_SOURCE_RTC,
      .rtc = s_internal,
      .rtc_timeout_us = timeout_us,
      .prepare = prepare_power,
      .resume = resume_power,
      .user_data = NULL,
  };
  s_power_transition_name = name;
  const uint64_t before_us = hal_micros64();
  hal_power_result_t result = {0};
  status = hal_power_enter_ex(&request, &result);
  if (status != HAL_OK) {
    derr("%s failed: %s", name, hal_status_to_string(status));
    return;
  }

  const uint64_t monotonic_elapsed_us = hal_micros64() - before_us;
  deb("%s result reason=%s elapsed=%lu ms monotonic=%lu ms reset=%u", name,
      wake_reason_name(result.reason),
      (unsigned long)(result.elapsed_us / 1000u),
      (unsigned long)(monotonic_elapsed_us / 1000u),
      result.resumed_from_reset ? 1u : 0u);
}

static void exercise_power_states(void) {
  if (s_internal == NULL || s_power_exercised) {
    return;
  }
  s_power_exercised = true;
  exercise_power_state("sleep", HAL_POWER_STATE_SLEEP,
                       HAL_POWER_POLICY_FAST_WAKE, UINT64_C(2000000));
  exercise_power_state("deep-fast", HAL_POWER_STATE_DEEP_SLEEP,
                       HAL_POWER_POLICY_FAST_WAKE, UINT64_C(3000000));
  exercise_power_state("deep-lowest", HAL_POWER_STATE_DEEP_SLEEP,
                       HAL_POWER_POLICY_LOWEST_POWER, UINT64_C(4000000));
#if HAL_EXAMPLE_RTC_POWER_DOWN_TEST
  if (!s_resumed_from_power_down) {
    exercise_power_state("power-down", HAL_POWER_STATE_POWER_DOWN,
                         HAL_POWER_POLICY_LOWEST_POWER, UINT64_C(5000000));
  }
#endif
}

void app_start(void) {
  hal_debug_init_default();
  hal_serial_set_flush(true);
  deb("=== JaszczurHAL RTC backends ===");
  hal_power_result_t boot_wake = {0};
  if (hal_power_get_last_wake_ex(&boot_wake) == HAL_OK &&
      boot_wake.resumed_from_reset) {
    s_resumed_from_power_down = true;
    s_power_exercised = true;
    deb("power-down wake reason=%s elapsed=%lu ms",
        wake_reason_name(boot_wake.reason),
        (unsigned long)(boot_wake.elapsed_us / 1000u));
  }
  (void)hal_i2c_init(EXAMPLE_I2C_SDA, EXAMPLE_I2C_SCL, HAL_I2C_CLOCK_FAST_HZ);

  s_internal = init_internal_rtc();

  s_pcf8563 = init_rtc(HAL_RTC_CHIP_PCF8563, HAL_RTC_PCF8563_DEFAULT_I2C_ADDR,
                       "PCF8563");
  s_ds3231 =
      init_rtc(HAL_RTC_CHIP_DS3231, HAL_RTC_DS3231_DEFAULT_I2C_ADDR, "DS3231");
  if (s_pcf8563 != NULL) {
    exercise_pcf8563_timer(s_pcf8563);
  }
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  if ((uint32_t)(now - s_last_report_ms) < 1000u) {
    hal_delay_ms(10u);
    return;
  }
  s_last_report_ms = now;

  exercise_power_states();

  print_datetime("PCF8563", s_pcf8563);
  print_datetime("DS3231", s_ds3231);
  print_datetime(INTERNAL_RTC_NAME, s_internal);
  if (s_ds3231 != NULL) {
    float temperature_c = 0.0f;
    const hal_status_t status =
        hal_rtc_get_temperature_ex(s_ds3231, &temperature_c);
    if (status == HAL_OK) {
      deb("DS3231 temperature=%.2f C", (double)temperature_c);
    } else {
      derr("DS3231 temperature read failed: %s", hal_status_to_string(status));
    }
  }
}
