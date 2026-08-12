#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP
#include "hal/core/hal_config.h"
#include "hal/system/hal_sync.h"
#include <hardware/sync.h>
#include <pico/platform.h>

#if defined(HAL_ENABLE_FREERTOS) && defined(__FREERTOS)
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>
#else
#include <pico/mutex.h>
#endif

#if defined(HAL_ENABLE_FREERTOS) && defined(__FREERTOS)
#define JH_RP_HAL_SYNC_FREERTOS 1
#else
#define JH_RP_HAL_SYNC_FREERTOS 0
#endif

struct hal_mutex_impl_t {
#if JH_RP_HAL_SYNC_FREERTOS
  SemaphoreHandle_t handle;
#else
  mutex_t mtx;
#endif
};

hal_mutex_t hal_mutex_create(void) {
  hal_mutex_impl_t *m = new hal_mutex_impl_t();
  HAL_ASSERT(m != NULL, "hal_mutex_create: allocation failed");
#if JH_RP_HAL_SYNC_FREERTOS
  if (m == NULL) {
    return NULL;
  }

  m->handle = xSemaphoreCreateMutex();
  if (m->handle == NULL) {
    delete m;
    HAL_ASSERT(false, "hal_mutex_create: FreeRTOS mutex allocation failed");
    return NULL;
  }
#else
  mutex_init(&m->mtx);
#endif
  return m;
}

void hal_mutex_lock(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_lock: mutex is NULL");
  if (mutex == NULL) {
    return;
  }

#if JH_RP_HAL_SYNC_FREERTOS
  HAL_ASSERT(!portCHECK_IF_IN_ISR(),
             "hal_mutex_lock: FreeRTOS mutex cannot be locked from ISR");
  if (portCHECK_IF_IN_ISR()) {
    return;
  }

  const BaseType_t scheduler_state = xTaskGetSchedulerState();
  const TickType_t wait_ticks =
      (scheduler_state == taskSCHEDULER_RUNNING) ? portMAX_DELAY : 0u;
  const BaseType_t ok = xSemaphoreTake(mutex->handle, wait_ticks);
  HAL_ASSERT(ok == pdTRUE, "hal_mutex_lock: FreeRTOS mutex take failed");
  (void)ok;
#else
  mutex_enter_blocking(&mutex->mtx);
#endif
}

bool hal_mutex_try_lock(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_try_lock: mutex is NULL");
  if (mutex == NULL) {
    return false;
  }

#if JH_RP_HAL_SYNC_FREERTOS
  if (portCHECK_IF_IN_ISR()) {
    return false;
  }
  return xSemaphoreTake(mutex->handle, 0u) == pdTRUE;
#else
  return mutex_try_enter(&mutex->mtx, NULL);
#endif
}

void hal_mutex_unlock(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_unlock: mutex is NULL");
  if (mutex == NULL) {
    return;
  }

#if JH_RP_HAL_SYNC_FREERTOS
  HAL_ASSERT(!portCHECK_IF_IN_ISR(),
             "hal_mutex_unlock: FreeRTOS mutex cannot be unlocked from ISR");
  if (portCHECK_IF_IN_ISR()) {
    return;
  }

  const BaseType_t ok = xSemaphoreGive(mutex->handle);
  HAL_ASSERT(ok == pdTRUE, "hal_mutex_unlock: FreeRTOS mutex give failed");
  (void)ok;
#else
  mutex_exit(&mutex->mtx);
#endif
}

void hal_mutex_destroy(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_destroy: mutex is NULL");
  if (mutex == NULL) {
    return;
  }

#if JH_RP_HAL_SYNC_FREERTOS
  vSemaphoreDelete(mutex->handle);
#endif
  delete mutex;
}

/* Nesting-safe critical section.
 *
 * noInterrupts()/interrupts() do NOT nest: a nested exit() would re-enable
 * interrupts while an outer section still expects them masked. We count depth
 * and only re-enable on the outermost exit. save_and_disable_interrupts() /
 * restore_interrupts() (pico SDK) capture and restore the PRIOR interrupt
 * state, so an outer caller that was already masked stays masked on exit.
 *
 * Depth and saved state are per-core: RP2040 is dual-core and interrupt masking
 * is inherently per-core, so a shared counter would let core 0's exit observe
 * core 1's depth. get_core_num() indexes the owning core's slot. */
namespace {
volatile uint32_t s_critical_depth[2] = {0u, 0u};
uint32_t s_saved_irq[2] = {0u, 0u};
} // namespace

extern "C" bool hal_rp2040_critical_section_active(void) {
  return s_critical_depth[get_core_num()] > 0u;
}

void hal_critical_section_enter(void) {
  const uint32_t saved = save_and_disable_interrupts();
  const uint core = get_core_num();
  if (s_critical_depth[core] == 0u) {
    s_saved_irq[core] = saved;
  }
  ++s_critical_depth[core];
}

void hal_critical_section_exit(void) {
  const uint core = get_core_num();
  if (s_critical_depth[core] == 0u) {
    return;
  }

  --s_critical_depth[core];
  if (s_critical_depth[core] == 0u) {
    restore_interrupts(s_saved_irq[core]);
  }
}
#endif // HAL_TARGET_IS_RP
