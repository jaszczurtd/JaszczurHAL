#include "hal/i2c/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/rtc/hal_rtc.h"
#include "utils/unity.h"

#ifdef HAL_ENABLE_RTC

static hal_rtc_t s_rtc = nullptr;

static hal_rtc_config_t default_cfg(void) {
  hal_rtc_config_t cfg = {};
  cfg.chip = HAL_RTC_CHIP_PCF8563;
  cfg.bus.i2c.sda_pin = 4;
  cfg.bus.i2c.scl_pin = 5;
  cfg.bus.i2c.clock_hz = HAL_I2C_CLOCK_FAST_HZ;
  cfg.bus.i2c.i2c_bus = 0;
  cfg.bus.i2c.i2c_addr = 0;
  return cfg;
}

static hal_rtc_alarm_t valid_alarm(void) {
  hal_rtc_alarm_t alarm = {};
  alarm.minute_enabled = true;
  alarm.minute = 15;
  alarm.hour_enabled = true;
  alarm.hour = 6;
  alarm.day_enabled = true;
  alarm.day = 24;
  alarm.weekday_enabled = false;
  alarm.weekday = 0;
  return alarm;
}

void setUp(void) {
  hal_rtc_config_t cfg = default_cfg();
  s_rtc = hal_rtc_init(&cfg);
}

void tearDown(void) {
  hal_rtc_deinit(s_rtc);
  s_rtc = nullptr;
}

void test_init_returns_handle(void) { TEST_ASSERT_NOT_NULL(s_rtc); }

void test_init_ds3231_backend_returns_handle(void) {
  hal_rtc_config_t cfg = default_cfg();
  cfg.chip = HAL_RTC_CHIP_DS3231;

  hal_rtc_t rtc = hal_rtc_init(&cfg);
  TEST_ASSERT_NOT_NULL(rtc);

  hal_rtc_datetime_t in = {};
  in.second = 30;
  in.minute = 15;
  in.hour = 8;
  in.day = 1;
  in.weekday = 1;
  in.month = 6;
  in.year = 2026;

  TEST_ASSERT_TRUE(hal_rtc_set_datetime(rtc, &in));

  hal_rtc_datetime_t out = {};
  TEST_ASSERT_TRUE(hal_rtc_get_datetime(rtc, &out));
  TEST_ASSERT_EQUAL_UINT8(in.second, out.second);
  TEST_ASSERT_EQUAL_UINT8(in.minute, out.minute);
  TEST_ASSERT_EQUAL_UINT8(in.hour, out.hour);
  TEST_ASSERT_EQUAL_UINT8(in.day, out.day);
  TEST_ASSERT_EQUAL_UINT8(in.month, out.month);
  TEST_ASSERT_EQUAL_UINT16(in.year, out.year);

  float temperature_c = 0.0f;
  TEST_ASSERT_FALSE(hal_rtc_get_temperature(rtc, &temperature_c));

  hal_rtc_deinit(rtc);
}

void test_clock_source_reports_external_provider(void) {
  hal_rtc_clock_source_t source = HAL_RTC_CLOCK_SOURCE_AUTO;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_get_clock_source_ex(s_rtc, &source));
  TEST_ASSERT_EQUAL_INT(HAL_RTC_CLOCK_SOURCE_EXTERNAL, source);
}

void test_internal_backend_skips_i2c_config_and_resolves_clock(void) {
  hal_rtc_config_t cfg = {};
  cfg.chip = HAL_RTC_CHIP_INTERNAL;
  cfg.bus.internal.clock_source = HAL_RTC_CLOCK_SOURCE_AUTO;

  hal_rtc_t rtc = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_init_ex(&cfg, &rtc));
  TEST_ASSERT_NOT_NULL(rtc);

  hal_rtc_clock_source_t source = HAL_RTC_CLOCK_SOURCE_AUTO;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_get_clock_source_ex(rtc, &source));
  TEST_ASSERT_EQUAL_INT(HAL_RTC_CLOCK_SOURCE_LSE, source);
  hal_rtc_deinit(rtc);

  cfg.bus.internal.clock_source = HAL_RTC_CLOCK_SOURCE_LSI;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_init_ex(&cfg, &rtc));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_get_clock_source_ex(rtc, &source));
  TEST_ASSERT_EQUAL_INT(HAL_RTC_CLOCK_SOURCE_LSI, source);
  hal_rtc_deinit(rtc);
}

