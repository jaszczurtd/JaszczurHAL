#include <hal/hal_app.h>
#include <hal/hal_gpio.h>
#include <hal/hal_serial.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>

void app_start(void) {
    hal_debug_init(115200, 0);
    hal_gpio_set_mode(LED_BUILTIN, HAL_GPIO_OUTPUT);
}

void app_task0(void) {
    hal_gpio_write(LED_BUILTIN, true);
    hal_delay_ms(200);
    hal_gpio_write(LED_BUILTIN, false);
    hal_delay_ms(200);
}
