#include "hal/impl/stm32g474/hal_pwm_stm32g474.h"
#include "hal/impl/stm32g474/port/stm32g474_regs.h"
#include "utils/unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_pwm_source_clock_tracks_pin_timer_bus(void) {
  TEST_ASSERT_EQUAL_UINT32(JH_G474_TIMCLK1_HZ,
                           jh_stm32_pwm_source_clock_hz(5u));
  TEST_ASSERT_EQUAL_UINT32(JH_G474_TIMCLK2_HZ,
                           jh_stm32_pwm_source_clock_hz(2u));
}

void test_pwm_source_clock_rejects_unknown_pin(void) {
  TEST_ASSERT_EQUAL_UINT32(0u, jh_stm32_pwm_source_clock_hz(127u));
}

void test_pwm_direct_timing_rejects_unrepresentable_rates(void) {
  jh_stm32_pwm_channel_desc channel = {};
  const uint32_t clock_hz = jh_stm32_pwm_source_clock_hz(5u);
  const uint32_t period_ticks = 4096u;
  const uint32_t nearest_unscaled_rate =
      (clock_hz + period_ticks / 2u) / period_ticks;

  TEST_ASSERT_TRUE(jh_stm32_pwm_prepare_pin(5u, nearest_unscaled_rate,
                                            period_ticks, &channel));
  TEST_ASSERT_FALSE(jh_stm32_pwm_prepare_pin(5u, nearest_unscaled_rate + 1u,
                                             period_ticks, &channel));

  const uint32_t slowest_rate = (clock_hz + 65535u) / 65536u;
  TEST_ASSERT_TRUE(jh_stm32_pwm_prepare_pin(5u, slowest_rate, 1u, &channel));
  TEST_ASSERT_FALSE(
      jh_stm32_pwm_prepare_pin(5u, slowest_rate - 1u, 1u, &channel));
  TEST_ASSERT_FALSE(jh_stm32_pwm_prepare_pin(5u, 1u, 65537u, &channel));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pwm_source_clock_tracks_pin_timer_bus);
  RUN_TEST(test_pwm_source_clock_rejects_unknown_pin);
  RUN_TEST(test_pwm_direct_timing_rejects_unrepresentable_rates);
  return UNITY_END();
}