void test_internal_backend_rejects_unsupported_clock_source(void) {
  hal_rtc_config_t cfg = {};
  cfg.chip = HAL_RTC_CHIP_INTERNAL;
  cfg.bus.internal.clock_source = HAL_RTC_CLOCK_SOURCE_HSE_DIV32;

  hal_rtc_t rtc = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_rtc_init_ex(&cfg, &rtc));
  TEST_ASSERT_NULL(rtc);
}

void test_internal_mock_accepts_aon_clock_source(void) {
  hal_rtc_config_t cfg = {};
  cfg.chip = HAL_RTC_CHIP_INTERNAL;
  cfg.bus.internal.clock_source = HAL_RTC_CLOCK_SOURCE_AON;

  hal_rtc_t rtc = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_init_ex(&cfg, &rtc));
  hal_rtc_clock_source_t source = HAL_RTC_CLOCK_SOURCE_AUTO;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_get_clock_source_ex(rtc, &source));
  TEST_ASSERT_EQUAL_INT(HAL_RTC_CLOCK_SOURCE_AON, source);
  hal_rtc_deinit(rtc);
}

void test_relative_wakeup_is_internal_and_one_shot(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_rtc_wakeup_arm_ex(s_rtc, 1000000u, 0u));

  hal_rtc_config_t cfg = {};
  cfg.chip = HAL_RTC_CHIP_INTERNAL;
  cfg.bus.internal.clock_source = HAL_RTC_CLOCK_SOURCE_AUTO;
  hal_rtc_t rtc = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_init_ex(&cfg, &rtc));

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_wakeup_arm_ex(rtc, 0u, 0u));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_rtc_wakeup_arm_ex(rtc, 2500001u, UINT32_C(0x80000000)));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_rtc_wakeup_arm_ex(rtc, 2500001u, HAL_RTC_WAKEUP_LOW_POWER));

  hal_rtc_wakeup_state_t state = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_wakeup_get_state_ex(rtc, &state));
  TEST_ASSERT_TRUE(state.armed);
  TEST_ASSERT_FALSE(state.pending);
  TEST_ASSERT_EQUAL_UINT64(2500001u, state.requested_timeout_us);
  TEST_ASSERT_EQUAL_UINT64(2500001u, state.programmed_timeout_us);
  TEST_ASSERT_EQUAL_UINT64(1u, state.resolution_us);
  TEST_ASSERT_EQUAL_HEX32(HAL_RTC_WAKEUP_LOW_POWER, state.flags);

  uint8_t irq_mask = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_rtc_get_interrupt_enable_ex(rtc, &irq_mask));
  TEST_ASSERT_EQUAL_HEX8(HAL_RTC_IRQ_WAKEUP, irq_mask);

  hal_mock_rtc_fire_wakeup(rtc);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_wakeup_get_state_ex(rtc, &state));
  TEST_ASSERT_FALSE(state.armed);
  TEST_ASSERT_TRUE(state.pending);

  uint8_t flags = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_get_and_clear_flags_ex(rtc, &flags));
  TEST_ASSERT_EQUAL_HEX8(HAL_RTC_FLAG_WAKEUP, flags);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_wakeup_get_state_ex(rtc, &state));
  TEST_ASSERT_FALSE(state.pending);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_wakeup_cancel_ex(rtc));
  hal_rtc_deinit(rtc);
}

