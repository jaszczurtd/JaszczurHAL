#include "utils/unity.h"

#include "hal/display/drivers/ili9341_driver.h"
#include "hal/display/drivers/st77xx_driver.h"
#include "hal/impl/.mock/hal_mock.h"
#include "support/display_spi_panel_model.h"

/* Built with HAL_DISPLAY_SPI_MAX_TRANSFER_BYTES = 256: fills and bitmaps
 * release the bus after at most 256 bytes, including wide rows. */

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
  TEST_ASSERT_EQUAL_size_t(7u, s_panel.selections);
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
  TEST_ASSERT_EQUAL_size_t(7u, s_panel.selections);
  TEST_ASSERT_TRUE(
      panel_model_area_equals(&s_panel, 3u, 2u, 40u, 20u, s_expected));
  expect_bus_released();
}

void test_ili9341_wide_rows_release_the_bus_within_the_limit(void) {
  start_ili9341();

  // Each 160-pixel row must split into 128 and 32 pixels.
  TEST_ASSERT_TRUE(jh_ili9341_fill_rect(&s_ili, 3u, 2u, 160u, 3u, FILL_COLOR));

  TEST_ASSERT_EQUAL_size_t(6u, s_panel.pixel_selections);
  TEST_ASSERT_EQUAL_size_t(256u, s_panel.max_select_pixel_bytes);
  fill_expected(160u, 3u, false);
  TEST_ASSERT_TRUE(
      panel_model_area_equals(&s_panel, 3u, 2u, 160u, 3u, s_expected));
  fill_expected(160u, 3u, true);
  TEST_ASSERT_TRUE(
      jh_ili9341_draw_rgb_bitmap(&s_ili, 3u, 2u, s_expected, 160u, 3u));
  TEST_ASSERT_EQUAL_size_t(12u, s_panel.pixel_selections);
  TEST_ASSERT_EQUAL_size_t(256u, s_panel.max_select_pixel_bytes);
  TEST_ASSERT_TRUE(
      panel_model_area_equals(&s_panel, 3u, 2u, 160u, 3u, s_expected));
  TEST_ASSERT_EQUAL_HEX16(0u, s_panel.pixels[2][2]);
  TEST_ASSERT_EQUAL_HEX16(0u, s_panel.pixels[2][163]);
  TEST_ASSERT_EQUAL_HEX16(0u, s_panel.pixels[5][3]);
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
  TEST_ASSERT_EQUAL_size_t(7u, s_panel.selections);
  fill_expected(40u, 20u, false);
  TEST_ASSERT_TRUE(
      panel_model_area_equals(&s_panel, x, y, 40u, 20u, s_expected));

  fill_expected(40u, 20u, true);
  TEST_ASSERT_TRUE(
      jh_st77xx_draw_rgb_bitmap(&s_st, 4u, 6u, s_expected, 40u, 20u));
  TEST_ASSERT_EQUAL_size_t(14u, s_panel.pixel_selections);
  TEST_ASSERT_EQUAL_size_t(14u, s_panel.selections);
  TEST_ASSERT_EQUAL_size_t(240u, s_panel.max_select_pixel_bytes);
  TEST_ASSERT_TRUE(
      panel_model_area_equals(&s_panel, x, y, 40u, 20u, s_expected));
  expect_bus_released();
}

static uint8_t fail_pixels_exchange(void *user, uint8_t mosi) {
  const uint8_t value = panel_model_exchange(user, mosi);
  const panel_model_t *panel = (const panel_model_t *)user;
  if (!hal_mock_gpio_get_state(panel->dc_pin) && mosi == 0x2Cu) {
    hal_mock_spi_fail_next_dma_write(0u, true);
    hal_mock_spi_fail_next_write(0u, true);
  }
  return value;
}

void test_pixel_failure_after_the_window_releases_the_single_lease(void) {
  start_ili9341();
  static const hal_mock_spi_device_t device = {
      panel_model_select, fail_pixels_exchange, panel_model_deselect};
  hal_mock_spi_attach_device(0u, 10u, &device, &s_panel);
  TEST_ASSERT_FALSE(jh_ili9341_fill_rect(&s_ili, 0u, 0u, 40u, 2u, FILL_COLOR));
  TEST_ASSERT_EQUAL_size_t(1u, s_panel.selections);
  TEST_ASSERT_EQUAL_size_t(0u, s_panel.pixel_selections);
  expect_bus_released();
}

void test_st7789_wide_rows_keep_offsets_and_bound_the_single_lease(void) {
  start_st7789();
  fill_expected(129u, 3u, true);
  TEST_ASSERT_TRUE(
      jh_st77xx_draw_rgb_bitmap(&s_st, 4u, 6u, s_expected, 129u, 3u));
  TEST_ASSERT_EQUAL_size_t(6u, s_panel.selections);
  TEST_ASSERT_EQUAL_size_t(256u, s_panel.max_select_pixel_bytes);
  TEST_ASSERT_TRUE(panel_model_area_equals(
      &s_panel, (uint16_t)(4u + s_st.x_start), (uint16_t)(6u + s_st.y_start),
      129u, 3u, s_expected));
  expect_bus_released();
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ili9341_fill_rect_sends_bands_within_the_limit);
  RUN_TEST(test_ili9341_bitmap_bands_keep_every_pixel_in_place);
  RUN_TEST(test_ili9341_wide_rows_release_the_bus_within_the_limit);
  RUN_TEST(test_ili9341_failed_band_stops_the_area_and_releases_the_bus);
  RUN_TEST(test_st7789_fill_and_bitmap_are_split_into_bands);
  RUN_TEST(test_st7789_wide_rows_keep_offsets_and_bound_the_single_lease);
  RUN_TEST(test_pixel_failure_after_the_window_releases_the_single_lease);
  return UNITY_END();
}
