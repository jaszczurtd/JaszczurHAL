#pragma once

/**
 * @file hal_timer.h
 * @brief Hardware abstraction for one-shot alarm timers.
 *
 * On RP2040 wraps the pico SDK hardware alarm API.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Alarm identifier type. */
typedef int32_t hal_alarm_id_t;

/** @brief Sentinel value indicating an invalid / unset alarm. */
#define HAL_ALARM_INVALID (-1)

/**
 * @brief Alarm callback signature.
 * @param id        Alarm identifier that fired.
 * @param user_data User pointer passed at creation.
 * @return Positive value to reschedule (delay in µs), 0 or negative to stop.
 */
typedef int64_t (*hal_alarm_callback_t)(hal_alarm_id_t id, void *user_data);

/**
 * @brief Schedule a one-shot alarm.
 * @param delay_us     Delay in microseconds before firing.
 * @param callback     Function to call when the alarm fires.
 * @param user_data    Opaque pointer forwarded to the callback.
 * @param fire_if_past If true, fire immediately when the target time is in the past.
 * @return Alarm ID on success, HAL_ALARM_INVALID on failure / pool exhaustion.
 */
hal_alarm_id_t hal_timer_add_alarm_us(uint32_t delay_us, hal_alarm_callback_t callback,
                                      void *user_data, bool fire_if_past);

/**
 * @brief Cancel a previously scheduled alarm.
 * @param alarm_id ID returned by hal_timer_add_alarm_us().
 * @return true if the alarm was found and cancelled.
 */
bool hal_timer_cancel_alarm(hal_alarm_id_t alarm_id);

#ifdef __cplusplus
}
#endif
