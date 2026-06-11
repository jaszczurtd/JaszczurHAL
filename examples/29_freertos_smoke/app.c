#include <FreeRTOS.h>
#include <task.h>

#include <hal/hal_app.h>
#include <hal/hal_gpio.h>
#include <hal/hal_serial.h>
#include <hal/hal_sync.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>

#if !defined(HAL_ENABLE_FREERTOS)
#error "29_freertos_smoke requires HAL_ENABLE_FREERTOS"
#endif

#if HAL_TARGET_IS_RP2040 && !defined(__FREERTOS)
#error                                                                         \
    "29_freertos_smoke on RP2040 requires arduino-pico FreeRTOS mode (__FREERTOS)"
#endif

static hal_mutex_t s_smoke_mutex;
static volatile uint32_t s_worker_ticks;

static void freertos_smoke_worker(void *arg) {
  (void)arg;

  for (;;) {
    hal_mutex_lock(s_smoke_mutex);
    ++s_worker_ticks;
    hal_mutex_unlock(s_smoke_mutex);

    hal_delay_ms(25);
  }
}

void app_start(void) {
  hal_debug_init(115200, 0);
  hal_gpio_set_mode(LED_BUILTIN, HAL_GPIO_OUTPUT);

  s_smoke_mutex = hal_mutex_create();
  if (s_smoke_mutex == NULL) {
    hal_derr("29_freertos_smoke: mutex allocation failed");
    return;
  }

  BaseType_t created = xTaskCreate(freertos_smoke_worker, "jh_smoke", 512, NULL,
                                   tskIDLE_PRIORITY + 1, NULL);
  if (created != pdPASS) {
    hal_derr("29_freertos_smoke: xTaskCreate failed");
  }
}

void app_task0(void) {
  if (s_smoke_mutex == NULL) {
    hal_delay_ms(100);
    return;
  }

  hal_mutex_lock(s_smoke_mutex);
  const uint32_t ticks = s_worker_ticks;
  hal_mutex_unlock(s_smoke_mutex);

  hal_gpio_write(LED_BUILTIN, (ticks & 1u) != 0u);
  hal_idle();
  hal_delay_ms(100);
}
