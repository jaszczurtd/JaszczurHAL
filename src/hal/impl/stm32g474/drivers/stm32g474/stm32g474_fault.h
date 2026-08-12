#pragma once

/**
 * @file stm32g474_fault.h
 * @brief STM32G474 SoC-specific crash / fault diagnostics driver.
 *
 * Mirrors @c drivers/rp2040/rp2040_fault.h to keep the @c hal_system
 * layer pure dispatch.
 *
 * @par Status
 * Implemented for the STM32G474 backend:
 *   - reset-reason classification from @c RCC->CSR flags,
 *   - retained fault-frame handoff from @c exception_info (captured by
 *     HardFault/MemManage/BusFault/UsageFault handlers),
 *   - synchronous stack-overflow detection through a 32-byte MPU guard,
 *   - alive-marker persistence used to avoid over-reporting BOR on cold boot.
 *
 * The public surface matches the RP-family driver exactly so the HAL layer
 * is symmetrical across backends.
 */

#include "hal/system/hal_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise and latch reset/fault state for this boot. */
void stm32g474_fault_init(void);

/** Last reset reason, stable after @ref stm32g474_fault_init. */
hal_reset_reason_t stm32g474_fault_get_reset_reason(void);

/** Retrieve captured fault info into @p out. */
bool stm32g474_fault_get_last_fault(hal_fault_info_t *out);

/** Forget any captured fault info. */
void stm32g474_fault_clear_last_fault(void);

/** Whether the previous boot is suspected to have been a brown-out.
 *  STM32G474 has native BOR detection in @c RCC->CSR, so this will become
 *  a direct silicon read rather than the heuristic used on RP2040. */
bool stm32g474_fault_brownout_suspected(void);

/** Refresh the retained alive marker (kept for API symmetry; native BOR
 *  detection on STM32 makes the heuristic optional). */
void stm32g474_fault_alive_mark(void);

/** Install the stack-bottom MPU guard when HAL_ENABLE_STACK_GUARD is enabled.
 */
bool stm32g474_fault_stack_guard_init(void);

/** Compatibility no-op: the MPU reports violations synchronously. */
void stm32g474_fault_stack_guard_check(void);

#ifdef __cplusplus
}
#endif
