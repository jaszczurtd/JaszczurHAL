#include "utils/unity.h"

#include "hal/display/drivers/ili9341_driver.h"
#include "hal/impl/.mock/hal_mock.h"
#include "support/display_spi_test_helpers.h"

#include <string.h>

static jh_ili9341_config_t make_config(void) {
  jh_ili9341_config_t config = {};
  config.bus = 0u;
  config.cs_pin = 10;
  config.dc_pin = 11;
  config.rst_pin = -1;
  config.clock_hz = 24000000u;
  return config;
}

void setUp(void) {
  hal_mock_spi_reset();
  hal_mock_set_millis(0u);
  hal_mock_set_micros(0u);
}

void tearDown(void) {}

void test_init_sends_ili9341_sequence_over_hal_spi(void) {
  jh_ili9341_t dev = {};
  const jh_ili9341_config_t config = make_config();

  TEST_ASSERT_TRUE(jh_ili9341_init(&dev, &config));

  TEST_ASSERT_TRUE(dev.initialized);
  TEST_ASSERT_EQUAL_UINT16(JH_ILI9341_TFTWIDTH, dev.width);
  TEST_ASSERT_EQUAL_UINT16(JH_ILI9341_TFTHEIGHT, dev.height);
  TEST_ASSERT_TRUE(hal_mock_gpio_is_output((uint8_t)config.cs_pin));
  TEST_ASSERT_TRUE(hal_mock_gpio_is_output((uint8_t)config.dc_pin));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state((uint8_t)config.cs_pin));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state((uint8_t)config.dc_pin));
  TEST_ASSERT_EQUAL_UINT32(config.clock_hz,
                           hal_mock_spi_get_clock_hz(config.bus));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(config.bus));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(config.bus));

  uint8_t tx[8] = {};
  const size_t tx_len = hal_mock_spi_get_tx(config.bus, tx, sizeof(tx));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT(8u, tx_len);
  TEST_ASSERT_EQUAL_HEX8(0x01, tx[0]); /* SWRESET when no RST pin is wired. */
  TEST_ASSERT_EQUAL_HEX8(0xEF, tx[1]);
  TEST_ASSERT_EQUAL_HEX8(0x03, tx[2]);
  TEST_ASSERT_EQUAL_HEX8(0x80, tx[3]);
  TEST_ASSERT_EQUAL_HEX8(0x02, tx[4]);
}

void test_set_rotation_writes_madctl_and_updates_dimensions(void) {
  jh_ili9341_t dev = {};
  const jh_ili9341_config_t config = make_config();
  TEST_ASSERT_TRUE(jh_ili9341_init(&dev, &config));

  TEST_ASSERT_TRUE(jh_ili9341_set_rotation(&dev, 1u));

  const uint8_t tail[] = {0x36u, 0x28u};
  TEST_ASSERT_TRUE(tx_has_tail(tail, sizeof(tail)));
  TEST_ASSERT_EQUAL_UINT16(JH_ILI9341_TFTHEIGHT, dev.width);
  TEST_ASSERT_EQUAL_UINT16(JH_ILI9341_TFTWIDTH, dev.height);
}

void test_addr_window_and_bitmap_write_big_endian_pixels(void) {
  jh_ili9341_t dev = {};
  const jh_ili9341_config_t config = make_config();
  const uint16_t pixels[] = {0x1234u, 0xABCDu};
  TEST_ASSERT_TRUE(jh_ili9341_init(&dev, &config));

  TEST_ASSERT_TRUE(jh_ili9341_draw_rgb_bitmap(&dev, 2u, 3u, pixels, 2u, 1u));

  const uint8_t tail[] = {0x2Au, 0x00u, 0x02u, 0x00u, 0x03u,
                          0x2Bu, 0x00u, 0x03u, 0x00u, 0x03u,
                          0x2Cu, 0x12u, 0x34u, 0xABu, 0xCDu};
  TEST_ASSERT_TRUE(tx_has_tail(tail, sizeof(tail)));
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_spi_get_dma_write_count(config.bus));
}

void test_fill_rect_falls_back_to_spi_write_when_dma_fails(void) {
  jh_ili9341_t dev = {};
  const jh_ili9341_config_t config = make_config();
  TEST_ASSERT_TRUE(jh_ili9341_init(&dev, &config));

  hal_mock_spi_reset();
  hal_mock_spi_fail_next_dma_write(config.bus, true);
  TEST_ASSERT_TRUE(jh_ili9341_fill_rect(&dev, 4u, 5u, 2u, 1u, 0x07E0u));

  const uint8_t tail[] = {0x07u, 0xE0u, 0x07u, 0xE0u};
  TEST_ASSERT_TRUE(tx_has_tail(tail, sizeof(tail)));
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_spi_get_dma_write_count(config.bus));
}

void test_stream_dma_failure_aborts_and_releases_device(void) {
  jh_ili9341_t dev = {};
  const jh_ili9341_config_t config = make_config();
  const uint8_t pixels[] = {0x12u, 0x34u};

  TEST_ASSERT_TRUE(jh_ili9341_init(&dev, &config));
  TEST_ASSERT_TRUE(jh_ili9341_begin_write(&dev, 0u, 0u, 1u, 1u));
  hal_mock_spi_fail_next_dma_write(config.bus, true);
  hal_mock_spi_fail_next_write(config.bus, true);
  TEST_ASSERT_FALSE(jh_ili9341_write_pixels_dma(&dev, pixels, sizeof(pixels)));
  TEST_ASSERT_FALSE(dev.write_active);
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state((uint8_t)config.cs_pin));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(config.bus));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(config.bus));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_sends_ili9341_sequence_over_hal_spi);
  RUN_TEST(test_set_rotation_writes_madctl_and_updates_dimensions);
  RUN_TEST(test_addr_window_and_bitmap_write_big_endian_pixels);
  RUN_TEST(test_fill_rect_falls_back_to_spi_write_when_dma_fails);
  RUN_TEST(test_stream_dma_failure_aborts_and_releases_device);
  return UNITY_END();
}
