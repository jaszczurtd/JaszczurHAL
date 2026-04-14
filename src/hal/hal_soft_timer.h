#pragma once

/**
 * @file hal_soft_timer.h
 * @brief C-compatible wrapper for SmartTimers utility.
 *
 * This API exposes non-blocking software timers as opaque C handles.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pass as interval to stop the timer. */
#define HAL_SOFT_TIMER_STOP 0u

/** @brief Opaque software timer implementation type. */
typedef struct hal_soft_timer_impl_s hal_soft_timer_impl_t;

/** @brief Handle to a software timer instance. */
typedef hal_soft_timer_impl_t *hal_soft_timer_t;

/** @brief Timer callback signature. */
typedef void (*hal_soft_timer_callback_t)(void);

/**
 * @brief Create a new software timer.
 * @return Timer handle, or NULL on allocation failure.
 */
hal_soft_timer_t hal_soft_timer_create(void);

/**
 * @brief Destroy a software timer.
 * @param timer Timer handle. NULL is ignored.
 */
void hal_soft_timer_destroy(hal_soft_timer_t timer);

/**
 * @brief Configure callback and interval, then restart the timer.
 * @param timer Timer handle.
 * @param callback Callback invoked after interval elapsed.
 * @param interval_ms Interval in milliseconds.
 * @return true on success, false when @p timer is NULL.
 */
bool hal_soft_timer_begin(hal_soft_timer_t timer,
                          hal_soft_timer_callback_t callback,
                          uint32_t interval_ms);

/**
 * @brief Restart timer counting from current time.
 * @param timer Timer handle.
 */
void hal_soft_timer_restart(hal_soft_timer_t timer);

/**
 * @brief Check whether timer interval elapsed.
 * @param timer Timer handle.
 * @return true when elapsed.
 */
bool hal_soft_timer_available(hal_soft_timer_t timer);

/**
 * @brief Get remaining time until next callback.
 * @param timer Timer handle.
 * @return Remaining milliseconds, or 0 when elapsed/stopped/invalid.
 */
uint32_t hal_soft_timer_time_left(hal_soft_timer_t timer);

/**
 * @brief Set timer interval without restarting.
 * @param timer Timer handle.
 * @param interval_ms New interval in milliseconds.
 */
void hal_soft_timer_set_interval(hal_soft_timer_t timer, uint32_t interval_ms);

/**
 * @brief Advance timer state and fire callback when elapsed.
 * @param timer Timer handle.
 */
void hal_soft_timer_tick(hal_soft_timer_t timer);

/**
 * @brief Stop timer and clear timing state.
 * @param timer Timer handle.
 */
void hal_soft_timer_abort(hal_soft_timer_t timer);

/**
 * @brief Descriptor for one entry in a timer initialisation table.
 *
 * Modules declare a const array of these entries and pass it to
 * hal_soft_timer_setup_table() / hal_soft_timer_tick_table().
 */
typedef struct {
  hal_soft_timer_t             *timer;
  hal_soft_timer_callback_t     callback;
  uint32_t                      intervalMs;
} hal_soft_timer_table_entry_t;

/**
 * @brief Create, configure and start every timer described in @p table.
 *
 * For each entry the function creates the timer (if not yet created),
 * calls hal_soft_timer_begin(), and optionally feeds the watchdog and
 * inserts a short delay between entries.
 *
 * @param table       Pointer to an array of table entries.
 * @param count       Number of entries in the array.
 * @param idle_cb     Optional callback invoked between entries
 *                    (e.g. watchdog_feed). May be NULL.
 * @param delay_ms    Milliseconds to wait between successive entries
 *                    (0 = no delay).
 * @return true on success, false when @p table is NULL or @p count is 0.
 */
bool hal_soft_timer_setup_table(const hal_soft_timer_table_entry_t *table,
                                uint32_t count,
                                void (*idle_cb)(void),
                                uint32_t delay_ms);

/**
 * @brief Tick every timer described in @p table.
 *
 * Equivalent to calling hal_soft_timer_tick() for each entry.
 *
 * @param table  Pointer to an array of table entries.
 * @param count  Number of entries in the array.
 * @return true on success, false when @p table is NULL or @p count is 0.
 */
bool hal_soft_timer_tick_table(const hal_soft_timer_table_entry_t *table,
                               uint32_t count);

#ifdef __cplusplus
}
#endif
