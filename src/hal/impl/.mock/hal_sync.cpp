#include "../../hal_sync.h"
#include "../../hal_config.h"
#include <mutex>

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
    mutex->mtx.lock();
}

void hal_mutex_unlock(hal_mutex_t mutex) {
    HAL_ASSERT(mutex != NULL, "hal_mutex_unlock: mutex is NULL");
    mutex->mtx.unlock();
}

void hal_mutex_destroy(hal_mutex_t mutex) {
    HAL_ASSERT(mutex != NULL, "hal_mutex_destroy: mutex is NULL");
    delete mutex;
}

void hal_critical_section_enter(void) {}
void hal_critical_section_exit(void) {}
