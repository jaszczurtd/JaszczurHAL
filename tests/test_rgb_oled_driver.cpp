#include "utils/unity.h"

#include "hal/hal_spi.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/drivers/display/rgb_oled_driver.h"

#include <string.h>

void setUp(void) {
  hal_mock_spi_reset();
  hal_mock_set_millis(0u);
}

void tearDown(void) {}

static jh_rgb_oled_config_t make_config(jh_rgb_oled_controller_t controller) {
  jh_rgb_oled_config_t config = {};
  config.bus = 0u;
  config.cs_pin = 10;
  config.dc_pin = 11;
  config.rst_pin = -1;
  config.clock_hz = 16000000u;
  config.spi_mode = HAL_SPI_MODE0;
  config.controller = controller;
  config.width = controller == JH_RGB_OLED_SSD1331 ? 96u : 128u;
  config.height = controller == JH_RGB_OLED_SSD1331 ? 64u : 128u;
  return config;
}

static bool tx_contains(const uint8_t *needle, size_t needle_len) {
  uint8_t tx[2048] = {};
  const size_t tx_len = hal_mock_spi_get_tx(0u, tx, sizeof(tx));
  if (needle_len == 0u || tx_len < needle_len) {
    return false;
  }
  for (size_t i = 0u; i <= tx_len - needle_len; ++i) {
    if (memcmp(&tx[i], needle, needle_len) == 0) {
      return true;
    }
  }
  return false;
}

void test_ssd1331_init_and_write_rgb565(void) {
  jh_rgb_oled_t dev = {};
  const jh_rgb_oled_config_t config = make_config(JH_RGB_OLED_SSD1331);
  const uint16_t pixels[] = {0x1234u, 0xABCDu};

  TEST_ASSERT_TRUE(jh_rgb_oled_init(&dev, &config));
  TEST_ASSERT_EQUAL_UINT16(96u, dev.width);
  TEST_ASSERT_EQUAL_UINT16(64u, dev.height);
  const uint8_t init_tail[] = {0xAFu};
  TEST_ASSERT_TRUE(tx_contains(init_tail, sizeof(init_tail)));

  hal_mock_spi_reset();
  TEST_ASSERT_TRUE(jh_rgb_oled_draw_rgb_bitmap(&dev, 1u, 2u, pixels, 2u, 1u));
  const uint8_t expected[] = {0x15u, 0x01u, 0x02u, 0x75u, 0x02u,
                              0x02u, 0x12u, 0x34u, 0xABu, 0xCDu};
  TEST_ASSERT_TRUE(tx_contains(expected, sizeof(expected)));
}

void test_ssd135x_init_write_command_and_column_offset(void) {
  jh_rgb_oled_t dev = {};
  jh_rgb_oled_config_t config = make_config(JH_RGB_OLED_SSD1351);
  config.column_offset = 2u;
  const uint16_t pixels[] = {0xF800u};

  TEST_ASSERT_TRUE(jh_rgb_oled_init(&dev, &config));
  const uint8_t unlock[] = {0xFDu, 0x12u, 0xFDu, 0xB1u};
  TEST_ASSERT_TRUE(tx_contains(unlock, sizeof(unlock)));

  hal_mock_spi_reset();
  TEST_ASSERT_TRUE(jh_rgb_oled_draw_rgb_bitmap(&dev, 1u, 3u, pixels, 1u, 1u));
  const uint8_t expected[] = {0x15u, 0x03u, 0x03u, 0x75u, 0x03u,
                              0x03u, 0x5Cu, 0xF8u, 0x00u};
  TEST_ASSERT_TRUE(tx_contains(expected, sizeof(expected)));
}

void test_rgb_oled_rejects_non_native_rotation(void) {
  jh_rgb_oled_t dev = {};
  const jh_rgb_oled_config_t config = make_config(JH_RGB_OLED_SSD1331);

  TEST_ASSERT_TRUE(jh_rgb_oled_init(&dev, &config));
  TEST_ASSERT_TRUE(jh_rgb_oled_set_rotation(&dev, 0u));
  TEST_ASSERT_FALSE(jh_rgb_oled_set_rotation(&dev, 1u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ssd1331_init_and_write_rgb565);
  RUN_TEST(test_ssd135x_init_write_command_and_column_offset);
  RUN_TEST(test_rgb_oled_rejects_non_native_rotation);
  return UNITY_END();
}
