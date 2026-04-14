#ifndef JASZCZURHAL_TOOLS_COMMON_DEFS_H
#define JASZCZURHAL_TOOLS_COMMON_DEFS_H

/**
 * @file tools_common_defs.h
 * @brief Shared macro/constant definitions used by both tools.h and tools_c.h.
 */

#include <stdint.h>
#include <hal/hal_bits.h>

#include "tools_logger_config.h"
#include "tools_sensor_config.h"
#include "tools_printable_config.h"

/** @brief Declare a static mutex variable. */
#define m_mutex_def(mutexname)            static hal_mutex_t mutexname = NULL
/** @brief Initialise a mutex. */
#define m_mutex_init(mutexname)           ((mutexname) = hal_mutex_create())
/** @brief Lock a mutex (blocking). */
#define m_mutex_enter_blocking(mutexname) hal_mutex_lock(mutexname)
/** @brief Unlock a mutex. */
#define m_mutex_exit(mutexname)           hal_mutex_unlock(mutexname)
/** @brief Delay in milliseconds. */
#define m_delay(val)                      hal_delay_ms(val)
/** @brief Delay in microseconds. */
#define m_delay_microseconds(val)         hal_delay_us(val)

/** @brief Sentinel initialization value used by selected legacy helpers. */
#define C_INIT_VAL 0xdeadbeef

/**
 * @brief Place variable in `.noinit` section.
 *
 * Value survives soft reset and can be used for reboot diagnostics.
 */
#ifndef NOINIT
#define NOINIT __attribute__((section(".noinit")))
#endif

/** @brief DTC status: no change. */
#define D_NONE 0
/** @brief DTC status: add/set request. */
#define D_ADD 1
/** @brief DTC status: clear/remove request. */
#define D_SUB 2

#endif
