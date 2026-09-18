// hal_mutex on std::mutex for host tests that compile a target backend and
// run it on real threads.

#include "hal/core/hal_mutex_once.h"
#include "hal/system/hal_sync.h"

#include <mutex>

struct hal_mutex_impl_t {
  std::mutex mutex;
};

extern "C" {

hal_mutex_t hal_mutex_create(void) { return new hal_mutex_impl_t(); }
hal_mutex_t jh_hal_mutex_try_create(void) { return hal_mutex_create(); }
void hal_mutex_lock(hal_mutex_t mutex) { mutex->mutex.lock(); }
bool hal_mutex_try_lock(hal_mutex_t mutex) { return mutex->mutex.try_lock(); }
void hal_mutex_unlock(hal_mutex_t mutex) { mutex->mutex.unlock(); }
void hal_mutex_destroy(hal_mutex_t mutex) { delete mutex; }
void hal_critical_section_enter(void) {}
void hal_critical_section_exit(void) {}

} // extern "C"
