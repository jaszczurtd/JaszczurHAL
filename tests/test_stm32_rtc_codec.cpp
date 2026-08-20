#include "hal/impl/stm32g474/port/stm32g474_rtc_codec.h"
#include "hal/impl/stm32g474/port/stm32g474_rtc_wakeup.h"
#include "utils/unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_stm32_rtc_datetime_register_roundtrip(void) {
  const hal_rtc_datetime_t input = {
      56u, 34u, 12u, 20u, 4u, 8u, 2026u, true,
  };
  uint32_t tr = 0u;
  uint32_t dr = 0u;
  TEST_ASSERT_TRUE(jh_stm32g474_rtc_encode_datetime(&input, &tr, &dr));
  TEST_ASSERT_EQUAL_HEX32(0x00123456u, tr);
  TEST_ASSERT_EQUAL_HEX32(0x0026A820u, dr);

  hal_rtc_datetime_t output = {};
  TEST_ASSERT_TRUE(jh_stm32g474_rtc_decode_datetime(tr, dr, &output));
  TEST_ASSERT_EQUAL_UINT8(input.second, output.second);
  TEST_ASSERT_EQUAL_UINT8(input.minute, output.minute);
  TEST_ASSERT_EQUAL_UINT8(input.hour, output.hour);
  TEST_ASSERT_EQUAL_UINT8(input.day, output.day);
  TEST_ASSERT_EQUAL_UINT8(input.weekday, output.weekday);
  TEST_ASSERT_EQUAL_UINT8(input.month, output.month);
  TEST_ASSERT_EQUAL_UINT16(input.year, output.year);
}

void test_stm32_rtc_datetime_enforces_internal_range_and_valid_bcd(void) {
  hal_rtc_datetime_t input = {
      59u, 59u, 23u, 31u, 5u, 12u, 2099u, false,
  };
  uint32_t tr = 0u;
  uint32_t dr = 0u;
  TEST_ASSERT_TRUE(jh_stm32g474_rtc_encode_datetime(&input, &tr, &dr));

  hal_rtc_datetime_t output = {};
  TEST_ASSERT_TRUE(jh_stm32g474_rtc_decode_datetime(tr, dr, &output));
  TEST_ASSERT_EQUAL_UINT16(2099u, output.year);

  input.year = 1999u;
  TEST_ASSERT_FALSE(jh_stm32g474_rtc_encode_datetime(&input, &tr, &dr));
  TEST_ASSERT_FALSE(jh_stm32g474_rtc_decode_datetime(0x0000006Au, dr, &output));

  input.year = 2025u;
  input.month = 2u;
  input.day = 29u;
  TEST_ASSERT_FALSE(jh_stm32g474_rtc_encode_datetime(&input, &tr, &dr));
}

void test_stm32_rtc_alarm_register_roundtrip(void) {
  const hal_rtc_alarm_t input = {
      true, 34u, true, 12u, false, 0u, true, 4u,
  };
  uint32_t reg = 0u;
  TEST_ASSERT_TRUE(jh_stm32g474_rtc_encode_alarm(&input, &reg));
  TEST_ASSERT_EQUAL_HEX32(0x45123480u, reg);

  hal_rtc_alarm_t output = {};
  TEST_ASSERT_TRUE(jh_stm32g474_rtc_decode_alarm(reg, &output));
  TEST_ASSERT_TRUE(output.minute_enabled);
  TEST_ASSERT_EQUAL_UINT8(34u, output.minute);
  TEST_ASSERT_TRUE(output.hour_enabled);
  TEST_ASSERT_EQUAL_UINT8(12u, output.hour);
  TEST_ASSERT_FALSE(output.day_enabled);
  TEST_ASSERT_TRUE(output.weekday_enabled);
  TEST_ASSERT_EQUAL_UINT8(4u, output.weekday);
}

void test_stm32_rtc_alarm_rejects_unrepresentable_match(void) {
  hal_rtc_alarm_t alarm = {};
  alarm.day_enabled = true;
  alarm.day = 20u;
  alarm.weekday_enabled = true;
  alarm.weekday = 4u;
  uint32_t reg = 0u;
  TEST_ASSERT_FALSE(jh_stm32g474_rtc_encode_alarm(&alarm, &reg));

  alarm = {};
  TEST_ASSERT_TRUE(jh_stm32g474_rtc_encode_alarm(&alarm, &reg));
  TEST_ASSERT_EQUAL_HEX32(JH_G474_RTC_ALARM_MSK1 | JH_G474_RTC_ALARM_MSK2 |
                              JH_G474_RTC_ALARM_MSK3 | JH_G474_RTC_ALARM_MSK4,
                          reg);
  TEST_ASSERT_FALSE(jh_stm32g474_rtc_alarm_enabled(&alarm));
}

void test_stm32_rtc_prescalers_produce_one_hz(void) {
  TEST_ASSERT_EQUAL_UINT32(32768u,
                           128u * ((JH_G474_RTC_PRER_LSE & 0x7fffu) + 1u));
  TEST_ASSERT_EQUAL_UINT32(32000u,
                           128u * ((JH_G474_RTC_PRER_LSI & 0x7fffu) + 1u));
}

void test_stm32_rtc_wakeup_rounds_up_and_covers_counter_range(void) {
  uint16_t counter = 0xffffu;
  uint64_t programmed_us = 0u;
  TEST_ASSERT_FALSE(
      jh_stm32g474_rtc_wakeup_compute(0u, &counter, &programmed_us));

  TEST_ASSERT_TRUE(
      jh_stm32g474_rtc_wakeup_compute(1u, &counter, &programmed_us));
  TEST_ASSERT_EQUAL_UINT16(0u, counter);
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(1000000), programmed_us);

  TEST_ASSERT_TRUE(jh_stm32g474_rtc_wakeup_compute(UINT64_C(1000001), &counter,
                                                   &programmed_us));
  TEST_ASSERT_EQUAL_UINT16(1u, counter);
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(2000000), programmed_us);

  TEST_ASSERT_TRUE(jh_stm32g474_rtc_wakeup_compute(
      JH_G474_RTC_WAKEUP_MAX_TIMEOUT_US, &counter, &programmed_us));
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, counter);
  TEST_ASSERT_EQUAL_UINT64(JH_G474_RTC_WAKEUP_MAX_TIMEOUT_US, programmed_us);

  TEST_ASSERT_FALSE(jh_stm32g474_rtc_wakeup_compute(
      JH_G474_RTC_WAKEUP_MAX_TIMEOUT_US + 1u, &counter, &programmed_us));
  TEST_ASSERT_FALSE(jh_stm32g474_rtc_wakeup_compute(UINT64_C(1000000), nullptr,
                                                    &programmed_us));
  TEST_ASSERT_FALSE(
      jh_stm32g474_rtc_wakeup_compute(UINT64_C(1000000), &counter, nullptr));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_stm32_rtc_datetime_register_roundtrip);
  RUN_TEST(test_stm32_rtc_datetime_enforces_internal_range_and_valid_bcd);
  RUN_TEST(test_stm32_rtc_alarm_register_roundtrip);
  RUN_TEST(test_stm32_rtc_alarm_rejects_unrepresentable_match);
  RUN_TEST(test_stm32_rtc_prescalers_produce_one_hz);
  RUN_TEST(test_stm32_rtc_wakeup_rounds_up_and_covers_counter_range);
  return UNITY_END();
}
