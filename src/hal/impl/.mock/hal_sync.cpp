#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "hal/core/hal_config.h"
#include "hal/system/hal_sync.h"
#include "hal_mock.h"
#include <mutex>
#include <new>
#include <stdint.h>

namespace {
uint32_t s_mutex_lock_count = 0u;
uint32_t s_mutex_unlock_count = 0u;
uint32_t s_mutex_depth = 0u;
uint32_t s_mutex_max_depth = 0u;
bool s_fail_next_mutex_create = false;
} // namespace

struct hal_mutex_impl_t {
  std::mutex mtx;
};

extern "C" hal_mutex_t jh_hal_mutex_try_create(void) {
  if (__atomic_exchange_n(&s_fail_next_mutex_create, false, __ATOMIC_ACQ_REL)) {
    return nullptr;
  }
  return new (std::nothrow) hal_mutex_impl_t();
}

hal_mutex_t hal_mutex_create(void) {
  hal_mutex_t mutex = jh_hal_mutex_try_create();
  HAL_ASSERT(mutex != NULL, "hal_mutex_create: allocation failed");
  return mutex;
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

bool hal_mutex_try_lock(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_try_lock: mutex is NULL");
  if (mutex == NULL || !mutex->mtx.try_lock()) {
    return false;
  }

  s_mutex_lock_count++;
  s_mutex_depth++;
  if (s_mutex_depth > s_mutex_max_depth) {
    s_mutex_max_depth = s_mutex_depth;
  }
  return true;
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
uint32_t s_critical_enter_count = 0u;
bool s_irq_enabled = true;
bool s_saved_irq = true;
} // namespace

void hal_critical_section_enter(void) {
  ++s_critical_enter_count;
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

uint32_t hal_mock_critical_enter_count(void) { return s_critical_enter_count; }

bool hal_mock_irq_enabled(void) { return s_irq_enabled; }

void hal_mock_critical_section_reset(void) {
  s_critical_depth = 0u;
  s_critical_enter_count = 0u;
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

void hal_mock_mutex_fail_next_create(bool fail) {
  __atomic_store_n(&s_fail_next_mutex_create, fail, __ATOMIC_RELEASE);
}
#endif // HAL_TARGET_IS_MOCK