void test_relative_wakeup_rejects_invalid_arguments(void) {
  hal_rtc_wakeup_state_t state = {};
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_wakeup_arm_ex(nullptr, 1000u, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_wakeup_cancel_ex(nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_rtc_wakeup_get_state_ex(nullptr, &state));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_rtc_wakeup_get_state_ex(s_rtc, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_rtc_wakeup_get_state_ex(s_rtc, &state));
}

void test_init_rejects_unknown_backend(void) {
  hal_rtc_config_t cfg = default_cfg();
  cfg.chip = (hal_rtc_chip_t)255;

  hal_rtc_t rtc = hal_rtc_init(&cfg);
  TEST_ASSERT_NULL(rtc);
}

void test_default_datetime_is_readable(void) {
  hal_rtc_datetime_t dt = {};
  TEST_ASSERT_TRUE(hal_rtc_get_datetime(s_rtc, &dt));
  TEST_ASSERT_EQUAL_UINT8(0, dt.second);
  TEST_ASSERT_EQUAL_UINT8(0, dt.minute);
  TEST_ASSERT_EQUAL_UINT8(0, dt.hour);
  TEST_ASSERT_EQUAL_UINT8(1, dt.day);
  TEST_ASSERT_EQUAL_UINT8(1, dt.month);
  TEST_ASSERT_EQUAL_UINT16(2000, dt.year);
  TEST_ASSERT_TRUE(dt.clock_integrity);
}

void test_set_and_get_datetime_roundtrip(void) {
  hal_rtc_datetime_t in = {};
  in.second = 58;
  in.minute = 59;
  in.hour = 23;
  in.day = 31;
  in.weekday = 6;
  in.month = 12;
  in.year = 2099;

  TEST_ASSERT_TRUE(hal_rtc_set_datetime(s_rtc, &in));

  hal_rtc_datetime_t out = {};
  TEST_ASSERT_TRUE(hal_rtc_get_datetime(s_rtc, &out));
  TEST_ASSERT_EQUAL_UINT8(in.second, out.second);
  TEST_ASSERT_EQUAL_UINT8(in.minute, out.minute);
  TEST_ASSERT_EQUAL_UINT8(in.hour, out.hour);
  TEST_ASSERT_EQUAL_UINT8(in.day, out.day);
  TEST_ASSERT_EQUAL_UINT8(in.weekday, out.weekday);
  TEST_ASSERT_EQUAL_UINT8(in.month, out.month);
  TEST_ASSERT_EQUAL_UINT16(in.year, out.year);
}

void test_set_datetime_rejects_invalid_values(void) {
  hal_rtc_datetime_t bad = {};
  bad.second = 60;
  bad.minute = 0;
  bad.hour = 0;
  bad.day = 1;
  bad.weekday = 0;
  bad.month = 1;
  bad.year = 2026;

  TEST_ASSERT_FALSE(hal_rtc_set_datetime(s_rtc, &bad));

  bad.second = 0;
  bad.month = 0;
  TEST_ASSERT_FALSE(hal_rtc_set_datetime(s_rtc, &bad));

  bad.month = 1;
  bad.year = 2100;
  TEST_ASSERT_FALSE(hal_rtc_set_datetime(s_rtc, &bad));

  bad.year = 2026;
  bad.month = 4;
  bad.day = 31;
  TEST_ASSERT_FALSE(hal_rtc_set_datetime(s_rtc, &bad));

  bad.year = 2023;
  bad.month = 2;
  bad.day = 29;
  TEST_ASSERT_FALSE(hal_rtc_set_datetime(s_rtc, &bad));
}

void test_epoch_roundtrip_from_unix_epoch_start(void) {
  TEST_ASSERT_TRUE(hal_rtc_set_epoch(s_rtc, 0ull));

  hal_rtc_datetime_t dt = {};
  TEST_ASSERT_TRUE(hal_rtc_get_datetime(s_rtc, &dt));
  TEST_ASSERT_EQUAL_UINT16(1970, dt.year);
  TEST_ASSERT_EQUAL_UINT8(1, dt.month);
  TEST_ASSERT_EQUAL_UINT8(1, dt.day);
  TEST_ASSERT_EQUAL_UINT8(0, dt.hour);
  TEST_ASSERT_EQUAL_UINT8(0, dt.minute);
  TEST_ASSERT_EQUAL_UINT8(0, dt.second);
  TEST_ASSERT_EQUAL_UINT8(4, dt.weekday);

  uint64_t epoch = 123ull;
  TEST_ASSERT_TRUE(hal_rtc_get_epoch(s_rtc, &epoch));
  TEST_ASSERT_EQUAL_UINT64(0ull, epoch);
}

void test_epoch_leap_day_roundtrip(void) {
  const uint64_t leap_day_epoch = 951782400ull; /* 2000-02-29 00:00:00 UTC */

  TEST_ASSERT_TRUE(hal_rtc_set_epoch(s_rtc, leap_day_epoch));

  hal_rtc_datetime_t dt = {};
  TEST_ASSERT_TRUE(hal_rtc_get_datetime(s_rtc, &dt));
  TEST_ASSERT_EQUAL_UINT16(2000, dt.year);
  TEST_ASSERT_EQUAL_UINT8(2, dt.month);
  TEST_ASSERT_EQUAL_UINT8(29, dt.day);
  TEST_ASSERT_EQUAL_UINT8(0, dt.hour);
  TEST_ASSERT_EQUAL_UINT8(0, dt.minute);
  TEST_ASSERT_EQUAL_UINT8(0, dt.second);
  TEST_ASSERT_EQUAL_UINT8(2, dt.weekday);

  uint64_t epoch = 0ull;
  TEST_ASSERT_TRUE(hal_rtc_get_epoch(s_rtc, &epoch));
  TEST_ASSERT_EQUAL_UINT64(leap_day_epoch, epoch);
}

void test_get_epoch_from_datetime(void) {
  hal_rtc_datetime_t dt = {};
  dt.second = 0;
  dt.minute = 0;
  dt.hour = 0;
  dt.day = 29;
  dt.weekday = 2;
  dt.month = 2;
  dt.year = 2000;

  TEST_ASSERT_TRUE(hal_rtc_set_datetime(s_rtc, &dt));

  uint64_t epoch = 0ull;
  TEST_ASSERT_TRUE(hal_rtc_get_epoch(s_rtc, &epoch));
  TEST_ASSERT_EQUAL_UINT64(951782400ull, epoch);
}

void test_set_epoch_rejects_out_of_supported_range(void) {
  TEST_ASSERT_TRUE(hal_rtc_set_epoch(s_rtc, 4102444799ull));
  hal_rtc_datetime_t maximum = {};
  TEST_ASSERT_TRUE(hal_rtc_get_datetime(s_rtc, &maximum));
  TEST_ASSERT_EQUAL_UINT16(2099u, maximum.year);
  TEST_ASSERT_EQUAL_UINT8(12u, maximum.month);
  TEST_ASSERT_EQUAL_UINT8(31u, maximum.day);

  /* 2100-01-01 00:00:00 UTC */
  TEST_ASSERT_FALSE(hal_rtc_set_epoch(s_rtc, 4102444800ull));
  TEST_ASSERT_FALSE(hal_rtc_set_epoch(s_rtc, UINT64_MAX));
}

void test_get_epoch_rejects_pre_unix_datetime(void) {
  hal_rtc_datetime_t dt = {};
  dt.second = 59;
  dt.minute = 59;
  dt.hour = 23;
  dt.day = 31;
  dt.weekday = 3;
  dt.month = 12;
  dt.year = 1969;
  dt.clock_integrity = true;

  hal_mock_rtc_set_datetime(s_rtc, &dt);

  uint64_t epoch = 0ull;
  TEST_ASSERT_FALSE(hal_rtc_get_epoch(s_rtc, &epoch));
}

void test_clock_integrity_flag_can_be_injected(void) {
  bool ok = true;

  TEST_ASSERT_TRUE(hal_rtc_get_clock_integrity(s_rtc, &ok));
  TEST_ASSERT_TRUE(ok);

  hal_mock_rtc_set_clock_integrity(s_rtc, false);
  TEST_ASSERT_TRUE(hal_rtc_get_clock_integrity(s_rtc, &ok));
  TEST_ASSERT_FALSE(ok);

  hal_rtc_datetime_t dt = {};
  TEST_ASSERT_TRUE(hal_rtc_get_datetime(s_rtc, &dt));
  TEST_ASSERT_FALSE(dt.clock_integrity);
}

void test_mock_datetime_injection(void) {
  hal_rtc_datetime_t injected = {};
  injected.second = 12;
  injected.minute = 34;
  injected.hour = 5;
  injected.day = 24;
  injected.weekday = 0;
  injected.month = 5;
  injected.year = 2026;
  injected.clock_integrity = true;

  hal_mock_rtc_set_datetime(s_rtc, &injected);

  hal_rtc_datetime_t out = {};
  TEST_ASSERT_TRUE(hal_rtc_get_datetime(s_rtc, &out));
  TEST_ASSERT_EQUAL_UINT8(injected.second, out.second);
  TEST_ASSERT_EQUAL_UINT8(injected.minute, out.minute);
  TEST_ASSERT_EQUAL_UINT8(injected.hour, out.hour);
  TEST_ASSERT_EQUAL_UINT8(injected.day, out.day);
  TEST_ASSERT_EQUAL_UINT8(injected.weekday, out.weekday);
  TEST_ASSERT_EQUAL_UINT8(injected.month, out.month);
  TEST_ASSERT_EQUAL_UINT16(injected.year, out.year);
}

void test_interrupt_enable_roundtrip(void) {
  uint8_t irq = 0;

  TEST_ASSERT_TRUE(hal_rtc_set_interrupt_enable(s_rtc, HAL_RTC_IRQ_ALARM));
  TEST_ASSERT_TRUE(hal_rtc_get_interrupt_enable(s_rtc, &irq));
  TEST_ASSERT_EQUAL_UINT8(HAL_RTC_IRQ_ALARM, irq);

  TEST_ASSERT_TRUE(hal_rtc_set_interrupt_enable(s_rtc, 0xFF));
  TEST_ASSERT_TRUE(hal_rtc_get_interrupt_enable(s_rtc, &irq));
  TEST_ASSERT_EQUAL_UINT8((HAL_RTC_IRQ_ALARM | HAL_RTC_IRQ_TIMER), irq);
}

void test_get_and_clear_flags(void) {
  uint8_t flags = 0;
  hal_mock_rtc_set_flags(s_rtc, HAL_RTC_FLAG_ALARM | HAL_RTC_FLAG_TIMER);

  TEST_ASSERT_TRUE(hal_rtc_get_and_clear_flags(s_rtc, &flags));
  TEST_ASSERT_EQUAL_UINT8((HAL_RTC_FLAG_ALARM | HAL_RTC_FLAG_TIMER), flags);

  flags = 0xFF;
  TEST_ASSERT_TRUE(hal_rtc_get_and_clear_flags(s_rtc, &flags));
  TEST_ASSERT_EQUAL_UINT8(0, flags);
}

void test_clkout_roundtrip_and_validation(void) {
  const hal_rtc_clkout_mode_t modes[] = {
      HAL_RTC_CLKOUT_DISABLED, HAL_RTC_CLKOUT_1_HZ,     HAL_RTC_CLKOUT_32_HZ,
      HAL_RTC_CLKOUT_1024_HZ,  HAL_RTC_CLKOUT_32768_HZ,
  };

  for (unsigned i = 0; i < (sizeof(modes) / sizeof(modes[0])); ++i) {
    TEST_ASSERT_TRUE(hal_rtc_set_clkout_mode(s_rtc, modes[i]));
    hal_rtc_clkout_mode_t out = HAL_RTC_CLKOUT_DISABLED;
    TEST_ASSERT_TRUE(hal_rtc_get_clkout_mode(s_rtc, &out));
    TEST_ASSERT_EQUAL_INT((int)modes[i], (int)out);
  }

  TEST_ASSERT_FALSE(hal_rtc_set_clkout_mode(s_rtc, (hal_rtc_clkout_mode_t)99));
}

void test_timer_roundtrip_and_validation(void) {
  hal_rtc_timer_clock_t out_clock = HAL_RTC_TIMER_DISABLED;
  uint8_t out_count = 0;

  TEST_ASSERT_TRUE(hal_rtc_set_timer(s_rtc, HAL_RTC_TIMER_64_HZ, 123));
  TEST_ASSERT_TRUE(hal_rtc_get_timer(s_rtc, &out_clock, &out_count));
  TEST_ASSERT_EQUAL_INT((int)HAL_RTC_TIMER_64_HZ, (int)out_clock);
  TEST_ASSERT_EQUAL_UINT8(123, out_count);

  TEST_ASSERT_TRUE(hal_rtc_set_timer(s_rtc, HAL_RTC_TIMER_DISABLED, 0));
  TEST_ASSERT_TRUE(hal_rtc_get_timer(s_rtc, &out_clock, &out_count));
  TEST_ASSERT_EQUAL_INT((int)HAL_RTC_TIMER_DISABLED, (int)out_clock);
  TEST_ASSERT_EQUAL_UINT8(0, out_count);

  TEST_ASSERT_FALSE(hal_rtc_set_timer(s_rtc, (hal_rtc_timer_clock_t)77, 10));
}

void test_alarm_roundtrip_and_validation(void) {
  hal_rtc_alarm_t in = valid_alarm();
  hal_rtc_alarm_t out = {};

  TEST_ASSERT_TRUE(hal_rtc_set_alarm(s_rtc, &in));
  TEST_ASSERT_TRUE(hal_rtc_get_alarm(s_rtc, &out));

  TEST_ASSERT_EQUAL_UINT8(in.minute_enabled, out.minute_enabled);
  TEST_ASSERT_EQUAL_UINT8(in.minute, out.minute);
  TEST_ASSERT_EQUAL_UINT8(in.hour_enabled, out.hour_enabled);
  TEST_ASSERT_EQUAL_UINT8(in.hour, out.hour);
  TEST_ASSERT_EQUAL_UINT8(in.day_enabled, out.day_enabled);
  TEST_ASSERT_EQUAL_UINT8(in.day, out.day);
  TEST_ASSERT_EQUAL_UINT8(in.weekday_enabled, out.weekday_enabled);
  TEST_ASSERT_EQUAL_UINT8(in.weekday, out.weekday);

  hal_rtc_alarm_t bad = valid_alarm();
  bad.minute = 60;
  TEST_ASSERT_FALSE(hal_rtc_set_alarm(s_rtc, &bad));

  bad = valid_alarm();
  bad.hour = 24;
  TEST_ASSERT_FALSE(hal_rtc_set_alarm(s_rtc, &bad));

  bad = valid_alarm();
  bad.day = 0;
  TEST_ASSERT_FALSE(hal_rtc_set_alarm(s_rtc, &bad));

  bad = valid_alarm();
  bad.weekday_enabled = true;
  bad.weekday = 7;
  TEST_ASSERT_FALSE(hal_rtc_set_alarm(s_rtc, &bad));
}

void test_invalid_arguments_return_false(void) {
  hal_rtc_datetime_t dt = {};
  bool ok = true;
  uint8_t flags = 0;
  uint8_t irq = 0;
  hal_rtc_clkout_mode_t clkout = HAL_RTC_CLKOUT_DISABLED;
  hal_rtc_clock_source_t source = HAL_RTC_CLOCK_SOURCE_AUTO;
  hal_rtc_timer_clock_t timer_clock = HAL_RTC_TIMER_DISABLED;
  uint8_t timer_count = 0;
  hal_rtc_alarm_t alarm = valid_alarm();
  uint64_t epoch = 0ull;

  TEST_ASSERT_FALSE(hal_rtc_get_datetime(nullptr, &dt));
  TEST_ASSERT_FALSE(hal_rtc_get_datetime(s_rtc, nullptr));

  TEST_ASSERT_FALSE(hal_rtc_set_datetime(nullptr, &dt));
  TEST_ASSERT_FALSE(hal_rtc_set_datetime(s_rtc, nullptr));

  TEST_ASSERT_FALSE(hal_rtc_get_epoch(nullptr, &epoch));
  TEST_ASSERT_FALSE(hal_rtc_get_epoch(s_rtc, nullptr));
  TEST_ASSERT_FALSE(hal_rtc_set_epoch(nullptr, 0ull));

  TEST_ASSERT_FALSE(hal_rtc_get_clock_integrity(nullptr, &ok));
  TEST_ASSERT_FALSE(hal_rtc_get_clock_integrity(s_rtc, nullptr));
  TEST_ASSERT_FALSE(hal_rtc_get_clock_source(nullptr, &source));
  TEST_ASSERT_FALSE(hal_rtc_get_clock_source(s_rtc, nullptr));

  TEST_ASSERT_FALSE(hal_rtc_set_interrupt_enable(nullptr, HAL_RTC_IRQ_ALARM));
  TEST_ASSERT_FALSE(hal_rtc_get_interrupt_enable(nullptr, &irq));
  TEST_ASSERT_FALSE(hal_rtc_get_interrupt_enable(s_rtc, nullptr));

  TEST_ASSERT_FALSE(hal_rtc_get_and_clear_flags(nullptr, &flags));
  TEST_ASSERT_FALSE(hal_rtc_get_and_clear_flags(s_rtc, nullptr));

  TEST_ASSERT_FALSE(hal_rtc_set_clkout_mode(nullptr, HAL_RTC_CLKOUT_1_HZ));
  TEST_ASSERT_FALSE(hal_rtc_get_clkout_mode(nullptr, &clkout));
  TEST_ASSERT_FALSE(hal_rtc_get_clkout_mode(s_rtc, nullptr));

  TEST_ASSERT_FALSE(hal_rtc_set_timer(nullptr, HAL_RTC_TIMER_1_HZ, 1));
  TEST_ASSERT_FALSE(hal_rtc_get_timer(nullptr, &timer_clock, &timer_count));
  TEST_ASSERT_FALSE(hal_rtc_get_timer(s_rtc, nullptr, &timer_count));
  TEST_ASSERT_FALSE(hal_rtc_get_timer(s_rtc, &timer_clock, nullptr));

  TEST_ASSERT_FALSE(hal_rtc_set_alarm(nullptr, &alarm));
  TEST_ASSERT_FALSE(hal_rtc_set_alarm(s_rtc, nullptr));
  TEST_ASSERT_FALSE(hal_rtc_get_alarm(nullptr, &alarm));
  TEST_ASSERT_FALSE(hal_rtc_get_alarm(s_rtc, nullptr));
}

/* ---- Status-returning (_ex) API coverage ---- */

void test_ex_init_and_handle_status(void) {
  hal_rtc_config_t cfg = default_cfg();
  hal_rtc_t handle = nullptr;

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_init_ex(nullptr, &handle));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_init_ex(&cfg, nullptr));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_init_ex(&cfg, &handle));
  TEST_ASSERT_NOT_NULL(handle);
  hal_rtc_deinit(handle);
  hal_rtc_deinit(nullptr);

  hal_rtc_config_t bad = default_cfg();
  bad.chip = (hal_rtc_chip_t)99;
  hal_rtc_t bad_handle = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_rtc_init_ex(&bad, &bad_handle));
  TEST_ASSERT_NULL(bad_handle);

  bad = default_cfg();
  bad.bus.i2c.clock_hz = 0;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_init_ex(&bad, &bad_handle));
  TEST_ASSERT_NULL(bad_handle);

  bad = default_cfg();
  bad.chip = HAL_RTC_CHIP_DS3231;
  bad.bus.i2c.i2c_addr = HAL_RTC_PCF8563_DEFAULT_I2C_ADDR;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_init_ex(&bad, &bad_handle));
  TEST_ASSERT_NULL(bad_handle);

  bad = default_cfg();
  bad.bus.i2c.i2c_bus = 2;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_init_ex(&bad, &bad_handle));
  TEST_ASSERT_NULL(bad_handle);
}

