/**
 * @file app.cpp
 * @brief SSD1306 monochrome OLED demo running on the shared hal_display
 *        backend (portable across RP2040 and STM32G474).
 *
 * The same source drives the panel through the shared GFX engine (jh_gfx) and
 * the shared SSD1306 I2C driver. Drawing is buffered: every frame is composed
 * in RAM and pushed to the panel with hal_display_flush().
 *
 * Wiring (I2C bus 0):
 *   RP2040     : SDA = GP4,  SCL = GP5  (Wire default)
 *   STM32G474  : SDA = PB9,  SCL = PB8  (I2C1 default)
 * Plus pull-ups to 3V3. Most 128x64 modules answer at 0x3C.
 * Console on the board default debug UART @ 115200.
 */

#include <hal/hal_app.h>
#include <hal/hal_display.h>
#include <hal/hal_i2c.h>
#include <hal/hal_system.h>
#include <tools.h>

static const int OLED_WIDTH = 128;
static const int OLED_HEIGHT = 64;
static const uint8_t OLED_I2C_ADDR = 0x3Cu;
static const int8_t OLED_RST_PIN = -1; /* not wired */

static const int LINE_HEIGHT = 16;

static uint32_t last_update_ms = 0;
static uint16_t counter = 0;

static void drawStaticLayout(void) {
  hal_display_fill_screen(HAL_COLOR_BLACK);

  hal_display_set_default_font_with_pos_and_color(0, 0, HAL_COLOR_WHITE);
  hal_display_print("JaszczurHAL OLED");

  hal_display_draw_line(0, 12, OLED_WIDTH - 1, 12, HAL_COLOR_WHITE);

  hal_display_draw_rect(0, 18, 60, 30, HAL_COLOR_WHITE);
  hal_display_fill_circle(96, 33, 14, HAL_COLOR_WHITE);

  hal_display_set_cursor(0, 52);
  hal_display_print("count:");
  hal_display_flush();
}

static void drawCounter(uint16_t value) {
  char text[12] = {0};
  hal_display_prepare_text(text, sizeof(text), "%u", value);

  /* Clear just the counter field, then redraw it. */
  hal_display_fill_rect(48, 50, OLED_WIDTH - 48, LINE_HEIGHT, HAL_COLOR_BLACK);
  hal_display_set_cursor(48, 52);
  hal_display_set_text_color(HAL_COLOR_WHITE);
  hal_display_print(text);
  hal_display_flush();
}

void app_start(void) {
  debugInit();

  hal_i2c_init(25u, 24u, HAL_I2C_CLOCK_FAST_HZ);

  if (!hal_display_init_ssd1306_i2c(OLED_WIDTH, OLED_HEIGHT, OLED_I2C_ADDR,
                                    OLED_RST_PIN, HAL_DISPLAY_VCC_SWITCHCAP,
                                    true)) {
    derr("SSD1306 init failed");
    return;
  }
  if (!hal_display_configure(OLED_WIDTH, OLED_HEIGHT, HAL_DISPLAY_ROTATION(0),
                             HAL_DISPLAY_INVERT_OFF,
                             HAL_DISPLAY_COLOR_ORDER_RGB)) {
    derr("display configure failed");
    return;
  }

  drawStaticLayout();
  drawCounter(counter);
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  if (now - last_update_ms < 500u) {
    return;
  }
  last_update_ms = now;

  drawCounter(counter++);
}
