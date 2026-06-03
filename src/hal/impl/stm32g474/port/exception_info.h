#pragma once

/**
 * @file exception_info.h
 * @brief Cortex-M4 fault capture for STM32G474, modelled on teltonika-tdf's
 *        crash_dump / exception_info.
 *
 * On a HardFault/MemManage/BusFault/UsageFault the naked handlers capture the
 * stacked exception frame (R0-R3, R12, LR, PC, xPSR) plus the SCB fault-status
 * registers (CFSR/HFSR/MMFAR/BFAR), store them in a `.noinit` record that
 * survives the subsequent reset, dump a human-readable summary over the debug
 * UART, and reset the MCU.
 *
 * After reboot, exception_info_report_last() prints the previously captured
 * fault (if any) so a crash that happened in the field is visible on the next
 * boot. This mirrors TDF's "retained crash record" approach, adapted from the
 * ARMv8-M (Cortex-M33) original to ARMv7E-M (Cortex-M4): the M4 has the same
 * CFSR/HFSR/MMFAR/BFAR registers, so the capture maps almost 1:1 (the v8-M
 * MSPLIM/PSPLIM stack-limit fields simply do not exist here).
 *
 * Only meaningful on the ARM target (JH_STM32G474_HW).
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Which fault handler captured the record. */
typedef enum {
    JH_FAULT_NONE       = 0,
    JH_FAULT_HARD       = 1,
    JH_FAULT_MEMMANAGE  = 2,
    JH_FAULT_BUS        = 3,
    JH_FAULT_USAGE      = 4,
} jh_fault_kind_t;

/** Captured fault record (kept in .noinit, survives reset). */
typedef struct {
    uint32_t magic;       /**< validity signature */
    uint32_t kind;        /**< jh_fault_kind_t     */
    /* Stacked exception frame. */
    uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr;
    /* SCB fault status. */
    uint32_t cfsr, hfsr, mmfar, bfar, shcsr;
    uint32_t exc_return;  /**< EXC_RETURN (LR on entry): stack/FP context info */
} jh_exception_info_t;

/**
 * @brief Check for a retained fault record and, if present, dump it over the
 *        debug UART, then clear it. Call once early in main() (after
 *        g474_debug_uart_init()).
 * @return true if a fault record was found and reported.
 */
bool exception_info_report_last(void);

/** @brief Read-only pointer to the retained record (NULL semantics via .kind). */
const jh_exception_info_t *exception_info_last(void);

#ifdef __cplusplus
}
#endif
