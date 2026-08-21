#pragma once

/**
 * @file hal_sync.h
 * @brief Hardware abstraction for mutexes and critical sections.
 *
 * On RP2040 non-FreeRTOS builds this wraps pico SDK mutex_t; on STM32G474
 * non-FreeRTOS builds it uses a single-core atomic spinlock. In supported
 * HAL_ENABLE_FREERTOS builds, including ESP32-S3, hal_mutex_* uses a FreeRTOS
 * mutex. Critical sections use target interrupt masking for timing-sensitive
 * code; the ESP32-S3 path also uses a shared portMUX to serialize both cores.
 * They are not a scheduler lock or an ISR-safe mutex. Mock builds use
 * std::mutex.
 */

#include <stdbool.h>

typedef struct hal_mutex_impl_t hal_mutex_impl_t;
#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque mutex handle. */
typedef hal_mutex_impl_t *hal_mutex_t;

/**
 * @brief Create and initialise a new mutex.
 * @return Mutex handle, or triggers HAL_ASSERT on allocation failure.
 */
hal_mutex_t hal_mutex_create(void);

/**
 * @brief Lock the mutex (blocking).
 * @param mutex Handle from hal_mutex_create().
 */
void hal_mutex_lock(hal_mutex_t mutex);

/**
 * @brief Try to lock the mutex without waiting.
 *
 * This is the only mutex operation permitted by bare-metal interrupt workers.
 * FreeRTOS mutexes remain task-only and return false in interrupt context.
 *
 * @param mutex Handle from hal_mutex_create().
 * @return true when the lock was acquired, false when it is already held or
 *         the handle is invalid.
 */
bool hal_mutex_try_lock(hal_mutex_t mutex);

/**
 * @brief Unlock the mutex.
 * @param mutex Handle from hal_mutex_create().
 */
void hal_mutex_unlock(hal_mutex_t mutex);

/**
 * @brief Destroy the mutex and free associated memory.
 * @param mutex Handle to destroy. Must not be used after this call.
 */
void hal_mutex_destroy(hal_mutex_t mutex);

/**
 * @brief Enter a hard, target-specific interrupt critical section.
 *
 * Must be paired with hal_critical_section_exit().
 */
void hal_critical_section_enter(void);

/**
 * @brief Exit the hard critical section (restores prior interrupt state).
 */
void hal_critical_section_exit(void);

#ifdef __cplusplus
}
#endif
