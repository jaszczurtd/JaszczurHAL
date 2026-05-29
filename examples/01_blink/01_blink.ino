#include <JaszczurHAL.h>

static const uint8_t LED_PIN = 25;

void setup() {
  hal_debug_init(115200);
  hal_gpio_set_mode(LED_PIN, HAL_GPIO_OUTPUT);
}

void loop() {
  hal_gpio_write(LED_PIN, true);
  hal_delay_ms(200);
  hal_gpio_write(LED_PIN, false);
  hal_delay_ms(200);
}