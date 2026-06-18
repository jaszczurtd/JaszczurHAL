/**
 * @file app.cpp
 * @brief HD44780-compatible 16x2 character LCD example over HAL GPIO.
 *
 * Wiring uses 4-bit parallel mode with RW tied to GND:
 *
 *   LCD RS -> EXAMPLE_LCD_RS
 *   LCD E  -> EXAMPLE_LCD_EN
 *   LCD D4 -> EXAMPLE_LCD_D4
 *   LCD D5 -> EXAMPLE_LCD_D5
 *   LCD D6 -> EXAMPLE_LCD_D6
 *   LCD D7 -> EXAMPLE_LCD_D7
 *
 * Add the usual contrast potentiometer on VO and connect the backlight through
 * a suitable resistor or module-specific backpack circuit.
 */

#include <hal/hal_app.h>
#include <hal/hal_hd44780.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools.h>

#include <new>

#if HAL_TARGET_IS_RP2040
#define EXAMPLE_LCD_RS 12u
#define EXAMPLE_LCD_EN 11u
#define EXAMPLE_LCD_D4 10u
#define EXAMPLE_LCD_D5 9u
#define EXAMPLE_LCD_D6 8u
#define EXAMPLE_LCD_D7 7u
#else
/* STM32 pin id = port * 16 + pin: PC0..PC5. */
#define EXAMPLE_LCD_RS 32u
#define EXAMPLE_LCD_EN 33u
#define EXAMPLE_LCD_D4 34u
#define EXAMPLE_LCD_D5 35u
#define EXAMPLE_LCD_D6 36u
#define EXAMPLE_LCD_D7 37u
#endif

alignas(HD44780) static unsigned char s_lcd_storage[sizeof(HD44780)];
static HD44780 *s_lcd = nullptr;
static uint32_t s_last_update_ms = 0u;
static uint32_t s_seconds = 0u;

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL HD44780 example ===");

  s_lcd = new (s_lcd_storage)
      HD44780(EXAMPLE_LCD_RS, EXAMPLE_LCD_EN, EXAMPLE_LCD_D4, EXAMPLE_LCD_D5,
              EXAMPLE_LCD_D6, EXAMPLE_LCD_D7);
  s_lcd->begin(16u, 2u);
  s_lcd->clear();
  s_lcd->print("JaszczurHAL");
  s_lcd->setCursor(0u, 1u);
  s_lcd->print("HD44780 ready");
}

void app_task0(void) {
  if (s_lcd == nullptr) {
    hal_delay_ms(1000u);
    return;
  }

  const uint32_t now = hal_millis();
  if ((now - s_last_update_ms) < 1000u) {
    return;
  }
  s_last_update_ms = now;

  s_lcd->setCursor(0u, 1u);
  s_lcd->print("t=");
  s_lcd->print(s_seconds++);
  s_lcd->print("s          ");
}
