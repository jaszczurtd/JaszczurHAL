#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../hal_sync.h"
#include "../../hal_config.h"

#include <stddef.h>
#include <new>
#include <stdint.h>

struct hal_mutex_impl_t {
    volatile uint8_t locked;
};

namespace {
#if defined(__arm__) || defined(__thumb__)
volatile uint32_t s_critical_depth = 0u;
uint32_t s_saved_primask = 0u;

static inline uint32_t stm32_read_primask(void) {
    uint32_t primask;
    __asm__ volatile("MRS %0, primask" : "=r"(primask) :: "memory");
    return primask;
}

static inline void stm32_disable_irq(void) {
    __asm__ volatile("cpsid i" ::: "memory");
}

static inline void stm32_enable_irq(void) {
    __asm__ volatile("cpsie i" ::: "memory");
}
#endif
} // namespace

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
#if defined(__arm__) || defined(__thumb__)
    const uint32_t primask = stm32_read_primask();
    stm32_disable_irq();
    if (s_critical_depth == 0u) {
        s_saved_primask = primask;
    }
    ++s_critical_depth;
#endif
}

void hal_critical_section_exit(void) {
#if defined(__arm__) || defined(__thumb__)
    if (s_critical_depth == 0u) {
        return;
    }

    --s_critical_depth;
    if ((s_critical_depth == 0u) && ((s_saved_primask & 0x1u) == 0u)) {
        stm32_enable_irq();
    }
#endif
}

#endif  // HAL_TARGET_IS_STM32G474
