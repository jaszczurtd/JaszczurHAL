#include <FreeRTOS.h>
#include <task.h>

#include <hal/hal_app.h>
#include <hal/hal_gpio.h>
#include <hal/hal_serial.h>

#if !defined(HAL_ENABLE_FREERTOS)
#error "29_freertos_smoke requires HAL_ENABLE_FREERTOS"
#endif

#if !defined(__FREERTOS)
#error "29_freertos_smoke requires arduino-pico FreeRTOS mode (__FREERTOS)"
#endif

static volatile TickType_t s_last_tick;

void app_start(void) {
  hal_debug_init(115200, 0);
  hal_gpio_set_mode(LED_BUILTIN, HAL_GPIO_OUTPUT);
  s_last_tick = xTaskGetTickCount();
}

void app_task0(void) {
  const TickType_t now = xTaskGetTickCount();
  hal_gpio_write(LED_BUILTIN, (now & 1u) != 0u);
  s_last_tick = now;
  vTaskDelay(pdMS_TO_TICKS(100));
}
