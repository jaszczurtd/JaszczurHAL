#include "hal/display/hal_display.h"
#include "utils/unity.h"

void hal_mock_spi_reset(void);
size_t hal_mock_spi_get_tx(uint8_t bus, uint8_t *out, size_t max_len);
bool hal_mock_spi_transaction_active(uint8_t bus);
int hal_mock_spi_get_lock_depth(uint8_t bus);

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

static void assert_padded_write_matches_packed(uint16_t x, uint16_t y,
                                               uint16_t width, uint16_t height,
                                               uint16_t pitch,
                                               bool incomplete) {
  // Two byte rows/pages, with two sentinel bytes between them.
  const uint8_t packed[] = {0xA1u, 0xB2u, 0xC3u, 0xD4u};
  const uint8_t padded[] = {0xA1u, 0xB2u, 0xEEu, 0xEEu, 0xC3u, 0xD4u};
  hal_display_buffer_desc_t desc = {HAL_DISPLAY_PIXEL_FORMAT_MONO10,
                                    width,
                                    width,
                                    height,
                                    sizeof(packed),
                                    incomplete};
  uint8_t expected[512] = {};
  uint8_t actual[512] = {};
  hal_mock_spi_reset();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_write_raw_ex(x, y, &desc, packed));
  const size_t expected_size =
      hal_mock_spi_get_tx(0u, expected, sizeof(expected));
  TEST_ASSERT_GREATER_THAN_size_t(sizeof(packed), expected_size);
  TEST_ASSERT_LESS_THAN_size_t(sizeof(expected), expected_size);
  // Return to the same full-profile/pending state as a fresh initialization.
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_suspend_ex());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_resume_ex());
  hal_mock_spi_reset();
  desc.pitch = pitch;
  desc.buf_size = sizeof(padded);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_write_raw_ex(x, y, &desc, padded));
  TEST_ASSERT_EQUAL_size_t(expected_size,
                           hal_mock_spi_get_tx(0u, actual, sizeof(actual)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, expected_size);
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0u));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));

  hal_mock_spi_reset();
  --desc.buf_size;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_display_write_raw_ex(x, y, &desc, padded));
  ++desc.buf_size;
  desc.pitch = (uint16_t)(width - 1u);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_display_write_raw_ex(x, y, &desc, padded));
  desc.pitch = pitch;
  hal_display_capabilities_t caps = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_get_capabilities_ex(&caps));
  if ((caps.screen_info & HAL_DISPLAY_SCREEN_INFO_MONO_VTILED) != 0u) {
    --desc.height;
    TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                          hal_display_write_raw_ex(x, y, &desc, padded));
    ++desc.height;
    TEST_ASSERT_EQUAL_INT(
        HAL_EINVAL,
        hal_display_write_raw_ex(x, (uint16_t)(y + 1u), &desc, padded));
  } else {
    ++desc.pitch;
    TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                          hal_display_write_raw_ex(x, y, &desc, padded));
    desc.pitch = pitch;
    TEST_ASSERT_EQUAL_INT(
        HAL_EINVAL,
        hal_display_write_raw_ex((uint16_t)(x + 1u), y, &desc, padded));
  }
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_display_write_raw_ex(caps.width, y, &desc, padded));
  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_spi_get_tx(0u, actual, sizeof(actual)));
  // No final padding is needed for a single byte row/page, even at maximum
  // stride.
  desc.height =
      (caps.screen_info & HAL_DISPLAY_SCREEN_INFO_MONO_VTILED) != 0u ? 8u : 1u;
  desc.pitch = (caps.screen_info & HAL_DISPLAY_SCREEN_INFO_MONO_VTILED) != 0u
                   ? UINT16_MAX
                   : (uint16_t)(UINT16_MAX & ~7u);
  desc.buf_size = 2u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_write_raw_ex(x, y, &desc, padded));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_suspend_ex());
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        hal_display_write_raw_ex(x, y, &desc, padded));
}

void test_ssd16xx_padded_pitch_all_rotations_and_refresh_modes(void) {
  hal_display_ssd16xx_config_t config = {};
  config.controller = HAL_DISPLAY_SSD16XX_SSD1680;
  config.transport = epd_transport();
  config.width = 24u;
  config.height = 32u;
  const uint8_t lut[] = {0x01u, 0x02u};
  hal_display_ssd16xx_profile_t partial = {};
  partial.lut = {lut, sizeof(lut)};
  for (unsigned profile = 0u; profile < 2u; ++profile) {
    config.partial_profile = profile == 0u ? NULL : &partial;
    for (unsigned rotation = 0u; rotation < 4u; ++rotation) {
      config.rotation = (hal_display_rotation_t)rotation;
      for (unsigned incomplete = 0u; incomplete < 2u; ++incomplete) {
        TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_ssd16xx_ex(&config));
        const bool vertical = (rotation & 1u) == 0u;
        assert_padded_write_matches_packed(
            vertical ? 3u : 8u, vertical ? 8u : 3u, vertical ? 2u : 16u,
            vertical ? 16u : 2u, vertical ? 4u : 32u, incomplete != 0u);
      }
    }
  }
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

void test_uc81xx_padded_pitch_preserves_profiles_windows_and_refresh(void) {
  hal_display_uc81xx_config_t config = {};
  config.controller = HAL_DISPLAY_UC81XX_UC8175;
  config.transport = epd_transport();
  config.width = 32u;
  config.height = 16u;
  hal_display_uc81xx_profile_t partial = {};
  partial.pll = 0x3Au;
  partial.override_pll = true;
  for (unsigned controller = 0u; controller < 4u; ++controller) {
    config.controller = (hal_display_uc81xx_controller_t)controller;
    for (unsigned profile = 0u; profile < 2u; ++profile) {
      config.partial_profile = profile == 0u ? NULL : &partial;
      for (unsigned incomplete = 0u; incomplete < 2u; ++incomplete) {
        TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_uc81xx_ex(&config));
        assert_padded_write_matches_packed(8u, 3u, 16u, 2u, 32u,
                                           incomplete != 0u);
      }
    }
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ssd16xx_facade_reports_epd_layout_and_flushes);
  RUN_TEST(test_ssd16xx_padded_pitch_all_rotations_and_refresh_modes);
  RUN_TEST(test_uc81xx_facade_reports_horizontal_packing);
  RUN_TEST(test_uc81xx_padded_pitch_preserves_profiles_windows_and_refresh);
  return UNITY_END();
}
