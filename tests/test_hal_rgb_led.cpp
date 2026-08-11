#include "hal/gpio/hal_rgb_led.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#ifdef HAL_ENABLE_RGB_LED

void setUp(void) { hal_mock_rgb_led_reset(); }

void tearDown(void) {}

void test_set_color_before_init_is_ignored(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_rgb_led_set_color(HAL_RGB_LED_RED));

  TEST_ASSERT_FALSE(hal_mock_rgb_led_is_initialized());
  TEST_ASSERT_EQUAL_INT(HAL_RGB_LED_NONE, hal_mock_rgb_led_get_color());
}

void test_init_sets_default_pixel_type_and_state(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rgb_led_init(8, 1));

  TEST_ASSERT_TRUE(hal_mock_rgb_led_is_initialized());
  TEST_ASSERT_EQUAL_UINT8(8, hal_mock_rgb_led_get_pin());
  TEST_ASSERT_EQUAL_UINT8(1, hal_mock_rgb_led_get_num_pixels());
  TEST_ASSERT_EQUAL_INT(HAL_RGB_LED_PIXEL_RGB_KHZ800,
                        hal_mock_rgb_led_get_pixel_type());
  TEST_ASSERT_EQUAL_INT(HAL_RGB_LED_NONE, hal_mock_rgb_led_get_color());
}

void test_init_ex_sets_explicit_pixel_type(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_rgb_led_init_ex(10, 4, HAL_RGB_LED_PIXEL_GRB_KHZ800));

  TEST_ASSERT_TRUE(hal_mock_rgb_led_is_initialized());
  TEST_ASSERT_EQUAL_UINT8(10, hal_mock_rgb_led_get_pin());
  TEST_ASSERT_EQUAL_UINT8(4, hal_mock_rgb_led_get_num_pixels());
  TEST_ASSERT_EQUAL_INT(HAL_RGB_LED_PIXEL_GRB_KHZ800,
                        hal_mock_rgb_led_get_pixel_type());
}

void test_brightness_clamp_and_off(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rgb_led_init(8, 1));

  hal_rgb_led_set_brightness(0);
  TEST_ASSERT_EQUAL_UINT8(1, hal_mock_rgb_led_get_brightness());

  hal_rgb_led_set_brightness(200);
  TEST_ASSERT_EQUAL_UINT8(200, hal_mock_rgb_led_get_brightness());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rgb_led_set_color(HAL_RGB_LED_PURPLE));
  TEST_ASSERT_EQUAL_INT(HAL_RGB_LED_PURPLE, hal_mock_rgb_led_get_color());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rgb_led_off());
  TEST_ASSERT_EQUAL_INT(HAL_RGB_LED_NONE, hal_mock_rgb_led_get_color());
}

void test_invalid_init_and_color_arguments_are_rejected(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_rgb_led_init(8, 0));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_rgb_led_init_ex(8, 1, (hal_rgb_led_pixel_type_t)0xFFFFu));
  TEST_ASSERT_FALSE(hal_mock_rgb_led_is_initialized());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rgb_led_init(8, 1));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      hal_rgb_led_set_color((hal_rgb_led_color_t)(HAL_RGB_LED_PURPLE + 1)));
  TEST_ASSERT_EQUAL_INT(HAL_RGB_LED_NONE, hal_mock_rgb_led_get_color());
}

void test_backend_failures_are_reported_and_state_is_retryable(void) {
  hal_mock_rgb_led_fail_next_init(true);
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_rgb_led_init(8, 1));
  TEST_ASSERT_FALSE(hal_mock_rgb_led_is_initialized());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rgb_led_init(8, 1));
  hal_mock_rgb_led_fail_next_write(true);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_rgb_led_set_color(HAL_RGB_LED_BLUE));
  TEST_ASSERT_EQUAL_INT(HAL_RGB_LED_NONE, hal_mock_rgb_led_get_color());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rgb_led_set_color(HAL_RGB_LED_BLUE));
  TEST_ASSERT_EQUAL_INT(HAL_RGB_LED_BLUE, hal_mock_rgb_led_get_color());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_set_color_before_init_is_ignored);
  RUN_TEST(test_init_sets_default_pixel_type_and_state);
  RUN_TEST(test_init_ex_sets_explicit_pixel_type);
  RUN_TEST(test_brightness_clamp_and_off);
  RUN_TEST(test_invalid_init_and_color_arguments_are_rejected);
  RUN_TEST(test_backend_failures_are_reported_and_state_is_retryable);
  return UNITY_END();
}

#else

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  return UNITY_END();
}

#endif /* HAL_ENABLE_RGB_LED */
