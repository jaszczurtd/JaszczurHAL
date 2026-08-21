#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_config.h"
#include "hal/system/hal_sync.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <new>
#include <stddef.h>
#include <stdint.h>

struct hal_mutex_impl_t {
  SemaphoreHandle_t handle;
};

namespace {

portMUX_TYPE s_critical_mux = portMUX_INITIALIZER_UNLOCKED;
uint32_t s_critical_depth[HAL_TARGET_CPU_CORES] = {};

bool esp32_in_isr(void) { return xPortInIsrContext() != pdFALSE; }

uint32_t current_core(void) {
  const BaseType_t core = xPortGetCoreID();
  HAL_ASSERT(core >= 0 && core < (BaseType_t)HAL_TARGET_CPU_CORES,
             "ESP32 FreeRTOS returned an invalid core id");
  return (core >= 0 && core < (BaseType_t)HAL_TARGET_CPU_CORES) ? (uint32_t)core
                                                                : 0u;
}

} // namespace

hal_mutex_t hal_mutex_create(void) {
  hal_mutex_impl_t *mutex = new (std::nothrow) hal_mutex_impl_t();
  HAL_ASSERT(mutex != nullptr, "hal_mutex_create: allocation failed");
  if (mutex == nullptr) {
    return nullptr;
  }

  mutex->handle = xSemaphoreCreateMutex();
  if (mutex->handle == nullptr) {
    delete mutex;
    HAL_ASSERT(false, "hal_mutex_create: FreeRTOS mutex allocation failed");
    return nullptr;
  }
  return mutex;
}

void hal_mutex_lock(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != nullptr, "hal_mutex_lock: mutex is NULL");
  if (mutex == nullptr) {
    return;
  }
  HAL_ASSERT(!esp32_in_isr(),
             "hal_mutex_lock: FreeRTOS mutex cannot be locked from ISR");
  if (esp32_in_isr()) {
    return;
  }

  const TickType_t wait_ticks =
      xTaskGetSchedulerState() == taskSCHEDULER_RUNNING ? portMAX_DELAY : 0u;
  const BaseType_t result = xSemaphoreTake(mutex->handle, wait_ticks);
  HAL_ASSERT(result == pdTRUE, "hal_mutex_lock: FreeRTOS mutex take failed");
  (void)result;
}

bool hal_mutex_try_lock(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != nullptr, "hal_mutex_try_lock: mutex is NULL");
  if (mutex == nullptr || esp32_in_isr()) {
    return false;
  }
  return xSemaphoreTake(mutex->handle, 0u) == pdTRUE;
}

void hal_mutex_unlock(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != nullptr, "hal_mutex_unlock: mutex is NULL");
  if (mutex == nullptr) {
    return;
  }
  HAL_ASSERT(!esp32_in_isr(),
             "hal_mutex_unlock: FreeRTOS mutex cannot be unlocked from ISR");
  if (esp32_in_isr()) {
    return;
  }

  const BaseType_t result = xSemaphoreGive(mutex->handle);
  HAL_ASSERT(result == pdTRUE, "hal_mutex_unlock: FreeRTOS mutex give failed");
  (void)result;
}

void hal_mutex_destroy(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != nullptr, "hal_mutex_destroy: mutex is NULL");
  if (mutex == nullptr) {
    return;
  }
  HAL_ASSERT(!esp32_in_isr(),
             "hal_mutex_destroy: FreeRTOS mutex cannot be destroyed from ISR");
  if (esp32_in_isr()) {
    return;
  }

  vSemaphoreDelete(mutex->handle);
  delete mutex;
}

extern "C" bool hal_esp32_critical_section_active(void) {
  return __atomic_load_n(&s_critical_depth[current_core()], __ATOMIC_ACQUIRE) !=
         0u;
}

void hal_critical_section_enter(void) {
  portENTER_CRITICAL_SAFE(&s_critical_mux);
  (void)__atomic_fetch_add(&s_critical_depth[current_core()], 1u,
                           __ATOMIC_RELAXED);
}

void hal_critical_section_exit(void) {
  const uint32_t core = current_core();
  if (__atomic_load_n(&s_critical_depth[core], __ATOMIC_RELAXED) == 0u) {
    return;
  }
  (void)__atomic_fetch_sub(&s_critical_depth[core], 1u, __ATOMIC_RELEASE);
  portEXIT_CRITICAL_SAFE(&s_critical_mux);
}

#endif // HAL_TARGET_IS_ESP32_FAMILY
