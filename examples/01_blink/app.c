#include <hal/hal_app.h>
#include <hal/hal_gpio.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools_c.h>

void app_start(void) {
  debugInit();
  hal_gpio_set_mode(HAL_LED_BUILTIN, HAL_GPIO_OUTPUT);
}

void app_task0(void) {
  hal_gpio_write(HAL_LED_BUILTIN, true);
  hal_delay_ms(200);
  hal_gpio_write(HAL_LED_BUILTIN, false);
  hal_delay_ms(200);
}
