/**
 * @file app.c
 * @brief Display data on an SSD1306 OLED and an HD44780 character LCD.
 *
 * The displays are initialized independently, so either one can be omitted.
 * Both drivers are included in the same firmware build.
 */

#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/display/hal_display.h>
#include <hal/display/hal_hd44780.h>
#include <hal/i2c/hal_i2c.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

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
/* STM32 pin numbers use port * 16 + pin. This example uses PB9/PB8 and
 * PC0..PC5. */
#define EXAMPLE_I2C_SDA 25u
#define EXAMPLE_I2C_SCL 24u
#define EXAMPLE_LCD_RS 32u
#define EXAMPLE_LCD_EN 33u
#define EXAMPLE_LCD_D4 34u
#define EXAMPLE_LCD_D5 35u
#define EXAMPLE_LCD_D6 36u
#define EXAMPLE_LCD_D7 37u
#endif

static const int OLED_WIDTH = 128;
static const int OLED_HEIGHT = 64;
static const uint8_t OLED_I2C_ADDR = 0x3cu;

static hal_hd44780_t s_lcd = NULL;
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
  char text[16] = {0};
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

  hal_hd44780_config_t lcd_config = {0};
  lcd_config.rs_pin = EXAMPLE_LCD_RS;
  lcd_config.rw_pin = HAL_HD44780_PIN_NONE;
  lcd_config.enable_pin = EXAMPLE_LCD_EN;
  lcd_config.data_pins[0] = EXAMPLE_LCD_D4;
  lcd_config.data_pins[1] = EXAMPLE_LCD_D5;
  lcd_config.data_pins[2] = EXAMPLE_LCD_D6;
  lcd_config.data_pins[3] = EXAMPLE_LCD_D7;
  lcd_config.bus_width = HAL_HD44780_BUS_4_BIT;

  hal_status_t lcd_status = hal_hd44780_create(&lcd_config, &s_lcd);
  if (lcd_status == HAL_OK) {
    lcd_status = hal_hd44780_begin(s_lcd, 16u, 2u, HAL_HD44780_FONT_5X8);
  }
  if (lcd_status == HAL_OK) {
    lcd_status = hal_hd44780_clear(s_lcd);
  }
  if (lcd_status == HAL_OK) {
    lcd_status = hal_hd44780_print(s_lcd, "JaszczurHAL", NULL);
  }
  if (lcd_status == HAL_OK) {
    lcd_status = hal_hd44780_set_cursor(s_lcd, 0u, 1u);
  }
  if (lcd_status == HAL_OK) {
    lcd_status = hal_hd44780_print(s_lcd, "LCD ready", NULL);
  }
  if (lcd_status == HAL_OK) {
    deb("HD44780 initialized");
  } else {
    derr("HD44780 init failed: %s", hal_status_to_string(lcd_status));
    if (s_lcd != NULL) {
      (void)hal_hd44780_destroy(s_lcd);
      s_lcd = NULL;
    }
  }
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
  if (s_lcd != NULL) {
    char line[17] = {0};
    hal_display_prepare_text(line, sizeof(line), "t=%-13lus",
                             (unsigned long)s_seconds);
    if (hal_hd44780_set_cursor(s_lcd, 0u, 1u) == HAL_OK) {
      (void)hal_hd44780_print(s_lcd, line, NULL);
    }
  }
}
