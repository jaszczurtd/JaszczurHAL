#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include <hal/hal_app.h>
#include <hal/hal_gpio.h>
#include <hal/hal_serial.h>
#include <hal/hal_sync.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools_c.h>

#if !defined(HAL_ENABLE_FREERTOS)
#error "29_freertos_smoke requires HAL_ENABLE_FREERTOS"
#endif

#if !defined(HAL_ENABLE_APP_TASK1)
#error "29_freertos_smoke requires HAL_ENABLE_APP_TASK1"
#endif

#if HAL_TARGET_IS_RP2040 && !defined(__FREERTOS)
#error                                                                         \
    "29_freertos_smoke on RP2040 requires arduino-pico FreeRTOS mode (__FREERTOS)"
#endif

static hal_mutex_t s_smoke_mutex;
static SemaphoreHandle_t s_table_mutex;
static uint32_t s_shared_table[6];
static volatile uint32_t s_task0_ticks;
static volatile uint32_t s_task1_ticks;
static uint32_t s_worker_updates[2];

typedef struct {
  const char *name;
  uint32_t worker_id;
  uint32_t slot_step;
  uint32_t increment;
  uint32_t delay_ms;
} smoke_worker_config_t;

static smoke_worker_config_t s_worker_a = {"worker_a", 0u, 1u, 1u, 250u};
static smoke_worker_config_t s_worker_b = {"worker_b", 1u, 2u, 10u, 370u};

static uint32_t smoke_table_sum_locked(void) {
  uint32_t sum = 0u;
  for (uint32_t i = 0u; i < 6u; ++i) {
    sum += s_shared_table[i];
  }
  return sum;
}

static void smoke_table_worker(void *arg) {
  const smoke_worker_config_t *cfg = (const smoke_worker_config_t *)arg;
  uint32_t iteration = 0u;

  for (;;) {
    uint32_t snapshot[6] = {};
    uint32_t slot = 0u;
    uint32_t neighbor = 0u;
    uint32_t before = 0u;
    uint32_t after = 0u;
    uint32_t sum = 0u;
    uint32_t updates_a = 0u;
    uint32_t updates_b = 0u;

    if (xSemaphoreTake(s_table_mutex, portMAX_DELAY) == pdTRUE) {
      slot = (iteration * cfg->slot_step + cfg->worker_id) % 6u;
      neighbor = s_shared_table[(slot + 1u) % 6u];
      before = s_shared_table[slot];
      after = before + cfg->increment + (neighbor & 1u);
      s_shared_table[slot] = after;
      ++s_worker_updates[cfg->worker_id];

      for (uint32_t i = 0u; i < 6u; ++i) {
        snapshot[i] = s_shared_table[i];
      }
      sum = smoke_table_sum_locked();
      updates_a = s_worker_updates[0];
      updates_b = s_worker_updates[1];

      xSemaphoreGive(s_table_mutex);

      deb("29_freertos_smoke: %s slot=%lu read=%lu neighbor=%lu write=%lu "
          "sum=%lu table=[%lu,%lu,%lu,%lu,%lu,%lu] updates=[%lu,%lu]",
          cfg->name, (unsigned long)slot, (unsigned long)before,
          (unsigned long)neighbor, (unsigned long)after, (unsigned long)sum,
          (unsigned long)snapshot[0], (unsigned long)snapshot[1],
          (unsigned long)snapshot[2], (unsigned long)snapshot[3],
          (unsigned long)snapshot[4], (unsigned long)snapshot[5],
          (unsigned long)updates_a, (unsigned long)updates_b);
    }

    ++iteration;
    hal_delay_ms(cfg->delay_ms);
  }
}

void app_start(void) {
  debugInit();
  hal_gpio_set_mode(LED_BUILTIN, HAL_GPIO_OUTPUT);

  s_smoke_mutex = hal_mutex_create();
  if (s_smoke_mutex == NULL) {
    derr("29_freertos_smoke: mutex allocation failed");
    return;
  }

  s_table_mutex = xSemaphoreCreateMutex();
  if (s_table_mutex == NULL) {
    derr("29_freertos_smoke: table mutex allocation failed");
    return;
  }

  BaseType_t created =
      xTaskCreate(smoke_table_worker, "jh_tbl_a", 768, (void *)&s_worker_a,
                  tskIDLE_PRIORITY + 1, NULL);
  if (created != pdPASS) {
    derr("29_freertos_smoke: xTaskCreate worker_a failed");
    return;
  }

  created = xTaskCreate(smoke_table_worker, "jh_tbl_b", 768,
                        (void *)&s_worker_b, tskIDLE_PRIORITY + 1, NULL);
  if (created != pdPASS) {
    derr("29_freertos_smoke: xTaskCreate worker_b failed");
    return;
  }

  deb("29_freertos_smoke: started two FreeRTOS table workers");
}

void app_task0(void) {
  if (s_smoke_mutex == NULL || s_table_mutex == NULL) {
    hal_delay_ms(100);
    return;
  }

  uint32_t table_sum = 0u;
  uint32_t task1_ticks = 0u;

  hal_mutex_lock(s_smoke_mutex);
  ++s_task0_ticks;
  task1_ticks = s_task1_ticks;
  hal_mutex_unlock(s_smoke_mutex);

  if (xSemaphoreTake(s_table_mutex, portMAX_DELAY) == pdTRUE) {
    table_sum = smoke_table_sum_locked();
    xSemaphoreGive(s_table_mutex);
  }

  hal_gpio_write(LED_BUILTIN, ((table_sum + task1_ticks) & 1u) != 0u);
  hal_debug_loop();
  hal_idle();
  hal_delay_ms(100);
}

void app_task1(void) {
  if (s_smoke_mutex == NULL || s_table_mutex == NULL) {
    hal_delay_ms(100);
    return;
  }

  hal_mutex_lock(s_smoke_mutex);
  ++s_task1_ticks;
  hal_mutex_unlock(s_smoke_mutex);

  hal_delay_ms(25);
}
