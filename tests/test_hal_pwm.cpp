#include "hal/gpio/hal_pwm.h"
#include "hal/gpio/hal_pwm_freq.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_default_resolution(void) {
  TEST_ASSERT_EQUAL_UINT8(8, hal_mock_pwm_get_resolution());
}

void test_set_resolution(void) {
  hal_pwm_set_resolution(12);
  TEST_ASSERT_EQUAL_UINT8(12, hal_mock_pwm_get_resolution());
  hal_pwm_set_resolution(8);
}

void test_write_stores_value(void) {
  hal_pwm_write(3, 128);
  TEST_ASSERT_EQUAL_UINT32(128, hal_mock_pwm_get_value(3));
}

void test_pin_support_query(void) {
  TEST_ASSERT_TRUE(hal_pwm_is_pin_supported(3));
  TEST_ASSERT_FALSE(hal_pwm_is_pin_supported(64));
}

void test_write_clamps_to_resolution_max(void) {
  hal_pwm_set_resolution(8);
  hal_pwm_write(4, 300);

  TEST_ASSERT_EQUAL_UINT32(255, hal_mock_pwm_get_value(4));
}

void test_write_zero(void) {
  hal_pwm_write(5, 255);
  hal_pwm_write(5, 0);
  TEST_ASSERT_EQUAL_UINT32(0, hal_mock_pwm_get_value(5));
}

void test_write_multiple_pins_independent(void) {
  hal_pwm_write(0, 10);
  hal_pwm_write(1, 20);
  TEST_ASSERT_EQUAL_UINT32(10, hal_mock_pwm_get_value(0));
  TEST_ASSERT_EQUAL_UINT32(20, hal_mock_pwm_get_value(1));
}

void test_pwm_freq_write_null_is_noop(void) { hal_pwm_freq_write(NULL, 123); }

void test_pwm_freq_set_frequency_keeps_the_channel_and_its_duty(void) {
  hal_pwm_freq_channel_t ch = hal_pwm_freq_create(9u, 200u, 2047u);
  TEST_ASSERT_NOT_NULL(ch);
  hal_pwm_freq_write(ch, 700);
  TEST_ASSERT_EQUAL_UINT32(5000000u, hal_pwm_freq_get_period_ns(ch));

  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(ch, 400u));
  TEST_ASSERT_EQUAL_UINT32(400u, hal_mock_pwm_freq_get_frequency(ch));
  TEST_ASSERT_EQUAL_UINT32(2500000u, hal_pwm_freq_get_period_ns(ch));
  TEST_ASSERT_EQUAL_INT(700, hal_mock_pwm_freq_get_value(ch));
  TEST_ASSERT_TRUE(hal_mock_pwm_freq_is_running(ch));
  TEST_ASSERT_EQUAL_UINT8(9u, hal_mock_pwm_freq_get_pin(ch));

  hal_pwm_freq_destroy(ch);
}

void test_pwm_freq_set_frequency_rejects_invalid_requests(void) {
  hal_pwm_freq_channel_t ch = hal_pwm_freq_create(9u, 200u, 2047u);
  TEST_ASSERT_NOT_NULL(ch);

  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_pwm_freq_set_frequency(NULL, 400u));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_pwm_freq_set_frequency(ch, 0u));
  TEST_ASSERT_EQUAL_UINT32(200u, hal_mock_pwm_freq_get_frequency(ch));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_pwm_freq_get_period_ns(NULL));

  hal_pwm_freq_destroy(ch);
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_pwm_freq_set_frequency(ch, 400u));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_pwm_freq_get_period_ns(ch));
}

void test_pwm_freq_mock_failure_is_reported_once_and_changes_nothing(void) {
  hal_pwm_freq_channel_t ch = hal_pwm_freq_create(9u, 200u, 2047u);
  TEST_ASSERT_NOT_NULL(ch);

  hal_mock_pwm_freq_fail_next_set_frequency(HAL_EBUSY);
  TEST_ASSERT_EQUAL(HAL_EBUSY, hal_pwm_freq_set_frequency(ch, 400u));
  TEST_ASSERT_EQUAL_UINT32(200u, hal_mock_pwm_freq_get_frequency(ch));
  TEST_ASSERT_EQUAL(HAL_OK, hal_pwm_freq_set_frequency(ch, 400u));
  TEST_ASSERT_EQUAL_UINT32(400u, hal_mock_pwm_freq_get_frequency(ch));

  hal_pwm_freq_destroy(ch);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_default_resolution);
  RUN_TEST(test_set_resolution);
  RUN_TEST(test_write_stores_value);
  RUN_TEST(test_pin_support_query);
  RUN_TEST(test_write_clamps_to_resolution_max);
  RUN_TEST(test_write_zero);
  RUN_TEST(test_write_multiple_pins_independent);
  RUN_TEST(test_pwm_freq_write_null_is_noop);
  RUN_TEST(test_pwm_freq_set_frequency_keeps_the_channel_and_its_duty);
  RUN_TEST(test_pwm_freq_set_frequency_rejects_invalid_requests);
  RUN_TEST(test_pwm_freq_mock_failure_is_reported_once_and_changes_nothing);
  return UNITY_END();
}
