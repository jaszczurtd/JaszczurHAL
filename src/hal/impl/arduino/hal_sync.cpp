#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"
#include "../../hal_sync.h"
#include <Arduino.h>
#include <hardware/sync.h>
#include <pico/mutex.h>
#include <pico/platform.h>

struct hal_mutex_impl_t {
  mutex_t mtx;
};

hal_mutex_t hal_mutex_create(void) {
  hal_mutex_impl_t *m = new hal_mutex_impl_t();
  HAL_ASSERT(m != NULL, "hal_mutex_create: allocation failed");
  mutex_init(&m->mtx);
  return m;
}

void hal_mutex_lock(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_lock: mutex is NULL");
  if (mutex == NULL) {
    return;
  }

  mutex_enter_blocking(&mutex->mtx);
}

void hal_mutex_unlock(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_unlock: mutex is NULL");
  if (mutex == NULL) {
    return;
  }

  mutex_exit(&mutex->mtx);
}

void hal_mutex_destroy(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_destroy: mutex is NULL");
  if (mutex == NULL) {
    return;
  }

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
#endif // HAL_TARGET_IS_RP2040
