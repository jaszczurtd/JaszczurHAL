#include "hal/display/drivers/ili9341_driver.h"
#include "hal/display/hal_display.h"
#include "utils/unity.h"

/* hal_mock.h needs the full mock feature set; declare what this test reads. */
void hal_mock_spi_reset(void);
uint32_t hal_mock_spi_get_clock_hz(uint8_t bus);
uint32_t hal_mock_spi_get_transfer_count(uint8_t bus);
void hal_mock_set_millis(uint32_t ms);
void hal_mock_set_micros(uint32_t us);
bool hal_mock_gpio_is_output(uint8_t pin);

void setUp(void) {
  hal_mock_spi_reset();
  hal_mock_set_millis(0u);
  hal_mock_set_micros(0u);
}
void tearDown(void) {}

void test_ex_takes_bus_and_clock_from_its_config(void) {
  const hal_display_ili9341_config_t config = {1u, 5, 6, -1, 8000000u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_ili9341_ex(&config));
  TEST_ASSERT_EQUAL_UINT32(8000000u, hal_mock_spi_get_clock_hz(1u));
  TEST_ASSERT_TRUE(hal_mock_spi_get_transfer_count(1u) > 0u);
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_spi_get_transfer_count(0u));
  TEST_ASSERT_TRUE(hal_mock_gpio_is_output(6u));
}

void test_zero_clock_and_legacy_init_keep_the_default(void) {
  const hal_display_ili9341_config_t config = {0u, 5, 6, 7, 0u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_ili9341_ex(&config));
  TEST_ASSERT_EQUAL_UINT32(JH_ILI9341_SPI_DEFAULT_HZ,
                           hal_mock_spi_get_clock_hz(0u));
  hal_mock_spi_reset();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init(5u, 6u, 0xFFu));
  TEST_ASSERT_EQUAL_UINT32(JH_ILI9341_SPI_DEFAULT_HZ,
                           hal_mock_spi_get_clock_hz(0u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ex_takes_bus_and_clock_from_its_config);
  RUN_TEST(test_zero_clock_and_legacy_init_keep_the_default);
  return UNITY_END();
}
