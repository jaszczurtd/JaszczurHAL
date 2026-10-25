#include "utils/unity.h"

#include "hal/display/drivers/ili9341_driver.h"
#include "hal/display/drivers/st77xx_driver.h"
#include "hal/impl/.mock/hal_mock.h"
#include "support/display_spi_panel_model.h"

/* Built with HAL_DISPLAY_SPI_MAX_TRANSFER_BYTES = 256: fills and bitmaps
 * release the bus after every band of whole rows that fits 256 bytes. */

#define FILL_COLOR 0x1234u

static panel_model_t s_panel;
static uint16_t s_expected[40u * 20u];

static jh_ili9341_t s_ili;
static jh_st77xx_t s_st;

static void start_ili9341(void) {
  jh_ili9341_config_t config = {};
  config.bus = 0u;
  config.cs_pin = 10;
  config.dc_pin = 11;
  config.rst_pin = -1;
  config.clock_hz = 24000000u;
  TEST_ASSERT_TRUE(jh_ili9341_init(&s_ili, &config));
  panel_model_attach(&s_panel, 0u, 10u, 11u);
}

static void start_st7789(void) {
  jh_st77xx_config_t config = {};
  config.bus = 0u;
  config.cs_pin = 10;
  config.dc_pin = 11;
  config.rst_pin = -1;
  config.clock_hz = 32000000u;
  config.spi_mode = HAL_SPI_MODE0;
  config.chip = JH_ST77XX_CHIP_ST7789;
  config.width = 135u;
  config.height = 240u;
  TEST_ASSERT_TRUE(jh_st77xx_init(&s_st, &config));
  panel_model_attach(&s_panel, 0u, 10u, 11u);
}

static void expect_bus_released(void) {
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0u));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(10u));
}

static void fill_expected(uint16_t w, uint16_t h, bool pattern) {
  for (size_t i = 0u; i < (size_t)w * h; ++i) {
    s_expected[i] = pattern ? (uint16_t)(i * 7u + 1u) : (uint16_t)FILL_COLOR;
  }
}

void setUp(void) {
  hal_mock_spi_reset();
  hal_mock_set_millis(0u);
  hal_mock_set_micros(0u);
}

void tearDown(void) { hal_mock_spi_reset(); }

void test_ili9341_fill_rect_sends_bands_within_the_limit(void) {
  start_ili9341();

  // 40 px rows are 80 bytes: three rows per band, seven bands for 20 rows.
  TEST_ASSERT_TRUE(jh_ili9341_fill_rect(&s_ili, 5u, 7u, 40u, 20u, FILL_COLOR));

  TEST_ASSERT_EQUAL_size_t(7u, s_panel.pixel_selections);
  TEST_ASSERT_EQUAL_size_t(240u, s_panel.max_select_pixel_bytes);
  fill_expected(40u, 20u, false);
  TEST_ASSERT_TRUE(
      panel_model_area_equals(&s_panel, 5u, 7u, 40u, 20u, s_expected));
  TEST_ASSERT_EQUAL_HEX16(0u, s_panel.pixels[7][4]);
  TEST_ASSERT_EQUAL_HEX16(0u, s_panel.pixels[7][45]);
  TEST_ASSERT_EQUAL_HEX16(0u, s_panel.pixels[27][5]);
  expect_bus_released();
}

void test_ili9341_bitmap_bands_keep_every_pixel_in_place(void) {
  start_ili9341();
  fill_expected(40u, 20u, true);

  TEST_ASSERT_TRUE(
      jh_ili9341_draw_rgb_bitmap(&s_ili, 3u, 2u, s_expected, 40u, 20u));

  TEST_ASSERT_EQUAL_size_t(7u, s_panel.pixel_selections);
  TEST_ASSERT_TRUE(
      panel_model_area_equals(&s_panel, 3u, 2u, 40u, 20u, s_expected));
  expect_bus_released();
}

void test_ili9341_row_wider_than_the_limit_is_one_band(void) {
  start_ili9341();

  // 160 px are 320 bytes: one whole row per band, above the limit.
  TEST_ASSERT_TRUE(jh_ili9341_fill_rect(&s_ili, 0u, 0u, 160u, 3u, FILL_COLOR));

  TEST_ASSERT_EQUAL_size_t(3u, s_panel.pixel_selections);
  TEST_ASSERT_EQUAL_size_t(320u, s_panel.max_select_pixel_bytes);
  expect_bus_released();
}

void test_ili9341_failed_band_stops_the_area_and_releases_the_bus(void) {
  start_ili9341();
  hal_mock_spi_fail_next_dma_write(0u, true);
  hal_mock_spi_fail_next_write(0u, true);

  TEST_ASSERT_FALSE(jh_ili9341_fill_rect(&s_ili, 0u, 0u, 40u, 20u, FILL_COLOR));

  TEST_ASSERT_LESS_THAN_size_t(7u, s_panel.pixel_selections);
  expect_bus_released();
}

void test_st7789_fill_and_bitmap_are_split_into_bands(void) {
  start_st7789();
  const uint16_t x = (uint16_t)(4u + s_st.x_start);
  const uint16_t y = (uint16_t)(6u + s_st.y_start);

  TEST_ASSERT_TRUE(jh_st77xx_fill_rect(&s_st, 4u, 6u, 40u, 20u, FILL_COLOR));
  TEST_ASSERT_EQUAL_size_t(7u, s_panel.pixel_selections);
  fill_expected(40u, 20u, false);
  TEST_ASSERT_TRUE(
      panel_model_area_equals(&s_panel, x, y, 40u, 20u, s_expected));

  fill_expected(40u, 20u, true);
  TEST_ASSERT_TRUE(
      jh_st77xx_draw_rgb_bitmap(&s_st, 4u, 6u, s_expected, 40u, 20u));
  TEST_ASSERT_EQUAL_size_t(14u, s_panel.pixel_selections);
  TEST_ASSERT_EQUAL_size_t(240u, s_panel.max_select_pixel_bytes);
  TEST_ASSERT_TRUE(
      panel_model_area_equals(&s_panel, x, y, 40u, 20u, s_expected));
  expect_bus_released();
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ili9341_fill_rect_sends_bands_within_the_limit);
  RUN_TEST(test_ili9341_bitmap_bands_keep_every_pixel_in_place);
  RUN_TEST(test_ili9341_row_wider_than_the_limit_is_one_band);
  RUN_TEST(test_ili9341_failed_band_stops_the_area_and_releases_the_bus);
  RUN_TEST(test_st7789_fill_and_bitmap_are_split_into_bands);
  return UNITY_END();
}
