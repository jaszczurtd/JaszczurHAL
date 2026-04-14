#ifndef T_WATCHDOG
#define T_WATCHDOG

/**
 * @file multicoreWatchdog.h
 * @brief Dual-core software watchdog for RP2040.
 *
 * Each core must call its update function periodically.
 * If a core stalls, the user-supplied callback is invoked
 * with diagnostic counter values.
 */

#include "libConfig.h"
#include <stdbool.h>
#include <stdint.h>
#include "tools_common_defs.h"
#include "tools_api.h"

/** @brief Number of diagnostic counter values passed to the watchdog callback. */
#define WATCHDOG_VALUES_AMOUNT 4

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the dual-core watchdog.
 *
 * Enables the hardware watchdog with a timeout of @p time milliseconds.
 * An internal software timer fires every @p time / 10 ms to check whether
 * both cores are still alive; the hardware watchdog is fed only when both
 * cores have incremented their counters since the last check.
 *
 * If the system was previously reset by the watchdog, the user-supplied
 * @p function callback is invoked once with diagnostic values (core-alive
 * flags saved across the reboot via NOINIT RAM) before normal operation
 * resumes.
 *
 * @param function Callback invoked once if the watchdog caused the reboot.
 *                 Receives an array of WATCHDOG_VALUES_AMOUNT diagnostic
 *                 integers and its size. May be NULL.
 * @param time     Hardware watchdog timeout in milliseconds.
 *                 The liveness check runs at time / 10 ms intervals.
 * @return true if the system was rebooted by the watchdog (useful for
 *         post-reboot recovery logic), false on a clean boot.
 */
bool setupWatchdog(void(*function)(int *values, int size), unsigned int time);

/**
 * @brief Must be called periodically from core 0 to signal liveness.
 */
void updateWatchdogCore0(void);

/**
 * @brief Must be called periodically from core 1 to signal liveness.
 */
void updateWatchdogCore1(void);

/**
 * @brief Mark core 0 as started. Call once after core 0 initialisation.
 */
void setStartedCore0(void);

/**
 * @brief Mark core 1 as started. Call once after core 1 initialisation.
 */
void setStartedCore1(void);

/**
 * @brief Check whether both cores have been started.
 * @return true if both setStartedCore0() and setStartedCore1() have been called.
 */
bool isEnvironmentStarted(void);

/**
 * @brief Force an immediate system reset via the hardware watchdog.
 */
void triggerSystemReset(void);

/**
 * @brief Feed the hardware watchdog timer (alias for hal_watchdog_feed).
 */
void watchdog_feed(void);

#ifdef __cplusplus
}
#endif

#endif
