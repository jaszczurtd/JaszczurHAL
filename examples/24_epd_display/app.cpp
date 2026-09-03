/**
 * @file app.cpp
 * @brief SSD1681 monochrome e-paper example using the raw display facade.
 */

#include <JaszczurHAL.h>
#include <hal/core/hal_app.h>
#include <hal/serial/hal_serial.h>

#include <string.h>

#if HAL_TARGET_IS_RP
#define EPD_SPI_MISO 16u
#define EPD_SPI_MOSI 19u
#define EPD_SPI_SCK 18u
#define EPD_CS 17
#define EPD_DC 20
#define EPD_RST 21
#define EPD_BUSY 22
#elif HAL_TARGET_IS_STM32G474
/* STM32 pin id = port * 16 + pin: SPI1 PA6/PA7/PA5. */
#define EPD_SPI_MISO 6u
#define EPD_SPI_MOSI 7u
#define EPD_SPI_SCK 5u
#define EPD_CS 22u
#define EPD_DC 39u
#define EPD_RST 9u
#define EPD_BUSY 8u
#else
#define EPD_SPI_MISO 6u
#define EPD_SPI_MOSI 7u
#define EPD_SPI_SCK 5u
#define EPD_CS 4
#define EPD_DC 3
#define EPD_RST 2
#define EPD_BUSY 1
#endif

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

  hal_status_t status =
      hal_spi_init(0u, EPD_SPI_MISO, EPD_SPI_MOSI, EPD_SPI_SCK);
  if (status != HAL_OK) {
    derr("SPI init failed: %s", hal_status_to_string(status));
    return;
  }

  hal_display_ssd16xx_config_t config = {};
  config.controller = HAL_DISPLAY_SSD16XX_SSD1681;
  config.transport.bus = 0u;
  config.transport.cs_pin = EPD_CS;
  config.transport.dc_pin = EPD_DC;
  config.transport.rst_pin = EPD_RST;
  config.transport.busy_pin = EPD_BUSY;
  config.transport.clock_hz = 4000000u;
  config.transport.busy_timeout_ms = 30000u;
  config.transport.busy_active_high = true;
  config.width = EPD_WIDTH;
  config.height = EPD_HEIGHT;
  config.rotation = HAL_DISPLAY_ROTATION_0;
  /* No LUT profile: use the controller OTP waveform at the default 25 C. */

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
