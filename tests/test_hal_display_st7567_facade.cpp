#include "hal/display/hal_display.h"
#include "utils/unity.h"

void hal_mock_spi_reset(void);
size_t hal_mock_spi_get_tx(uint8_t bus, uint8_t *out, size_t max_len);
void hal_mock_spi_fail_next_write(uint8_t bus, bool fail);
bool hal_mock_spi_transaction_active(uint8_t bus);
int hal_mock_spi_get_lock_depth(uint8_t bus);
bool hal_mock_gpio_get_state(uint8_t pin);

void setUp(void) { hal_mock_spi_reset(); }
void tearDown(void) {}

static hal_display_st7567_config_t st7567_config(void) {
  hal_display_st7567_config_t config = {};
  config.bus_type = HAL_DISPLAY_ST7567_BUS_SPI;
  config.bus = 0u;
  config.rst_pin = -1;
  config.spi_cs_pin = 10;
  config.spi_dc_pin = 11;
  config.width = 128u;
  config.height = 64u;
  config.regulation_ratio = 4u;
  config.pixel_format = HAL_DISPLAY_PIXEL_FORMAT_MONO10;
  return config;
}

void test_st7567_capabilities_expose_page_layout(void) {
  const hal_display_st7567_config_t config = st7567_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_st7567_ex(&config));

  hal_display_capabilities_t caps = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_get_capabilities_ex(&caps));
  TEST_ASSERT_EQUAL_UINT16(128u, caps.width);
  TEST_ASSERT_EQUAL_UINT16(64u, caps.height);
  TEST_ASSERT_EQUAL_UINT16(8u, caps.y_alignment);
  TEST_ASSERT_EQUAL_UINT16(8u, caps.height_alignment);
  TEST_ASSERT_BITS_HIGH(HAL_DISPLAY_SCREEN_INFO_MONO_VTILED, caps.screen_info);
  TEST_ASSERT_BITS_HIGH(HAL_DISPLAY_CAP_RAW_WRITE, caps.flags);
  TEST_ASSERT_BITS_LOW(HAL_DISPLAY_CAP_LEGACY_GFX, caps.flags);
  TEST_ASSERT_EQUAL_INT(HAL_DISPLAY_PIXEL_FORMAT_MONO10,
                        caps.current_pixel_format);
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_display_set_font_ex(HAL_FONT_DEFAULT));
}

void test_st7567_raw_write_validates_format_and_page_alignment(void) {
  const hal_display_st7567_config_t config = st7567_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_st7567_ex(&config));
  const uint8_t page[8] = {0xAAu, 0x55u, 0xAAu, 0x55u,
                           0xAAu, 0x55u, 0xAAu, 0x55u};
  hal_display_buffer_desc_t desc = {
      HAL_DISPLAY_PIXEL_FORMAT_MONO10, 8u, 8u, 8u, sizeof(page), false};

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_write_raw_ex(4u, 8u, &desc, page));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_display_write_raw_ex(4u, 1u, &desc, page));
  desc.pixel_format = HAL_DISPLAY_PIXEL_FORMAT_MONO01;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_display_write_raw_ex(4u, 8u, &desc, page));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_display_set_pixel_format_ex(HAL_DISPLAY_PIXEL_FORMAT_MONO01));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_write_raw_ex(4u, 8u, &desc, page));
}

void test_st7567_raw_write_skips_padding_between_pages(void) {
  const hal_display_st7567_config_t config = st7567_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_st7567_ex(&config));
  // Two three-column pages with two padding bytes, omitted on the last page.
  const uint8_t pixels[] = {0xA1u, 0xB2u, 0xC3u, 0xEEu,
                            0xEEu, 0xD4u, 0xE5u, 0xF6u};
  hal_display_buffer_desc_t desc = {
      HAL_DISPLAY_PIXEL_FORMAT_MONO10, 5u, 3u, 16u, sizeof(pixels), false};
  const uint8_t expected[] = {0x04u, 0x10u, 0xB1u, 0xA1u, 0xB2u, 0xC3u,
                              0x04u, 0x10u, 0xB2u, 0xD4u, 0xE5u, 0xF6u};
  uint8_t tx[32] = {};
  for (unsigned polarity = 0u; polarity < 2u; ++polarity) {
    desc.pixel_format = polarity == 0u ? HAL_DISPLAY_PIXEL_FORMAT_MONO10
                                       : HAL_DISPLAY_PIXEL_FORMAT_MONO01;
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_display_set_pixel_format_ex(desc.pixel_format));
    hal_mock_spi_reset();
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_display_write_raw_ex(4u, 8u, &desc, pixels));
    TEST_ASSERT_EQUAL_size_t(sizeof(expected),
                             hal_mock_spi_get_tx(0u, tx, sizeof(tx)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, tx, sizeof(expected));
  }
  hal_mock_spi_reset();
  --desc.buf_size;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_display_write_raw_ex(4u, 8u, &desc, pixels));
  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_spi_get_tx(0u, tx, sizeof(tx)));
  ++desc.buf_size;
  desc.pitch = 2u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_display_write_raw_ex(4u, 8u, &desc, pixels));
  desc.pitch = 5u;
  desc.height = 15u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_display_write_raw_ex(4u, 8u, &desc, pixels));
  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_spi_get_tx(0u, tx, sizeof(tx)));

  // A single page never reads trailing padding, even at the largest pitch.
  desc.height = 8u;
  desc.pitch = UINT16_MAX;
  desc.buf_size = 3u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_display_write_raw_ex(4u, 8u, &desc, pixels));
  hal_mock_spi_fail_next_write(0u, true);
  TEST_ASSERT_EQUAL_INT(HAL_EIO,
                        hal_display_write_raw_ex(4u, 8u, &desc, pixels));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0u));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(10u));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_st7567_capabilities_expose_page_layout);
  RUN_TEST(test_st7567_raw_write_validates_format_and_page_alignment);
  RUN_TEST(test_st7567_raw_write_skips_padding_between_pages);
  return UNITY_END();
}
