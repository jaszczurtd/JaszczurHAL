/**
 * @file app.cpp
 * @brief Combined SSD1306 OLED and HD44780 character LCD example.
 *
 * Both displays are initialized independently.  This keeps the example useful
 * with either display attached while compiling both portable display paths in
 * one firmware project.
 */

#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/display/hal_display.h>
#include <hal/display/hal_hd44780.h>
#include <hal/i2c/hal_i2c.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

#include <new>

#if HAL_TARGET_IS_RP
#define EXAMPLE_I2C_SDA 4u
#define EXAMPLE_I2C_SCL 5u
#define EXAMPLE_LCD_RS 12u
#define EXAMPLE_LCD_EN 11u
#define EXAMPLE_LCD_D4 10u
#define EXAMPLE_LCD_D5 9u
#define EXAMPLE_LCD_D6 8u
#define EXAMPLE_LCD_D7 7u
#else
/* STM32 pin id = port * 16 + pin: PB9/PB8 and PC0..PC5. */
#define EXAMPLE_I2C_SDA 25u
#define EXAMPLE_I2C_SCL 24u
#define EXAMPLE_LCD_RS 32u
#define EXAMPLE_LCD_EN 33u
#define EXAMPLE_LCD_D4 34u
#define EXAMPLE_LCD_D5 35u
#define EXAMPLE_LCD_D6 36u
#define EXAMPLE_LCD_D7 37u
#endif

static constexpr int OLED_WIDTH = 128;
static constexpr int OLED_HEIGHT = 64;
static constexpr uint8_t OLED_I2C_ADDR = 0x3cu;

alignas(HD44780) static unsigned char s_lcd_storage[sizeof(HD44780)];
static HD44780 *s_lcd = nullptr;
static bool s_oled_ready = false;
static uint32_t s_last_update_ms = 0u;
static uint32_t s_seconds = 0u;

static void draw_oled_layout(void) {
  hal_display_fill_screen(HAL_COLOR_BLACK);
  hal_display_set_default_font_with_pos_and_color(0, 0, HAL_COLOR_WHITE);
  hal_display_print("JaszczurHAL OLED");
  hal_display_draw_line(0, 12, OLED_WIDTH - 1, 12, HAL_COLOR_WHITE);
  hal_display_draw_rect(0, 18, 60, 30, HAL_COLOR_WHITE);
  hal_display_fill_circle(96, 33, 14, HAL_COLOR_WHITE);
  hal_display_set_cursor(0, 52);
  hal_display_print("seconds:");
  hal_display_flush();
}

static void update_oled(uint32_t seconds) {
  char text[16] = {};
  hal_display_prepare_text(text, sizeof(text), "%lu", (unsigned long)seconds);
  hal_display_fill_rect(64, 50, OLED_WIDTH - 64, 14, HAL_COLOR_BLACK);
  hal_display_set_cursor(64, 52);
  hal_display_set_text_color(HAL_COLOR_WHITE);
  hal_display_print(text);
  hal_display_flush();
}

void app_start(void) {
  hal_debug_init_default();
  deb("");
  deb("=== JaszczurHAL OLED + LCD example ===");

  hal_i2c_init_bus(0u, EXAMPLE_I2C_SDA, EXAMPLE_I2C_SCL, HAL_I2C_CLOCK_FAST_HZ);

  s_oled_ready =
      hal_display_init_ssd1306_i2c(OLED_WIDTH, OLED_HEIGHT, OLED_I2C_ADDR, -1,
                                   HAL_DISPLAY_VCC_SWITCHCAP, true);
  if (s_oled_ready) {
    s_oled_ready = hal_display_configure(
        OLED_WIDTH, OLED_HEIGHT, HAL_DISPLAY_ROTATION(0),
        HAL_DISPLAY_INVERT_OFF, HAL_DISPLAY_COLOR_ORDER_RGB);
  }
  if (s_oled_ready) {
    draw_oled_layout();
    update_oled(0u);
    deb("SSD1306 ready");
  } else {
    derr("SSD1306 init failed; continuing with HD44780");
  }

  s_lcd = new (s_lcd_storage)
      HD44780(EXAMPLE_LCD_RS, EXAMPLE_LCD_EN, EXAMPLE_LCD_D4, EXAMPLE_LCD_D5,
              EXAMPLE_LCD_D6, EXAMPLE_LCD_D7);
  s_lcd->begin(16u, 2u);
  s_lcd->clear();
  s_lcd->print("JaszczurHAL");
  s_lcd->setCursor(0u, 1u);
  s_lcd->print("LCD ready");
  deb("HD44780 initialized");
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  if ((now - s_last_update_ms) < 1000u) {
    hal_delay_ms(10u);
    return;
  }
  s_last_update_ms = now;
  ++s_seconds;

  if (s_oled_ready) {
    update_oled(s_seconds);
  }
  if (s_lcd != nullptr) {
    char line[17] = {};
    hal_display_prepare_text(line, sizeof(line), "t=%-13lus",
                             (unsigned long)s_seconds);
    s_lcd->setCursor(0u, 1u);
    s_lcd->print(line);
  }
}
