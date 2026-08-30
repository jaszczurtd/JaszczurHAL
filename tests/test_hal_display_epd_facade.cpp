#include "hal/display/hal_display.h"
#include "utils/unity.h"

void hal_mock_spi_reset(void);

void setUp(void) { hal_mock_spi_reset(); }
void tearDown(void) {}

static hal_display_epd_spi_config_t epd_transport(void) {
  hal_display_epd_spi_config_t transport = {};
  transport.bus = 0u;
  transport.cs_pin = 10;
  transport.dc_pin = 11;
  transport.rst_pin = -1;
  transport.busy_pin = -1;
  transport.busy_active_high = true;
  return transport;
}

void test_ssd16xx_facade_reports_epd_layout_and_flushes(void) {
  hal_display_ssd16xx_config_t config = {};
  config.controller = HAL_DISPLAY_SSD16XX_SSD1680;
  config.transport = epd_transport();
  config.width = 16u;
  config.height = 16u;
  config.rotation = HAL_DISPLAY_ROTATION_0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_ssd16xx_ex(&config));

  hal_display_capabilities_t caps = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_get_capabilities_ex(&caps));
  TEST_ASSERT_BITS_HIGH(HAL_DISPLAY_SCREEN_INFO_EPD, caps.screen_info);
  TEST_ASSERT_BITS_HIGH(HAL_DISPLAY_SCREEN_INFO_MONO_MSB_FIRST,
                        caps.screen_info);
  TEST_ASSERT_BITS_HIGH(HAL_DISPLAY_SCREEN_INFO_MONO_VTILED, caps.screen_info);
  TEST_ASSERT_EQUAL_UINT16(8u, caps.y_alignment);
  TEST_ASSERT_EQUAL_INT(HAL_DISPLAY_PIXEL_FORMAT_MONO10,
                        caps.current_pixel_format);

  const uint8_t pixels[] = {0xAAu, 0x55u, 0xAAu, 0x55u};
  const hal_display_buffer_desc_t desc = {
      HAL_DISPLAY_PIXEL_FORMAT_MONO10, 4u, 4u, 8u, sizeof(pixels), true};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_display_write_raw_ex(0u, 8u, &desc, pixels));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_flush_ex());

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_display_set_rotation_ex(HAL_DISPLAY_ROTATION_90));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_get_capabilities_ex(&caps));
  TEST_ASSERT_EQUAL_UINT16(8u, caps.x_alignment);
  TEST_ASSERT_EQUAL_UINT16(8u, caps.width_alignment);
  TEST_ASSERT_BITS_LOW(HAL_DISPLAY_SCREEN_INFO_MONO_VTILED, caps.screen_info);

  const hal_display_buffer_desc_t rotated_desc = {
      HAL_DISPLAY_PIXEL_FORMAT_MONO10, 8u, 8u, 4u, sizeof(pixels), false};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_suspend_ex());
  TEST_ASSERT_EQUAL_INT(
      HAL_ESTATE, hal_display_write_raw_ex(0u, 0u, &rotated_desc, pixels));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_resume_ex());
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_display_write_raw_ex(0u, 0u, &rotated_desc, pixels));
}

void test_ssd16xx_raw_write_still_rejects_padded_pitch(void) {
  hal_display_ssd16xx_config_t config = {};
  config.controller = HAL_DISPLAY_SSD16XX_SSD1680;
  config.transport = epd_transport();
  config.width = 16u;
  config.height = 16u;
  config.rotation = HAL_DISPLAY_ROTATION_0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_ssd16xx_ex(&config));

  const uint8_t pixels[8] = {};
  /* Tiling flips with rotation (see hal_display_get_capabilities_ex), so a
   * single row-split strategy would be wrong for at least one orientation
   * until that is designed per-orientation; pitch > width must keep
   * failing on every rotation in the meantime. */
  const hal_display_buffer_desc_t desc = {
      HAL_DISPLAY_PIXEL_FORMAT_MONO10, 5u, 4u, 4u, sizeof(pixels), true};
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_display_write_raw_ex(0u, 8u, &desc, pixels));
}

void test_uc81xx_facade_reports_horizontal_packing(void) {
  hal_display_uc81xx_config_t config = {};
  config.controller = HAL_DISPLAY_UC81XX_UC8175;
  config.transport = epd_transport();
  config.width = 16u;
  config.height = 16u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_uc81xx_ex(&config));

  hal_display_capabilities_t caps = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_get_capabilities_ex(&caps));
  TEST_ASSERT_EQUAL_UINT16(8u, caps.x_alignment);
  TEST_ASSERT_EQUAL_UINT16(8u, caps.width_alignment);
  TEST_ASSERT_BITS_LOW(HAL_DISPLAY_SCREEN_INFO_MONO_VTILED, caps.screen_info);
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_display_set_pixel_format_ex(
                                              HAL_DISPLAY_PIXEL_FORMAT_MONO01));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_display_epd_refresh_ex(HAL_DISPLAY_EPD_REFRESH_FULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_suspend_ex());
  TEST_ASSERT_EQUAL_INT(
      HAL_ESTATE, hal_display_epd_refresh_ex(HAL_DISPLAY_EPD_REFRESH_FULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_resume_ex());
}

void test_uc81xx_raw_write_still_rejects_padded_pitch(void) {
  hal_display_uc81xx_config_t config = {};
  config.controller = HAL_DISPLAY_UC81XX_UC8175;
  config.transport = epd_transport();
  config.width = 16u;
  config.height = 16u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_uc81xx_ex(&config));

  const uint8_t pixels[8] = {};
  /* jh_uc81xx_write() re-applies panel profile/PTIN on every call; a
   * row-split loop would replay that once per row instead of once per
   * update, so pitch > width must keep failing until a lower-level
   * window+data primitive exists. */
  const hal_display_buffer_desc_t desc = {
      HAL_DISPLAY_PIXEL_FORMAT_MONO10, 16u, 8u, 8u, sizeof(pixels), true};
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_display_write_raw_ex(0u, 0u, &desc, pixels));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ssd16xx_facade_reports_epd_layout_and_flushes);
  RUN_TEST(test_ssd16xx_raw_write_still_rejects_padded_pitch);
  RUN_TEST(test_uc81xx_facade_reports_horizontal_packing);
  RUN_TEST(test_uc81xx_raw_write_still_rejects_padded_pitch);
  return UNITY_END();
}
