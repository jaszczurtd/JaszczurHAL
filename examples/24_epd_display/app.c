/**
 * @file app.c
 * @brief Draw a monochrome test pattern on an SSD1681 e-paper display.
 */

#include <JaszczurHAL.h>
#include <hal/core/hal_app.h>
#include <hal/serial/hal_serial.h>

#include <string.h>

#define EPD_WIDTH 200u
#define EPD_HEIGHT 200u

static uint8_t s_frame[(EPD_WIDTH * EPD_HEIGHT) / 8u];

static void make_test_pattern(void) {
  memset(s_frame, 0xFF, sizeof(s_frame));
  for (size_t i = 0u; i < sizeof(s_frame); ++i) {
    if (((i / 25u) & 1u) != 0u) {
      s_frame[i] = 0xAAu;
    }
  }
}

void app_start(void) {
  hal_debug_init_default();
  deb("=== JaszczurHAL SSD1681 EPD example ===");

  /* Bus, pins and SPI clock come from device_tree.json. */
  hal_status_t status =
      hal_spi_init(JH_HW_BUS_SPI_INDEX, JH_HW_BUS_SPI_PIN_MISO,
                   JH_HW_BUS_SPI_PIN_MOSI, JH_HW_BUS_SPI_PIN_SCK);
  if (status != HAL_OK) {
    derr("SPI init failed: %s", hal_status_to_string(status));
    return;
  }

  hal_display_ssd16xx_config_t config = {0};
  config.controller = HAL_DISPLAY_SSD16XX_SSD1681;
  config.transport.bus = JH_HW_NODE_EPD_BUS_INDEX;
  config.transport.cs_pin = JH_HW_NODE_EPD_PIN_CS;
  config.transport.dc_pin = JH_HW_NODE_EPD_DC_PIN_OUT;
  config.transport.rst_pin = JH_HW_NODE_EPD_RESET_PIN_OUT;
  config.transport.busy_pin = JH_HW_NODE_EPD_BUSY_PIN_IN;
  config.transport.clock_hz = JH_HW_NODE_EPD_FREQUENCY_HZ;
  config.transport.spi_mode = JH_HW_NODE_EPD_SPI_MODE;
  config.transport.busy_timeout_ms = 30000u;
  config.transport.busy_active_high = !JH_HW_NODE_EPD_BUSY_PROP_ACTIVE_LOW;
  config.width = EPD_WIDTH;
  config.height = EPD_HEIGHT;
  config.rotation = HAL_DISPLAY_ROTATION_0;
  /* Use the controller OTP refresh waveform and assume 25 C; no custom LUT is
   * supplied. */

  status = hal_display_init_ssd16xx_ex(&config);
  if (status != HAL_OK) {
    derr("EPD init failed: %s", hal_status_to_string(status));
    return;
  }

  make_test_pattern();
  const hal_display_buffer_desc_t frame = {
      HAL_DISPLAY_PIXEL_FORMAT_MONO10,
      EPD_WIDTH,
      EPD_WIDTH,
      EPD_HEIGHT,
      sizeof(s_frame),
      false,
  };
  status = hal_display_write_raw_ex(0u, 0u, &frame, s_frame);
  if (status != HAL_OK) {
    derr("EPD frame write failed: %s", hal_status_to_string(status));
    return;
  }
  deb("EPD frame refreshed");
}

void app_task0(void) { hal_delay_ms(1000u); }
