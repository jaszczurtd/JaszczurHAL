#pragma once

/**
 * @file hal_sync.h
 * @brief Hardware abstraction for mutexes and critical sections.
 *
 * On RP2040 this wraps pico SDK mutex_t and critical_section_t.
 * Mock builds use std::mutex.
 */

typedef struct hal_mutex_impl_t hal_mutex_impl_t;
#ifdef __cplusplus
extern "C" {
#endif


/** @brief Opaque mutex handle. */
typedef hal_mutex_impl_t* hal_mutex_t;

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
 * @brief Enter a global critical section (disables interrupts on RP2040).
 *
 * Must be paired with hal_critical_section_exit().
 */
void hal_critical_section_enter(void);

/**
 * @brief Exit the global critical section (re-enables interrupts).
 */
void hal_critical_section_exit(void);

#ifdef __cplusplus
}
#endif
