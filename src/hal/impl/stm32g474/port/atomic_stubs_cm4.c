/**
 * @file atomic_stubs_cm4.c
 * @brief Minimal __atomic_* stubs for Cortex-M4 (single-core, no libatomic).
 *
 * On single-core Cortex-M4 a critical section (disable/enable IRQ) is
 * sufficient to guarantee atomicity for 64-bit operations. These stubs satisfy
 * the linker when -latomic is not available in the bare-metal toolchain.
 */
#if defined(HAL_TARGET_STM32G474) || defined(STM32G474xx) || defined(STM32G4)

#include <stdint.h>
#include <string.h>

static inline uint32_t _disable_irq(void) {
    uint32_t primask;
    __asm volatile ("mrs %0, primask\n cpsid i" : "=r"(primask) :: "memory");
    return primask;
}

static inline void _restore_irq(uint32_t primask) {
    __asm volatile ("msr primask, %0" :: "r"(primask) : "memory");
}

void __atomic_store_8(volatile void *ptr, uint64_t val, int memorder) {
    (void)memorder;
    uint32_t s = _disable_irq();
    memcpy((void *)ptr, &val, 8);
    _restore_irq(s);
}

uint64_t __atomic_load_8(const volatile void *ptr, int memorder) {
    (void)memorder;
    uint64_t val;
    uint32_t s = _disable_irq();
    memcpy(&val, (const void *)ptr, 8);
    _restore_irq(s);
    return val;
}

uint64_t __atomic_exchange_8(volatile void *ptr, uint64_t val, int memorder) {
    (void)memorder;
    uint64_t old;
    uint32_t s = _disable_irq();
    memcpy(&old, (void *)ptr, 8);
    memcpy((void *)ptr, &val, 8);
    _restore_irq(s);
    return old;
}

#endif /* HAL_TARGET_STM32G474 || STM32G474xx || STM32G4 */
