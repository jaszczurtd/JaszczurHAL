#include "hal/core/hal_array.h"
#include "hal/display/hal_hd44780.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <stddef.h>
#include <stdint.h>

#define LCD_RS 2
#define LCD_EN 3
#define LCD_D4 4
#define LCD_D5 5
#define LCD_D6 6
#define LCD_D7 7

static hal_hd44780_config_t four_bit_config(void) {
  hal_hd44780_config_t config = {0};
  config.rs_pin = LCD_RS;
  config.rw_pin = HAL_HD44780_PIN_NONE;
  config.enable_pin = LCD_EN;
  config.data_pins[0] = LCD_D4;
  config.data_pins[1] = LCD_D5;
  config.data_pins[2] = LCD_D6;
  config.data_pins[3] = LCD_D7;
  config.bus_width = HAL_HD44780_BUS_4_BIT;
  return config;
}

static hal_hd44780_config_t eight_bit_config(void) {
  hal_hd44780_config_t config = {0};
  config.rs_pin = LCD_RS;
  config.rw_pin = HAL_HD44780_PIN_NONE;
  config.enable_pin = LCD_EN;
  for (size_t index = 0u; index < COUNTOF(config.data_pins); ++index) {
    config.data_pins[index] = (int16_t)(10u + index);
  }
  config.bus_width = HAL_HD44780_BUS_8_BIT;
  return config;
}

void setUp(void) { hal_mock_mutex_fail_next_create(false); }

void tearDown(void) {}

void test_hd44780_c_api_validates_configuration(void) {
  hal_hd44780_config_t config = four_bit_config();
  hal_hd44780_t lcd = NULL;

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_hd44780_create(NULL, &lcd));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_hd44780_create(&config, NULL));

  config.data_pins[0] = config.rs_pin;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_hd44780_create(&config, &lcd));

  config = four_bit_config();
  config.rw_pin = -2;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_hd44780_create(&config, &lcd));

  config = four_bit_config();
  config.rs_pin = 64;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_hd44780_create(&config, &lcd));

  config = four_bit_config();
  config.bus_width = (hal_hd44780_bus_width_t)6;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_hd44780_create(&config, &lcd));
  TEST_ASSERT_NULL(lcd);
}

void test_hd44780_c_api_exposes_lifecycle_and_display_operations(void) {
  const hal_hd44780_config_t config = four_bit_config();
  hal_hd44780_t lcd = NULL;
  size_t written = 0u;
  const uint8_t bytes[] = {'O', 'K'};
  const uint8_t glyph[8] = {0x00u, 0x04u, 0x0eu, 0x15u,
                            0x04u, 0x04u, 0x04u, 0x00u};

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_create(&config, &lcd));
  TEST_ASSERT_NOT_NULL(lcd);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_hd44780_begin(lcd, 16u, 2u, HAL_HD44780_FONT_5X8));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_hd44780_begin(lcd, 0u, 2u, HAL_HD44780_FONT_5X8));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_hd44780_begin(lcd, 16u, 2u, HAL_HD44780_FONT_5X10));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_clear(lcd));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_home(lcd));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_hd44780_set_row_offsets(lcd, 0, 64, 16, 80));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_set_cursor(lcd, 3u, 1u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_set_display_enabled(lcd, false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_set_display_enabled(lcd, true));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_set_cursor_enabled(lcd, true));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_set_cursor_enabled(lcd, false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_set_blink_enabled(lcd, true));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_set_blink_enabled(lcd, false));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_hd44780_scroll(lcd, HAL_HD44780_SCROLL_LEFT));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_hd44780_scroll(lcd, HAL_HD44780_SCROLL_RIGHT));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_set_text_direction(
                                    lcd, HAL_HD44780_TEXT_RIGHT_TO_LEFT));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_set_text_direction(
                                    lcd, HAL_HD44780_TEXT_LEFT_TO_RIGHT));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_set_autoscroll_enabled(lcd, true));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_set_autoscroll_enabled(lcd, false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_create_char(lcd, 0u, glyph));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_hd44780_create_char(lcd, 0u, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_command(lcd, 0x06u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_write_byte(lcd, 'A'));

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_hd44780_write(lcd, bytes, sizeof(bytes), &written));
  TEST_ASSERT_EQUAL_size_t(sizeof(bytes), written);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_write(lcd, NULL, 0u, &written));
  TEST_ASSERT_EQUAL_size_t(0u, written);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_hd44780_write(lcd, NULL, 1u, &written));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_print(lcd, "LCD", &written));
  TEST_ASSERT_EQUAL_size_t(3u, written);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_hd44780_print(lcd, NULL, &written));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_destroy(lcd));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_hd44780_clear(lcd));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_hd44780_destroy(lcd));
}

void test_hd44780_c_api_reports_static_pool_exhaustion(void) {
  const hal_hd44780_config_t config = four_bit_config();
  hal_hd44780_t handles[HAL_HD44780_MAX_INSTANCES] = {0};
  hal_hd44780_t extra = NULL;

  for (size_t index = 0u; index < COUNTOF(handles); ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_create(&config, &handles[index]));
  }
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_hd44780_create(&config, &extra));
  TEST_ASSERT_NULL(extra);

  for (size_t index = 0u; index < COUNTOF(handles); ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_destroy(handles[index]));
  }
}

void test_hd44780_c_api_reports_mutex_allocation_failure(void) {
  const hal_hd44780_config_t config = four_bit_config();
  hal_hd44780_t lcd = NULL;

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_create(&config, &lcd));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_destroy(lcd));

  lcd = NULL;
  hal_mock_mutex_fail_next_create(true);
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_hd44780_create(&config, &lcd));
  TEST_ASSERT_NULL(lcd);
}

void test_hd44780_c_api_supports_rw_and_eight_bit_configurations(void) {
  hal_hd44780_config_t config = four_bit_config();
  hal_hd44780_t lcd = NULL;

  config.rw_pin = 8;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_create(&config, &lcd));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_destroy(lcd));

  config = eight_bit_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_create(&config, &lcd));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_write_byte(lcd, '8'));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_destroy(lcd));

  config.rw_pin = 8;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_create(&config, &lcd));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_hd44780_destroy(lcd));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_hd44780_c_api_validates_configuration);
  RUN_TEST(test_hd44780_c_api_exposes_lifecycle_and_display_operations);
  RUN_TEST(test_hd44780_c_api_reports_static_pool_exhaustion);
  RUN_TEST(test_hd44780_c_api_reports_mutex_allocation_failure);
  RUN_TEST(test_hd44780_c_api_supports_rw_and_eight_bit_configurations);
  return UNITY_END();
}
