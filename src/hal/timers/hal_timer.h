#pragma once

/**
 * @file hal_timer.h
 * @brief Hardware abstraction for alarm timers.
 *
 * The API has two layers:
 *  1) low-level alarm primitives (alarm_id add/cancel),
 *  2) managed timers with create/start/stop/pause/resume/query semantics.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Alarm identifier type. */
typedef int32_t hal_alarm_id_t;

/** @brief Sentinel value indicating an invalid / unset alarm. */
#define HAL_ALARM_INVALID (-1)

/** @brief Opaque alarm-pool handle. */
typedef struct hal_timer_pool_impl_s hal_timer_pool_impl_t;
typedef hal_timer_pool_impl_t *hal_timer_pool_t;

/**
 * @brief Sentinel selecting the platform default alarm pool.
 *
 * Pass this where a pool handle is expected to use the shared default pool.
 */
#define HAL_TIMER_POOL_DEFAULT ((hal_timer_pool_t)0)

/**
 * @brief Result/status codes for timer operations.
 */
typedef enum {
  HAL_TIMER_OK = 0,
  HAL_TIMER_ERR_INVALID_ARG = -1,
  HAL_TIMER_ERR_TIME_PASSED = -2,
  HAL_TIMER_ERR_POOL_FULL = -3,
  HAL_TIMER_ERR_NO_RESOURCE = -4,
  HAL_TIMER_ERR_NOT_SUPPORTED = -5,
  HAL_TIMER_ERR_NOT_FOUND = -6,
  HAL_TIMER_ERR_ALREADY_RUNNING = -7,
  HAL_TIMER_ERR_NOT_RUNNING = -8,
  HAL_TIMER_ERR_NOT_PAUSED = -9,
  HAL_TIMER_ERR_INTERNAL = -10,
} hal_timer_result_t;

/**
 * @brief Managed timer runtime state.
 */
typedef enum {
  HAL_TIMER_STATE_STOPPED = 0,
  HAL_TIMER_STATE_RUNNING = 1,
  HAL_TIMER_STATE_PAUSED = 2,
} hal_timer_state_t;

/** @brief Opaque managed-timer handle. */
typedef struct hal_timer_impl_s hal_timer_impl_t;
typedef hal_timer_impl_t *hal_timer_t;

/**
 * @brief Alarm callback signature.
 * @param id        Alarm identifier that fired.
 * @param user_data User pointer passed at creation.
 * @return Positive value to reschedule (delay in µs), 0 or negative to stop.
 */
typedef int64_t (*hal_alarm_callback_t)(hal_alarm_id_t id, void *user_data);

/**
 * @brief Managed timer callback signature.
 *
 * Called whenever the timer fires (one-shot or periodic).
 */
typedef void (*hal_timer_callback_t)(hal_timer_t timer, void *user_data);

/**
 * @brief Create a dedicated alarm pool bound to a selected hardware alarm.
 *
 * This is an advanced API intended for scaling beyond the default pool.
 * On RP2040/RP2350 each timer instance has 4 hardware alarms.
 *
 * @param hardware_alarm_num Hardware alarm index (0..3 on RP2040 family).
 * @param max_timers         Requested logical timers in this pool.
 * @return Pool handle on success; NULL on allocation/claim failure.
 */
hal_timer_pool_t hal_timer_pool_create(uint8_t hardware_alarm_num,
                                       uint16_t max_timers);

/**
 * @brief Create a dedicated alarm pool using an unused hardware alarm.
 *
 * @param max_timers Requested logical timers in this pool.
 * @return Pool handle on success; NULL if no hardware alarm is available.
 */
hal_timer_pool_t hal_timer_pool_create_auto(uint16_t max_timers);

/**
 * @brief Destroy a pool created by hal_timer_pool_create*().
 *
 * Passing NULL or HAL_TIMER_POOL_DEFAULT is a no-op.
 */
void hal_timer_pool_destroy(hal_timer_pool_t pool);

/**
 * @brief Schedule a one-shot alarm in the selected pool.
 *
 * Use HAL_TIMER_POOL_DEFAULT to target the platform default pool.
 *
 * @return Alarm ID on success, HAL_ALARM_INVALID on failure / exhaustion.
 */
hal_alarm_id_t hal_timer_pool_add_alarm_us(hal_timer_pool_t pool,
                                           uint32_t delay_us,
                                           hal_alarm_callback_t callback,
                                           void *user_data, bool fire_if_past);

