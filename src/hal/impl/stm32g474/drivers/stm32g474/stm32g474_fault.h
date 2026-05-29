#pragma once

/**
 * @file stm32g474_fault.h
 * @brief STM32G474 SoC-specific crash / fault diagnostics driver.
 *
 * Mirrors @c drivers/rp2040/rp2040_fault.h to keep the @c hal_system
 * layer pure dispatch.
 *
 * @par Status
 * No-op stub. A real implementation will use:
 *   - @c RCC->CSR for the reset reason (including real @c BORRSTF
 *     brown-out detection, unlike RP2040 silicon),
 *   - @c SCB->{CFSR,HFSR,MMFAR,BFAR} for the richer Cortex-M4 HardFault
 *     info captured by a naked-ASM @c HardFault_Handler trampoline,
 *   - @c TAMP->BKP0R..BKP3R backup registers for retained storage (survive
 *     all resets except VBAT loss),
 *   - stack canary at the @c _estack / @c _Min_Stack_Size linker symbols.
 *
 * The public surface matches the RP2040 driver exactly so the HAL layer
 * is symmetrical across backends.
 */

#include "../../../../hal_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise: stub returns immediately. */
void stm32g474_fault_init(void);

/** Last reset reason. Stub returns @c HAL_RESET_REASON_UNKNOWN. */
hal_reset_reason_t stm32g474_fault_get_reset_reason(void);

/** Retrieve captured HardFault info into @p out. Stub returns @c false. */
bool stm32g474_fault_get_last_fault(hal_fault_info_t *out);

/** Forget any captured HardFault info. Stub: no-op. */
void stm32g474_fault_clear_last_fault(void);

/** Whether the previous boot is suspected to have been a brown-out.
 *  STM32G474 has native BOR detection in @c RCC->CSR, so this will become
 *  a direct silicon read rather than the heuristic used on RP2040. */
bool stm32g474_fault_brownout_suspected(void);

/** Refresh the retained alive marker (kept for API symmetry; native BOR
 *  detection on STM32 makes the heuristic optional). */
void stm32g474_fault_alive_mark(void);

/** Install the stack-bottom canary. Stub returns @c false. */
bool stm32g474_fault_stack_guard_init(void);

/** Verify the stack canary. Stub: no-op. */
void stm32g474_fault_stack_guard_check(void);

#ifdef __cplusplus
}
#endif
