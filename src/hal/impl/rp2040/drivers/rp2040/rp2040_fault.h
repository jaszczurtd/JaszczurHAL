#pragma once

/**
 * @file rp2040_fault.h
 * @brief RP2040 SoC-specific crash / fault diagnostics driver.
 *
 * Owns the architecture details (Cortex-M0+ HardFault frame layout, retained
 * scratch storage in @c watchdog_hw->scratch[], pico-sdk reset-reason flags,
 * linker-defined stack symbols). The @c hal_system layer talks to this
 * driver through plain function wrappers and never reaches for any RP2040
 * register directly.
 *
 * Retained scratch layout (watchdog_hw->scratch[]):
 *   [0] = state word: upper 24 bits signature 'JHD' (0x4A4844 << 8),
 *                     lower 8  bits flag bits (FAULT, ALIVE, STACK_OVERFLOW)
 *   [1] = stacked PC  at fault (valid iff FLAG_FAULT)
 *   [2] = stacked LR  at fault (valid iff FLAG_FAULT)
 *   [3] = stacked xPSR at fault, or mcause on RP2350 RISC-V
 *   [4..7] = owned by pico-sdk (watchdog magic / watchdog_reboot() args).
 * RP2350 ARM additionally keeps CFSR/HFSR/MMFAR/BFAR in a private retained
 * `.uninitialized_data` record because the four watchdog slots are exhausted.
 */

#include "hal/core/hal_compiler.h"
#include "hal/system/hal_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise: latch reset reason, snapshot any retained fault info into
 *  RAM, then clear the volatile flag bits so the next event is detected
 *  fresh. Idempotent. */
void rp2040_fault_init(void);

/** Last reset reason (stable after rp2040_fault_init). */
hal_reset_reason_t rp2040_fault_get_reset_reason(void);

/** Retrieve captured HardFault info into @p out. Returns false if no info. */
bool rp2040_fault_get_last_fault(hal_fault_info_t *out);

/** Forget any captured HardFault info (does not touch retained storage). */
void rp2040_fault_clear_last_fault(void);

/** True if the previous boot is suspected to have been a brown-out
 *  (silicon reported POR but the alive marker was retained). */
bool rp2040_fault_brownout_suspected(void);

/** Refresh the retained alive marker used by the brown-out heuristic. */
void rp2040_fault_alive_mark(void);

/** Verify the executing core's Pico SDK hardware stack guard configuration. */
hal_status_t rp2040_fault_stack_guard_init(void);

/** Record a stack overflow and reset through retained fault diagnostics. */
HAL_NORETURN void jh_stack_overflow_reset(void);

/** Same terminal path with a best-effort call-site address for retention. */
HAL_NORETURN void jh_stack_overflow_reset_with_context(uintptr_t pc,
                                                       uintptr_t lr);

/** Hardware guards fault synchronously, so periodic checking is unnecessary. */
void rp2040_fault_stack_guard_check(void);

#ifdef __cplusplus
}
#endif
