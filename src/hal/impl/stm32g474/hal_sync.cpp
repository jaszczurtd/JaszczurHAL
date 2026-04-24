#if !defined(ARDUINO) || defined(ARDUINO_ARCH_STM32)

#include "../../hal_sync.h"
#include "../../hal_config.h"

#include <new>

struct hal_mutex_impl_t {
    volatile uint8_t locked;
};

static inline void hal_sync_relax(void) {
#if defined(__arm__) || defined(__thumb__) || defined(__aarch64__)
    __asm__ volatile("nop");
#endif
}

hal_mutex_t hal_mutex_create(void) {
    hal_mutex_impl_t *m = new (std::nothrow) hal_mutex_impl_t();
    HAL_ASSERT(m != NULL, "hal_mutex_create: allocation failed");
    if (!m) {
        return NULL;
    }
    m->locked = 0u;
    return m;
}

void hal_mutex_lock(hal_mutex_t mutex) {
    HAL_ASSERT(mutex != NULL, "hal_mutex_lock: mutex is NULL");
    if (!mutex) {
        return;
    }

    while (__atomic_test_and_set(&mutex->locked, __ATOMIC_ACQUIRE)) {
        hal_sync_relax();
    }
}

void hal_mutex_unlock(hal_mutex_t mutex) {
    HAL_ASSERT(mutex != NULL, "hal_mutex_unlock: mutex is NULL");
    if (!mutex) {
        return;
    }

    __atomic_clear(&mutex->locked, __ATOMIC_RELEASE);
}

void hal_mutex_destroy(hal_mutex_t mutex) {
    HAL_ASSERT(mutex != NULL, "hal_mutex_destroy: mutex is NULL");
    if (!mutex) {
        return;
    }

    delete mutex;
}

void hal_critical_section_enter(void) {
    /* STM32G474 TODO: replace with PRIMASK/NVIC critical-section handling. */
}

void hal_critical_section_exit(void) {
    /* STM32G474 TODO: replace with PRIMASK/NVIC critical-section handling. */
}

#endif /* !defined(ARDUINO) || defined(ARDUINO_ARCH_STM32) */