void test_ex_deinit_invalidates_handle_and_releases_slot(void) {
  hal_rtc_config_t cfg = default_cfg();
  hal_rtc_t handle = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_init_ex(&cfg, &handle));
  TEST_ASSERT_NOT_NULL(handle);

  hal_rtc_deinit(handle);

  hal_rtc_datetime_t datetime = {};
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_get_datetime_ex(handle, &datetime));
  hal_rtc_deinit(handle);

  hal_rtc_t replacement = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_init_ex(&cfg, &replacement));
  TEST_ASSERT_NOT_NULL(replacement);
  hal_rtc_deinit(replacement);
}

void test_ex_init_reports_pool_exhaustion(void) {
  hal_rtc_config_t cfg = default_cfg();
  hal_rtc_t handles[HAL_RTC_MAX_INSTANCES - 1] = {};

  for (int i = 0; i < HAL_RTC_MAX_INSTANCES - 1; ++i) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_init_ex(&cfg, &handles[i]));
    TEST_ASSERT_NOT_NULL(handles[i]);
  }

  hal_rtc_t extra = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_rtc_init_ex(&cfg, &extra));
  TEST_ASSERT_NULL(extra);

  for (int i = 0; i < HAL_RTC_MAX_INSTANCES - 1; ++i) {
    hal_rtc_deinit(handles[i]);
  }
}

