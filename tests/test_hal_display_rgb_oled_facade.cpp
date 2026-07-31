#include "hal/hal_display.h"
#include "utils/unity.h"

void hal_mock_spi_reset(void);
uint32_t hal_mock_spi_get_transfer_count(uint8_t bus);
int hal_mock_spi_get_lock_depth(uint8_t bus);
bool hal_mock_spi_transaction_active(uint8_t bus);

void setUp(void) { hal_mock_spi_reset(); }
void tearDown(void) {}

static hal_display_rgb_oled_config_t rgb_config(void) {
  hal_display_rgb_oled_config_t config = {};
  config.controller = HAL_DISPLAY_RGB_OLED_SSD1331;
  config.bus = 0u;
  config.cs_pin = 10;
  config.dc_pin = 11;
  config.rst_pin = -1;
  config.width = 96u;
  config.height = 64u;
  return config;
}

void test_rgb_oled_is_exposed_as_immediate_rgb_backend(void) {
  const hal_display_rgb_oled_config_t config = rgb_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_rgb_oled_ex(&config));

  hal_display_capabilities_t caps = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_get_capabilities_ex(&caps));
  TEST_ASSERT_EQUAL_UINT16(96u, caps.width);
  TEST_ASSERT_EQUAL_UINT16(64u, caps.height);
  TEST_ASSERT_BITS_HIGH(HAL_DISPLAY_CAP_RAW_WRITE, caps.flags);
  TEST_ASSERT_BITS_HIGH(HAL_DISPLAY_CAP_STREAM_WRITE, caps.flags);
  TEST_ASSERT_BITS_HIGH(HAL_DISPLAY_CAP_LEGACY_GFX, caps.flags);
  TEST_ASSERT_BITS_LOW(HAL_DISPLAY_CAP_DMA_WRITE, caps.flags);
  TEST_ASSERT_EQUAL_UINT8(HAL_DISPLAY_ROTATION_MASK(HAL_DISPLAY_ROTATION_0),
                          caps.supported_rotations);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_fill_rect_ex(0, 0, 2, 2, 0xF800u));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_display_set_rotation_ex(HAL_DISPLAY_ROTATION_90));
}

void test_rgb_oled_raw_write_dispatches_rgb565_bytes(void) {
  const hal_display_rgb_oled_config_t config = rgb_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_rgb_oled_ex(&config));
  const uint8_t pixels[] = {0xF8u, 0x00u, 0x07u, 0xE0u,
                            0x00u, 0x1Fu, 0xFFu, 0xFFu};
  const hal_display_buffer_desc_t desc = {
      HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE, 2u, 2u, 2u, sizeof(pixels), false};
  const uint32_t before = hal_mock_spi_get_transfer_count(0u);

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_display_write_raw_ex(3u, 4u, &desc, pixels));
  TEST_ASSERT_GREATER_THAN_UINT32(before, hal_mock_spi_get_transfer_count(0u));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_rgb_oled_is_exposed_as_immediate_rgb_backend);
  RUN_TEST(test_rgb_oled_raw_write_dispatches_rgb565_bytes);
  return UNITY_END();
}
