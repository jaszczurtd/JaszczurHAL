#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"
#include "../../hal_sync.h"
#include "hal_mock.h"
#include <mutex>
#include <stdint.h>

namespace {
uint32_t s_mutex_lock_count = 0u;
uint32_t s_mutex_unlock_count = 0u;
uint32_t s_mutex_depth = 0u;
uint32_t s_mutex_max_depth = 0u;
} // namespace

struct hal_mutex_impl_t {
  std::mutex mtx;
};

hal_mutex_t hal_mutex_create(void) {
  hal_mutex_impl_t *m = new hal_mutex_impl_t();
  HAL_ASSERT(m != NULL, "hal_mutex_create: allocation failed");
  return m;
}

void hal_mutex_lock(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_lock: mutex is NULL");
  if (mutex == NULL) {
    return;
  }

  mutex->mtx.lock();
  s_mutex_lock_count++;
  s_mutex_depth++;
  if (s_mutex_depth > s_mutex_max_depth) {
    s_mutex_max_depth = s_mutex_depth;
  }
}

void hal_mutex_unlock(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_unlock: mutex is NULL");
  if (mutex == NULL) {
    return;
  }

  s_mutex_unlock_count++;
  if (s_mutex_depth > 0u) {
    s_mutex_depth--;
  }
  mutex->mtx.unlock();
}

void hal_mutex_destroy(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_destroy: mutex is NULL");
  if (mutex == NULL) {
    return;
  }

  delete mutex;
}

/* Mirror the target nesting contract so it can be regression-tested on the
 * host: count depth, save the prior "interrupts enabled" state on the outermost
 * enter, and restore it only on the outermost exit (an already-masked outer
 * caller stays masked). The flags below are pure test introspection - there are
 * no real interrupts on the host. */
namespace {
uint32_t s_critical_depth = 0u;
bool s_irq_enabled = true;
bool s_saved_irq = true;
} // namespace

void hal_critical_section_enter(void) {
  if (s_critical_depth == 0u) {
    s_saved_irq = s_irq_enabled;
    s_irq_enabled = false;
  }
  ++s_critical_depth;
}

void hal_critical_section_exit(void) {
  if (s_critical_depth == 0u) {
    return;
  }

  --s_critical_depth;
  if (s_critical_depth == 0u) {
    s_irq_enabled = s_saved_irq;
  }
}

uint32_t hal_mock_critical_depth(void) { return s_critical_depth; }

bool hal_mock_irq_enabled(void) { return s_irq_enabled; }

void hal_mock_critical_section_reset(void) {
  s_critical_depth = 0u;
  s_irq_enabled = true;
  s_saved_irq = true;
}

void hal_mock_mutex_stats_reset(void) {
  s_mutex_lock_count = 0u;
  s_mutex_unlock_count = 0u;
  s_mutex_depth = 0u;
  s_mutex_max_depth = 0u;
}

uint32_t hal_mock_mutex_lock_count(void) { return s_mutex_lock_count; }

uint32_t hal_mock_mutex_unlock_count(void) { return s_mutex_unlock_count; }

uint32_t hal_mock_mutex_max_depth(void) { return s_mutex_max_depth; }
#endif // HAL_TARGET_IS_MOCK
