#include "utils/unity.h"

#include "hal/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/drivers/display/ssd1306_driver.h"

#include <stdlib.h>
#include <string.h>

static jh_ssd1306_config_t make_config(uint16_t w, uint16_t h, uint8_t addr) {
  jh_ssd1306_config_t config = {};
  config.bus = 0u;
  config.i2c_addr = addr;
  config.width = w;
  config.height = h;
  config.rst_pin = -1;
  config.vccstate = JH_SSD1306_SWITCHCAPVCC;
  config.clock_hz = JH_SSD1306_DEFAULT_I2C_HZ;
  return config;
}

void setUp(void) {
  hal_mock_i2c_reset_write_log();
  hal_mock_spi_reset();
  hal_mock_gpio_trace_reset();
  hal_mock_set_millis(0u);
  hal_mock_set_micros(0u);
}

void tearDown(void) {}

/* Locate the first captured write frame that starts with the given control
 * byte and whose first payload byte matches. Returns the frame index or -1. */
static int find_command_frame(uint8_t command) {
  const int count = hal_mock_i2c_get_write_frame_count();
  for (int i = 0; i < count; ++i) {
    uint8_t frame[64] = {};
    const int len = hal_mock_i2c_get_write_frame(i, frame, sizeof(frame));
    if (len >= 2 && frame[0] == 0x00u && frame[1] == command) {
      return i;
    }
  }
  return -1;
}

static bool command_frame_is(int index, uint8_t command) {
  uint8_t frame[8] = {};
  const int len = hal_mock_i2c_get_write_frame(index, frame, sizeof(frame));
  return len >= 2 && frame[0] == 0x00u && frame[1] == command;
}

static bool find_command_value(uint8_t command, uint8_t value) {
  const int count = hal_mock_i2c_get_write_frame_count();
  for (int i = 0; i + 1 < count; ++i) {
    if (command_frame_is(i, command) && command_frame_is(i + 1, value)) {
      return true;
    }
  }
  return false;
}

void test_init_sends_ssd1306_command_sequence(void) {
  jh_ssd1306_t dev = {};
  const jh_ssd1306_config_t config = make_config(128u, 64u, 0x3Cu);

  TEST_ASSERT_TRUE(jh_ssd1306_init(&dev, &config));

  TEST_ASSERT_TRUE(dev.initialized);
  TEST_ASSERT_EQUAL_UINT16(128u, dev.width);
  TEST_ASSERT_EQUAL_UINT16(64u, dev.height);
  TEST_ASSERT_EQUAL_HEX8(0x3Cu, dev.i2c_addr);
  TEST_ASSERT_EQUAL_HEX8(0x3Cu, hal_mock_i2c_get_last_addr());
  TEST_ASSERT_EQUAL_UINT32(JH_SSD1306_DEFAULT_I2C_HZ,
                           hal_mock_i2c_get_clock_hz());

  /* First command frame must be DISPLAYOFF (0xAE), as a [0x00, cmd] pair. */
  uint8_t first[8] = {};
  const int first_len = hal_mock_i2c_get_write_frame(0, first, sizeof(first));
  TEST_ASSERT_GREATER_OR_EQUAL_INT(2, first_len);
  TEST_ASSERT_EQUAL_HEX8(0x00u, first[0]);
  TEST_ASSERT_EQUAL_HEX8(0xAEu, first[1]);

  /* The full init sequence ends with DISPLAYON (0xAF). */
  TEST_ASSERT_NOT_EQUAL(-1, find_command_frame(0xAFu));
  /* Charge-pump and multiplex commands are part of the sequence. */
  TEST_ASSERT_NOT_EQUAL(-1, find_command_frame(0x8Du));
  TEST_ASSERT_NOT_EQUAL(-1, find_command_frame(0xA8u));
}

void test_default_address_for_32px_panel(void) {
  jh_ssd1306_t dev = {};
  const jh_ssd1306_config_t config = make_config(128u, 32u, 0u);

  TEST_ASSERT_TRUE(jh_ssd1306_init(&dev, &config));
  /* Unspecified address resolves to 0x3C for 32px-tall panels. */
  TEST_ASSERT_EQUAL_HEX8(0x3Cu, dev.i2c_addr);
}

