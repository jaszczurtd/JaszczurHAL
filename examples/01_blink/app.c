#include <hal/hal_app.h>
#include <hal/hal_gpio.h>
#include <hal/hal_serial.h>
#include <hal/hal_system.h>

/* Board-portable LED pin. Override with -DBLINK_LED_PIN=... if needed. */
#ifndef BLINK_LED_PIN
#  if defined(JH_STM32G474_HW)
#    define BLINK_LED_PIN 5u   /* Nucleo-G474RE LD2 = PA5 */
#  else
#    define BLINK_LED_PIN 25u  /* Raspberry Pi Pico onboard LED = GP25 */
#  endif
#endif

static const uint8_t LED_PIN = BLINK_LED_PIN;

void app_start(void) {
    hal_debug_init(115200, 0);
    hal_gpio_set_mode(LED_PIN, HAL_GPIO_OUTPUT);
}

void app_task0(void) {
    hal_gpio_write(LED_PIN, true);
    hal_delay_ms(200);
    hal_gpio_write(LED_PIN, false);
    hal_delay_ms(200);
}
