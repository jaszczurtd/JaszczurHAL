#include "utils/unity.h"

#include "hal/hal_spi.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/drivers/display/st7567_driver.h"

#include <string.h>

void setUp(void) {
  hal_mock_spi_reset();
  hal_mock_set_millis(0u);
}

void tearDown(void) {}

static jh_st7567_config_t make_config(void) {
  jh_st7567_config_t config = {};
  config.bus_type = JH_ST7567_BUS_SPI;
  config.bus = 0u;
  config.spi_cs_pin = 10;
  config.spi_dc_pin = 11;
  config.rst_pin = -1;
  config.clock_hz = 8000000u;
  config.spi_mode = HAL_SPI_MODE0;
  config.width = 132u;
  config.height = 64u;
  config.column_offset = 2u;
  config.line_offset = 0u;
  config.regulation_ratio = 4u;
  config.pixel_format = JH_ST7567_PIXEL_MONO10;
  return config;
}

static bool tx_contains(const uint8_t *needle, size_t needle_len) {
  uint8_t tx[1024] = {};
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

void test_st7567_init_sends_power_sequence_and_reports_buffer_size(void) {
  jh_st7567_t dev = {};
  const jh_st7567_config_t config = make_config();

  TEST_ASSERT_TRUE(jh_st7567_init(&dev, &config));
  TEST_ASSERT_EQUAL_size_t(132u * 8u, jh_st7567_buffer_size(&dev));
  const uint8_t init[] = {0xAEu, 0xA6u, 0xE2u};
  TEST_ASSERT_TRUE(tx_contains(init, sizeof(init)));
  const uint8_t power[] = {0x2Cu, 0x2Eu, 0x2Fu};
  TEST_ASSERT_TRUE(tx_contains(power, sizeof(power)));
}

void test_st7567_write_sets_column_page_and_streams_page_data(void) {
  jh_st7567_t dev = {};
  const jh_st7567_config_t config = make_config();
  const uint8_t page[] = {0xAAu, 0x55u, 0x00u};

  TEST_ASSERT_TRUE(jh_st7567_init(&dev, &config));
  hal_mock_spi_reset();
  TEST_ASSERT_TRUE(jh_st7567_write(&dev, 1u, 8u, 3u, 8u, page, sizeof(page)));

  const uint8_t command[] = {0x03u, 0x10u, 0xB1u};
  TEST_ASSERT_TRUE(tx_contains(command, sizeof(command)));
  TEST_ASSERT_TRUE(tx_contains(page, sizeof(page)));
}

void test_st7567_rejects_non_page_aligned_write(void) {
  jh_st7567_t dev = {};
  const jh_st7567_config_t config = make_config();
  const uint8_t page[] = {0xAAu};

  TEST_ASSERT_TRUE(jh_st7567_init(&dev, &config));
  TEST_ASSERT_FALSE(jh_st7567_write(&dev, 0u, 1u, 1u, 8u, page, sizeof(page)));
}

void test_st7567_rejects_out_of_bounds_write(void) {
  jh_st7567_t dev = {};
  const jh_st7567_config_t config = make_config();
  const uint8_t page[] = {0xAAu, 0x55u};

  TEST_ASSERT_TRUE(jh_st7567_init(&dev, &config));
  TEST_ASSERT_FALSE(
      jh_st7567_write(&dev, 131u, 0u, 2u, 8u, page, sizeof(page)));
  TEST_ASSERT_FALSE(jh_st7567_write(&dev, 0u, 64u, 1u, 8u, page, sizeof(page)));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_st7567_init_sends_power_sequence_and_reports_buffer_size);
  RUN_TEST(test_st7567_write_sets_column_page_and_streams_page_data);
  RUN_TEST(test_st7567_rejects_non_page_aligned_write);
  RUN_TEST(test_st7567_rejects_out_of_bounds_write);
  return UNITY_END();
}