/**
 * @brief Schedule a one-shot alarm in the selected pool with diagnostics.
 *
 * @param out_result Optional pointer receiving detailed status code.
 * @return Alarm ID on success, HAL_ALARM_INVALID otherwise.
 */
hal_alarm_id_t hal_timer_pool_add_alarm_us_ex(
    hal_timer_pool_t pool, uint32_t delay_us, hal_alarm_callback_t callback,
    void *user_data, bool fire_if_past, hal_timer_result_t *out_result);

/**
 * @brief Cancel an alarm in the selected pool.
 *
 * Use HAL_TIMER_POOL_DEFAULT to cancel from the platform default pool.
 *
 * @return true if the alarm was found and cancelled.
 */
bool hal_timer_pool_cancel_alarm(hal_timer_pool_t pool,
                                 hal_alarm_id_t alarm_id);

/**
 * @brief Schedule a one-shot alarm.
 * @param delay_us     Delay in microseconds before firing.
 * @param callback     Function to call when the alarm fires.
 * @param user_data    Opaque pointer forwarded to the callback.
 * @param fire_if_past If true, fire immediately when the target time is in the
 * past.
 * @return Alarm ID on success, HAL_ALARM_INVALID on failure / pool exhaustion.
 */
hal_alarm_id_t hal_timer_add_alarm_us(uint32_t delay_us,
                                      hal_alarm_callback_t callback,
                                      void *user_data, bool fire_if_past);

/**
 * @brief Schedule a one-shot alarm with diagnostics.
 *
 * @param out_result Optional pointer receiving detailed status code.
 * @return Alarm ID on success, HAL_ALARM_INVALID otherwise.
 */
hal_alarm_id_t hal_timer_add_alarm_us_ex(uint32_t delay_us,
                                         hal_alarm_callback_t callback,
                                         void *user_data, bool fire_if_past,
                                         hal_timer_result_t *out_result);

/**
 * @brief Cancel a previously scheduled alarm.
 * @param alarm_id ID returned by hal_timer_add_alarm_us().
 * @return true if the alarm was found and cancelled.
 */
bool hal_timer_cancel_alarm(hal_alarm_id_t alarm_id);

/**
 * @brief Create a managed timer object.
 *
 * The timer is created in STOPPED state. Call hal_timer_start() to arm it.
 *
 * @param pool      Alarm pool for this timer (HAL_TIMER_POOL_DEFAULT allowed).
 * @param period_us Initial period in microseconds (>0).
 * @param periodic  true for periodic timer; false for one-shot timer.
 * @param callback  User callback invoked on fire.
 * @param user_data Opaque pointer forwarded to callback.
 * @param out_timer Output handle.
 */
hal_timer_result_t hal_timer_create(hal_timer_pool_t pool, uint32_t period_us,
                                    bool periodic,
                                    hal_timer_callback_t callback,
                                    void *user_data, hal_timer_t *out_timer);

/** @brief Destroy a managed timer object (stops active timer first). */
hal_timer_result_t hal_timer_destroy(hal_timer_t timer);

/** @brief Start (arm) a managed timer. */
hal_timer_result_t hal_timer_start(hal_timer_t timer);

/** @brief Stop a managed timer. */
hal_timer_result_t hal_timer_stop(hal_timer_t timer);

/** @brief Pause a running managed timer. */
hal_timer_result_t hal_timer_pause(hal_timer_t timer);

/** @brief Resume a paused managed timer. */
hal_timer_result_t hal_timer_resume(hal_timer_t timer);

/**
 * @brief Change timer period.
 *
 * @param restart_if_running true: re-arm immediately from now with new period.
 *                           false: apply to future arms / periodic cycles only.
 */
hal_timer_result_t hal_timer_set_period_us(hal_timer_t timer,
                                           uint32_t period_us,
                                           bool restart_if_running);

/** @brief Read configured period. */
hal_timer_result_t hal_timer_get_period_us(hal_timer_t timer,
                                           uint32_t *out_period_us);

/** @brief Read current state. Returns HAL_TIMER_STATE_STOPPED for NULL handle.
 */
hal_timer_state_t hal_timer_get_state(hal_timer_t timer);

/**
 * @brief Read remaining time to fire.
 *
 * RUNNING: estimated remaining time in microseconds.
 * PAUSED:  latched remaining time from pause moment.
 */
hal_timer_result_t hal_timer_get_remaining_us(hal_timer_t timer,
                                              int64_t *out_remaining_us);

#ifdef __cplusplus
}
#endif