void test_buffer_size_matches_geometry(void) {
  jh_ssd1306_t dev = {};
  const jh_ssd1306_config_t config = make_config(128u, 64u, 0x3Cu);
  TEST_ASSERT_TRUE(jh_ssd1306_init(&dev, &config));

  TEST_ASSERT_EQUAL_UINT32(128u * 8u, (unsigned)jh_ssd1306_buffer_size(&dev));
}

void test_display_streams_full_framebuffer_as_data(void) {
  jh_ssd1306_t dev = {};
  const jh_ssd1306_config_t config = make_config(128u, 64u, 0x3Cu);
  TEST_ASSERT_TRUE(jh_ssd1306_init(&dev, &config));

  const size_t bytes = jh_ssd1306_buffer_size(&dev);
  uint8_t *buffer = (uint8_t *)malloc(bytes);
  TEST_ASSERT_NOT_NULL(buffer);
  memset(buffer, 0xA5, bytes);

  /* The mock truncates its frame log (32 frames x 8 bytes), so we count
   * transactions to confirm the full framebuffer was streamed: 6 address
   * setup commands plus ceil(bytes / chunk) data frames (chunk = 16). */
  hal_mock_i2c_reset_write_log();
  const uint32_t before = hal_i2c_get_transaction_count();
  TEST_ASSERT_TRUE(jh_ssd1306_display(&dev, buffer));
  const uint32_t frames = hal_i2c_get_transaction_count() - before;

  const uint32_t expected_data_frames = (uint32_t)((bytes + 15u) / 16u);
  TEST_ASSERT_EQUAL_UINT32(6u + expected_data_frames, frames);

  /* The page/column window is programmed before the pixel stream. */
  TEST_ASSERT_NOT_EQUAL(-1, find_command_frame(0x22u)); /* PAGEADDR   */
  TEST_ASSERT_NOT_EQUAL(-1, find_command_frame(0x21u)); /* COLUMNADDR */

  /* The first captured data frame carries the 0x40 control byte and pixels. */
  int data_idx = -1;
  const int count = hal_mock_i2c_get_write_frame_count();
  for (int i = 0; i < count; ++i) {
    uint8_t frame[8] = {};
    const int len = hal_mock_i2c_get_write_frame(i, frame, sizeof(frame));
    if (len >= 2 && frame[0] == 0x40u) {
      data_idx = i;
      TEST_ASSERT_EQUAL_HEX8(0xA5u, frame[1]);
      break;
    }
  }
  TEST_ASSERT_NOT_EQUAL(-1, data_idx);

  free(buffer);
}

void test_ssd1309_applies_vcom_display_and_address_offsets(void) {
  jh_ssd1306_t dev = {};
  jh_ssd1306_config_t config = make_config(128u, 64u, 0x3Cu);
  config.controller = JH_SSD1306_CONTROLLER_SSD1309;
  config.segment_offset = 2u;
  config.page_offset = 1u;
  config.display_offset = 3u;

  TEST_ASSERT_TRUE(jh_ssd1306_init(&dev, &config));
  TEST_ASSERT_EQUAL_INT(JH_SSD1306_CONTROLLER_SSD1309, dev.controller);
  TEST_ASSERT_TRUE(find_command_value(0xD3u, 0x03u)); /* display offset */
  TEST_ASSERT_TRUE(find_command_value(0xDBu, 0x34u)); /* SSD1309 VCOM */

  const size_t bytes = jh_ssd1306_buffer_size(&dev);
  uint8_t *buffer = (uint8_t *)malloc(bytes);
  TEST_ASSERT_NOT_NULL(buffer);
  memset(buffer, 0x5A, bytes);

  hal_mock_i2c_reset_write_log();
  TEST_ASSERT_TRUE(jh_ssd1306_display(&dev, buffer));
  TEST_ASSERT_TRUE(command_frame_is(0, 0x22u)); /* PAGEADDR */
  TEST_ASSERT_TRUE(command_frame_is(1, 0x01u));
  TEST_ASSERT_TRUE(command_frame_is(2, 0x08u));
  TEST_ASSERT_TRUE(command_frame_is(3, 0x21u)); /* COLUMNADDR */
  TEST_ASSERT_TRUE(command_frame_is(4, 0x02u));
  TEST_ASSERT_TRUE(command_frame_is(5, 0x81u));

  free(buffer);
}

