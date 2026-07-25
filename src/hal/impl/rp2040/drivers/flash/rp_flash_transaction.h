#pragma once

#include "hal/hal_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef hal_status_t (*jh_rp_flash_operation_t)(void *context);

/**
 * Initialise the current core as a flash-safe lockout victim.
 *
 * Native app entry calls this before application code. FreeRTOS SMP uses the
 * Pico SDK scheduler-aware safety helper, while bare-metal multicore installs
 * the FIFO lockout handler on each participating core.
 */
hal_status_t jh_rp_flash_transaction_core_init(void);

/**
 * Execute one RAM-resident flash mutation inside the shared safe zone.
 *
 * The operation and its context must not reside in an XIP address window.
 * Calls from IRQ/hard-critical context and recursive calls are rejected.
 */
hal_status_t jh_rp_flash_transaction_execute(jh_rp_flash_operation_t operation,
                                             void *context,
                                             uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
