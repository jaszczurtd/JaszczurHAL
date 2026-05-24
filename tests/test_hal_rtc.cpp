#include "utils/unity.h"
#include "hal/hal_rtc.h"
#include "hal/impl/.mock/hal_mock.h"

#ifndef HAL_DISABLE_RTC

static hal_rtc_t s_rtc = nullptr;

static hal_rtc_config_t default_cfg(void) {
    hal_rtc_config_t cfg = {};
    cfg.chip = HAL_RTC_CHIP_PCF8563;
    cfg.bus.i2c.sda_pin = 4;
    cfg.bus.i2c.scl_pin = 5;
    cfg.bus.i2c.clock_hz = 400000;
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

void test_init_returns_handle(void) {
    TEST_ASSERT_NOT_NULL(s_rtc);
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
        HAL_RTC_CLKOUT_DISABLED,
        HAL_RTC_CLKOUT_1_HZ,
        HAL_RTC_CLKOUT_32_HZ,
        HAL_RTC_CLKOUT_1024_HZ,
        HAL_RTC_CLKOUT_32768_HZ,
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
    hal_rtc_timer_clock_t timer_clock = HAL_RTC_TIMER_DISABLED;
    uint8_t timer_count = 0;
    hal_rtc_alarm_t alarm = valid_alarm();

    TEST_ASSERT_FALSE(hal_rtc_get_datetime(nullptr, &dt));
    TEST_ASSERT_FALSE(hal_rtc_get_datetime(s_rtc, nullptr));

    TEST_ASSERT_FALSE(hal_rtc_set_datetime(nullptr, &dt));
    TEST_ASSERT_FALSE(hal_rtc_set_datetime(s_rtc, nullptr));

    TEST_ASSERT_FALSE(hal_rtc_get_clock_integrity(nullptr, &ok));
    TEST_ASSERT_FALSE(hal_rtc_get_clock_integrity(s_rtc, nullptr));

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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_returns_handle);
    RUN_TEST(test_default_datetime_is_readable);
    RUN_TEST(test_set_and_get_datetime_roundtrip);
    RUN_TEST(test_set_datetime_rejects_invalid_values);
    RUN_TEST(test_clock_integrity_flag_can_be_injected);
    RUN_TEST(test_mock_datetime_injection);
    RUN_TEST(test_interrupt_enable_roundtrip);
    RUN_TEST(test_get_and_clear_flags);
    RUN_TEST(test_clkout_roundtrip_and_validation);
    RUN_TEST(test_timer_roundtrip_and_validation);
    RUN_TEST(test_alarm_roundtrip_and_validation);
    RUN_TEST(test_invalid_arguments_return_false);
    return UNITY_END();
}

#else

int main(void) {
    UNITY_BEGIN();
    return UNITY_END();
}

#endif /* HAL_DISABLE_RTC */