void test_sh1106_uses_page_addressing_and_segment_offset(void) {
  jh_ssd1306_t dev = {};
  jh_ssd1306_config_t config = make_config(128u, 64u, 0x3Cu);
  config.controller = JH_SSD1306_CONTROLLER_SH1106;
  config.segment_offset = 2u;
  config.page_offset = 1u;

  TEST_ASSERT_TRUE(jh_ssd1306_init(&dev, &config));
  TEST_ASSERT_TRUE(find_command_value(0xADu, 0x8Bu)); /* SH1106 DCDC */
  TEST_ASSERT_TRUE(find_command_value(0xDBu, 0x35u)); /* SH/CH VCOM */

  const size_t bytes = jh_ssd1306_buffer_size(&dev);
  uint8_t *buffer = (uint8_t *)malloc(bytes);
  TEST_ASSERT_NOT_NULL(buffer);
  memset(buffer, 0x3C, bytes);

  hal_mock_i2c_reset_write_log();
  TEST_ASSERT_TRUE(jh_ssd1306_display(&dev, buffer));
  TEST_ASSERT_TRUE(command_frame_is(0, 0x02u)); /* lower column */
  TEST_ASSERT_TRUE(command_frame_is(1, 0x10u)); /* higher column */
  TEST_ASSERT_TRUE(command_frame_is(2, 0xB1u)); /* page 1 */
  TEST_ASSERT_EQUAL_INT(-1, find_command_frame(0x21u));
  TEST_ASSERT_EQUAL_INT(-1, find_command_frame(0x22u));

  uint8_t frame[8] = {};
  const int data_len = hal_mock_i2c_get_write_frame(3, frame, sizeof(frame));
  TEST_ASSERT_GREATER_OR_EQUAL_INT(2, data_len);
  TEST_ASSERT_EQUAL_HEX8(0x40u, frame[0]);
  TEST_ASSERT_EQUAL_HEX8(0x3Cu, frame[1]);

  free(buffer);
}

void test_invert_sends_invert_and_normal_commands(void) {
  jh_ssd1306_t dev = {};
  const jh_ssd1306_config_t config = make_config(128u, 64u, 0x3Cu);
  TEST_ASSERT_TRUE(jh_ssd1306_init(&dev, &config));

  hal_mock_i2c_reset_write_log();
  TEST_ASSERT_TRUE(jh_ssd1306_invert(&dev, true));
  TEST_ASSERT_NOT_EQUAL(-1, find_command_frame(0xA7u)); /* INVERTDISPLAY */

  hal_mock_i2c_reset_write_log();
  TEST_ASSERT_TRUE(jh_ssd1306_invert(&dev, false));
  TEST_ASSERT_NOT_EQUAL(-1, find_command_frame(0xA6u)); /* NORMALDISPLAY */
}

void test_orientation_contrast_suspend_and_resume(void) {
  jh_ssd1306_t dev = {};
  const jh_ssd1306_config_t config = make_config(128u, 64u, 0x3Cu);
  TEST_ASSERT_TRUE(jh_ssd1306_init(&dev, &config));

  hal_mock_i2c_reset_write_log();
  TEST_ASSERT_TRUE(
      jh_ssd1306_set_orientation(&dev, JH_SSD1306_ORIENTATION_ROTATED_180));
  TEST_ASSERT_NOT_EQUAL(-1, find_command_frame(0xA0u));
  TEST_ASSERT_NOT_EQUAL(-1, find_command_frame(0xC0u));

  hal_mock_i2c_reset_write_log();
  TEST_ASSERT_TRUE(jh_ssd1306_set_contrast(&dev, 0x44u));
  TEST_ASSERT_TRUE(find_command_value(0x81u, 0x44u));
  TEST_ASSERT_EQUAL_HEX8(0x44u, dev.contrast);

  hal_mock_i2c_reset_write_log();
  TEST_ASSERT_TRUE(jh_ssd1306_suspend(&dev));
  TEST_ASSERT_TRUE(dev.suspended);
  TEST_ASSERT_NOT_EQUAL(-1, find_command_frame(0xAEu));

  uint8_t pixel = 0u;
  TEST_ASSERT_FALSE(jh_ssd1306_display(&dev, &pixel));

  hal_mock_i2c_reset_write_log();
  TEST_ASSERT_TRUE(jh_ssd1306_resume(&dev));
  TEST_ASSERT_FALSE(dev.suspended);
  TEST_ASSERT_NOT_EQUAL(-1, find_command_frame(0xAFu));
}

