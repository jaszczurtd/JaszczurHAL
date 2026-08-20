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

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pwm_source_clock_tracks_pin_timer_bus);
  RUN_TEST(test_pwm_source_clock_rejects_unknown_pin);
  return UNITY_END();
}
