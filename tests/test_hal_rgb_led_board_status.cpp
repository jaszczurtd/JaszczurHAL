/* hal_rgb_led_init_board_status() with an addressable status LED, which the
 * host-mock board lacks: this unit defines what the generated headers would.
 * CMake builds it once per source the helper reads:
 * - BOARD_RGB / BOARD_GRB: jh_board_config.h only, as for an assembly whose
 *   statusLed is a node of its own (the ESP32-S3-Zero WS2812 on GPIO21);
 * - ASSEMBLY: jh_hardware.h only, the board.statusLed node of the assembly,
 *   as host tests that load jh_hardware.h see it. */
#if defined(TEST_STATUS_LED_ASSEMBLY)
#define JH_HW_NODE_BOARD_STATUS_LED_PIN_DATA 16U
#define JH_HW_NODE_BOARD_STATUS_LED_PROP_PIXEL_ORDER "RGB"
#define JH_HW_NODE_BOARD_STATUS_LED_PROP_COUNT 2U
#define EXPECTED_PIN 16
#define EXPECTED_PIXELS 2
#define EXPECTED_PIXEL_TYPE HAL_RGB_LED_PIXEL_RGB_KHZ800
#else
#define HAL_BOARD_STATUS_LED_KIND_ADDRESSABLE 1
#define HAL_BOARD_STATUS_LED_PIN 21u
#define EXPECTED_PIN 21
#define EXPECTED_PIXELS 1
#if defined(TEST_STATUS_LED_BOARD_RGB)
#define HAL_BOARD_STATUS_LED_PIXEL_ORDER_RGB 1
#define EXPECTED_PIXEL_TYPE HAL_RGB_LED_PIXEL_RGB_KHZ800
#else
#define HAL_BOARD_STATUS_LED_PIXEL_ORDER_GRB 1
#define EXPECTED_PIXEL_TYPE HAL_RGB_LED_PIXEL_GRB_KHZ800
#endif
#endif

#include "hal/gpio/hal_rgb_led.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#ifdef HAL_ENABLE_RGB_LED

void setUp(void) { hal_mock_rgb_led_reset(); }

void tearDown(void) {}

void test_starts_the_described_status_led(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rgb_led_init_board_status());

  TEST_ASSERT_TRUE(hal_mock_rgb_led_is_initialized());
  TEST_ASSERT_EQUAL_UINT8(EXPECTED_PIN, hal_mock_rgb_led_get_pin());
  TEST_ASSERT_EQUAL_UINT8(EXPECTED_PIXELS, hal_mock_rgb_led_get_num_pixels());
  TEST_ASSERT_EQUAL_INT(EXPECTED_PIXEL_TYPE, hal_mock_rgb_led_get_pixel_type());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_starts_the_described_status_led);
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