void test_ex_datetime_roundtrip_and_validation(void) {
  hal_rtc_datetime_t in = {};
  in.second = 30;
  in.minute = 15;
  in.hour = 8;
  in.day = 1;
  in.weekday = 1;
  in.month = 6;
  in.year = 2026;

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_set_datetime_ex(s_rtc, &in));
  hal_rtc_datetime_t out = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_get_datetime_ex(s_rtc, &out));
  TEST_ASSERT_EQUAL_UINT8(in.minute, out.minute);
  TEST_ASSERT_EQUAL_UINT16(in.year, out.year);

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_set_datetime_ex(nullptr, &in));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_get_datetime_ex(s_rtc, nullptr));

  hal_rtc_datetime_t bad = in;
  bad.second = 60;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_set_datetime_ex(s_rtc, &bad));
}

void test_ex_epoch_and_control_status(void) {
  const uint64_t epoch = 1750000000ull;
  uint64_t out_epoch = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_set_epoch_ex(s_rtc, epoch));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_get_epoch_ex(s_rtc, &out_epoch));
  TEST_ASSERT_EQUAL_UINT64(epoch, out_epoch);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_get_epoch_ex(s_rtc, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_set_epoch_ex(nullptr, epoch));
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        hal_rtc_set_epoch_ex(s_rtc, 4102444800ull));

  hal_rtc_datetime_t pre_unix = {};
  pre_unix.second = 59;
  pre_unix.minute = 59;
  pre_unix.hour = 23;
  pre_unix.day = 31;
  pre_unix.weekday = 3;
  pre_unix.month = 12;
  pre_unix.year = 1969;
  pre_unix.clock_integrity = true;
  hal_mock_rtc_set_datetime(s_rtc, &pre_unix);
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, hal_rtc_get_epoch_ex(s_rtc, &out_epoch));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_set_epoch_ex(s_rtc, epoch));

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_rtc_set_interrupt_enable_ex(s_rtc, HAL_RTC_IRQ_ALARM));
  uint8_t mask = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_get_interrupt_enable_ex(s_rtc, &mask));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_rtc_get_interrupt_enable_ex(s_rtc, nullptr));

  const hal_rtc_alarm_t alarm = valid_alarm();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_set_alarm_ex(s_rtc, &alarm));
  hal_rtc_alarm_t read_alarm = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_get_alarm_ex(s_rtc, &read_alarm));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_set_alarm_ex(nullptr, &alarm));
}

