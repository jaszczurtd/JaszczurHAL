/* Firmware of the device tree library builds: it blinks the status LED the
 * assembly selects, through the generated board header. */
#include <JaszczurHAL.h>
#include <hal/gpio/hal_gpio.h>

static bool s_led_on;

void app_start(void) {
#if defined(HAL_LED_BUILTIN)
  hal_gpio_set_mode(HAL_LED_BUILTIN, HAL_GPIO_OUTPUT);
#endif
}

void app_task0(void) {
  s_led_on = !s_led_on;
#if defined(HAL_LED_BUILTIN)
  hal_gpio_write(HAL_LED_BUILTIN, s_led_on);
#endif
}