void test_spi_transport_splits_command_setup_and_pixel_data(void) {
  jh_ssd1306_t dev = {};
  jh_ssd1306_config_t config = make_config(128u, 64u, 0u);
  config.controller = JH_SSD1306_CONTROLLER_SSD1315;
  config.bus_type = JH_SSD1306_BUS_SPI;
  config.bus = 0u;
  config.clock_hz = 0u;
  config.spi_dc_pin = 5;
  config.spi_cs_pin = 6;
  config.internal_iref = true;

  TEST_ASSERT_TRUE(jh_ssd1306_init(&dev, &config));
  TEST_ASSERT_EQUAL_INT(JH_SSD1306_BUS_SPI, dev.bus_type);
  TEST_ASSERT_TRUE(hal_mock_gpio_is_output(5u));
  TEST_ASSERT_TRUE(hal_mock_gpio_is_output(6u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(6u));
  TEST_ASSERT_EQUAL_UINT32(JH_SSD1306_DEFAULT_SPI_HZ,
                           hal_mock_spi_get_clock_hz(0u));

  uint8_t init_tx[96] = {};
  const size_t init_len = hal_mock_spi_get_tx(0u, init_tx, sizeof(init_tx));
  TEST_ASSERT_GREATER_THAN_UINT(0u, (unsigned)init_len);
  TEST_ASSERT_EQUAL_HEX8(0xAEu, init_tx[0]); /* DISPLAYOFF, no I2C prefix */

  const size_t bytes = jh_ssd1306_buffer_size(&dev);
  uint8_t *buffer = (uint8_t *)malloc(bytes);
  TEST_ASSERT_NOT_NULL(buffer);
  memset(buffer, 0x55, bytes);

  hal_mock_spi_reset();
  TEST_ASSERT_TRUE(jh_ssd1306_display(&dev, buffer));

  uint8_t tx[64] = {};
  const size_t tx_len = hal_mock_spi_get_tx(0u, tx, sizeof(tx));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT(22u, (unsigned)tx_len);
  TEST_ASSERT_EQUAL_HEX8(0x22u, tx[0]); /* PAGEADDR */
  TEST_ASSERT_EQUAL_HEX8(0x00u, tx[1]);
  TEST_ASSERT_EQUAL_HEX8(0x07u, tx[2]);
  TEST_ASSERT_EQUAL_HEX8(0x21u, tx[3]); /* COLUMNADDR */
  TEST_ASSERT_EQUAL_HEX8(0x00u, tx[4]);
  TEST_ASSERT_EQUAL_HEX8(0x7Fu, tx[5]);
  TEST_ASSERT_EQUAL_HEX8(0x55u, tx[6]); /* first data byte */
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(5u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(6u));

  free(buffer);
}

void test_rejects_invalid_arguments(void) {
  jh_ssd1306_t dev = {};
  const jh_ssd1306_config_t config = make_config(0u, 64u, 0x3Cu);
  TEST_ASSERT_FALSE(jh_ssd1306_init(&dev, &config));
  TEST_ASSERT_FALSE(jh_ssd1306_init(NULL, NULL));

  jh_ssd1306_t good = {};
  const jh_ssd1306_config_t good_config = make_config(128u, 64u, 0x3Cu);
  TEST_ASSERT_TRUE(jh_ssd1306_init(&good, &good_config));
  TEST_ASSERT_FALSE(jh_ssd1306_display(&good, NULL));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_sends_ssd1306_command_sequence);
  RUN_TEST(test_default_address_for_32px_panel);
  RUN_TEST(test_buffer_size_matches_geometry);
  RUN_TEST(test_display_streams_full_framebuffer_as_data);
  RUN_TEST(test_ssd1309_applies_vcom_display_and_address_offsets);
  RUN_TEST(test_sh1106_uses_page_addressing_and_segment_offset);
  RUN_TEST(test_invert_sends_invert_and_normal_commands);
  RUN_TEST(test_orientation_contrast_suspend_and_resume);
  RUN_TEST(test_spi_transport_splits_command_setup_and_pixel_data);
  RUN_TEST(test_rejects_invalid_arguments);
  return UNITY_END();
}