void test_ex_feature_statuses_and_validation(void) {
  bool integrity_ok = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_rtc_get_clock_integrity_ex(s_rtc, &integrity_ok));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_rtc_get_clock_integrity_ex(s_rtc, nullptr));

  uint8_t flags = 0;
  hal_mock_rtc_set_flags(s_rtc, HAL_RTC_FLAG_ALARM | HAL_RTC_FLAG_TIMER);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_get_and_clear_flags_ex(s_rtc, &flags));
  TEST_ASSERT_EQUAL_UINT8((HAL_RTC_FLAG_ALARM | HAL_RTC_FLAG_TIMER), flags);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_rtc_get_and_clear_flags_ex(s_rtc, nullptr));

  float temperature_c = 123.0f;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_rtc_get_temperature_ex(s_rtc, &temperature_c));
  TEST_ASSERT_EQUAL_FLOAT(123.0f, temperature_c);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_get_temperature_ex(s_rtc, nullptr));

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_rtc_set_clkout_mode_ex(s_rtc, HAL_RTC_CLKOUT_1024_HZ));
  hal_rtc_clkout_mode_t clkout = HAL_RTC_CLKOUT_DISABLED;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_get_clkout_mode_ex(s_rtc, &clkout));
  TEST_ASSERT_EQUAL_INT((int)HAL_RTC_CLKOUT_1024_HZ, (int)clkout);
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_rtc_set_clkout_mode_ex(s_rtc, (hal_rtc_clkout_mode_t)99));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_get_clkout_mode_ex(s_rtc, nullptr));

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_rtc_set_timer_ex(s_rtc, HAL_RTC_TIMER_64_HZ, 42));
  hal_rtc_timer_clock_t timer_clock = HAL_RTC_TIMER_DISABLED;
  uint8_t timer_count = 0;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_rtc_get_timer_ex(s_rtc, &timer_clock, &timer_count));
  TEST_ASSERT_EQUAL_INT((int)HAL_RTC_TIMER_64_HZ, (int)timer_clock);
  TEST_ASSERT_EQUAL_UINT8(42, timer_count);
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_rtc_set_timer_ex(s_rtc, (hal_rtc_timer_clock_t)77, 10));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_rtc_get_timer_ex(s_rtc, nullptr, &timer_count));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_rtc_get_timer_ex(s_rtc, &timer_clock, nullptr));

  hal_rtc_alarm_t alarm = valid_alarm();
  alarm.minute = 60;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_set_alarm_ex(s_rtc, &alarm));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_set_alarm_ex(s_rtc, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rtc_get_alarm_ex(s_rtc, nullptr));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_returns_handle);
  RUN_TEST(test_init_ds3231_backend_returns_handle);
  RUN_TEST(test_clock_source_reports_external_provider);
  RUN_TEST(test_internal_backend_skips_i2c_config_and_resolves_clock);
  RUN_TEST(test_internal_backend_rejects_unsupported_clock_source);
  RUN_TEST(test_internal_mock_accepts_aon_clock_source);
  RUN_TEST(test_relative_wakeup_is_internal_and_one_shot);
  RUN_TEST(test_relative_wakeup_rejects_invalid_arguments);
  RUN_TEST(test_init_rejects_unknown_backend);
  RUN_TEST(test_default_datetime_is_readable);
  RUN_TEST(test_set_and_get_datetime_roundtrip);
  RUN_TEST(test_set_datetime_rejects_invalid_values);
  RUN_TEST(test_epoch_roundtrip_from_unix_epoch_start);
  RUN_TEST(test_epoch_leap_day_roundtrip);
  RUN_TEST(test_get_epoch_from_datetime);
  RUN_TEST(test_set_epoch_rejects_out_of_supported_range);
  RUN_TEST(test_get_epoch_rejects_pre_unix_datetime);
  RUN_TEST(test_clock_integrity_flag_can_be_injected);
  RUN_TEST(test_mock_datetime_injection);
  RUN_TEST(test_interrupt_enable_roundtrip);
  RUN_TEST(test_get_and_clear_flags);
  RUN_TEST(test_clkout_roundtrip_and_validation);
  RUN_TEST(test_timer_roundtrip_and_validation);
  RUN_TEST(test_alarm_roundtrip_and_validation);
  RUN_TEST(test_invalid_arguments_return_false);
  RUN_TEST(test_ex_init_and_handle_status);
  RUN_TEST(test_ex_deinit_invalidates_handle_and_releases_slot);
  RUN_TEST(test_ex_init_reports_pool_exhaustion);
  RUN_TEST(test_ex_datetime_roundtrip_and_validation);
  RUN_TEST(test_ex_epoch_and_control_status);
  RUN_TEST(test_ex_feature_statuses_and_validation);
  return UNITY_END();
}

#else

int main(void) {
  UNITY_BEGIN();
  return UNITY_END();
}

#endif /* HAL_ENABLE_RTC */
