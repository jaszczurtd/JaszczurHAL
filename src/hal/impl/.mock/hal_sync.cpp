#include "hal/core/hal_compiler.h"
#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "hal/core/hal_config.h"
#include "hal/system/hal_sync.h"
#include "hal_mock.h"
#include <atomic>
#include <mutex>
#include <new>
#include <stdint.h>

namespace {
/* Statistics span every mutex, and threaded host tests lock different
 * mutexes at once, so the counters themselves must be atomic. */
std::atomic<uint32_t> s_mutex_lock_count{0u};
std::atomic<uint32_t> s_mutex_unlock_count{0u};
std::atomic<uint32_t> s_mutex_depth{0u};
std::atomic<uint32_t> s_mutex_max_depth{0u};
bool s_fail_next_mutex_create = false;

void record_lock(void) {
  s_mutex_lock_count.fetch_add(1u);
  const uint32_t depth = s_mutex_depth.fetch_add(1u) + 1u;
  uint32_t seen = s_mutex_max_depth.load();
  while (depth > seen &&
         !s_mutex_max_depth.compare_exchange_weak(seen, depth)) {
  }
}
} // namespace

struct hal_mutex_impl_t {
  std::mutex mtx;
};

extern "C" hal_mutex_t jh_hal_mutex_try_create(void) {
  if (HAL_ATOMIC_EXCHANGE(&s_fail_next_mutex_create, false,
                          HAL_ATOMIC_ACQ_REL)) {
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
  record_lock();
}

bool hal_mutex_try_lock(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_try_lock: mutex is NULL");
  if (mutex == NULL || !mutex->mtx.try_lock()) {
    return false;
  }

  record_lock();
  return true;
}

void hal_mutex_unlock(hal_mutex_t mutex) {
  HAL_ASSERT(mutex != NULL, "hal_mutex_unlock: mutex is NULL");
  if (mutex == NULL) {
    return;
  }

  s_mutex_unlock_count.fetch_add(1u);
  uint32_t depth = s_mutex_depth.load();
  while (depth > 0u &&
         !s_mutex_depth.compare_exchange_weak(depth, depth - 1u)) {
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
  s_mutex_lock_count.store(0u);
  s_mutex_unlock_count.store(0u);
  s_mutex_depth.store(0u);
  s_mutex_max_depth.store(0u);
}

uint32_t hal_mock_mutex_lock_count(void) { return s_mutex_lock_count.load(); }

uint32_t hal_mock_mutex_unlock_count(void) {
  return s_mutex_unlock_count.load();
}

uint32_t hal_mock_mutex_max_depth(void) { return s_mutex_max_depth.load(); }

void hal_mock_mutex_fail_next_create(bool fail) {
  HAL_ATOMIC_STORE(&s_fail_next_mutex_create, fail, HAL_ATOMIC_RELEASE);
}
#endif // HAL_TARGET_IS_MOCK
