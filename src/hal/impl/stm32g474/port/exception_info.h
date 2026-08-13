#pragma once

/**
 * @file exception_info.h
 * @brief Cortex-M4 fault capture for STM32G474.
 *
 * On a HardFault/MemManage/BusFault/UsageFault the naked handlers capture the
 * valid stacked exception frame (R0-R3, R12, LR, PC, xPSR) plus the SCB
 * fault-status registers (CFSR/HFSR/MMFAR/BFAR), store them in a `.noinit`
 * record that survives the subsequent reset, and reset the MCU. Fault entry
 * switches to a dedicated CCMRAM emergency stack; reporting happens after the
 * next boot so fault handling never blocks on UART.
 *
 * After reboot, exception_info_report_last() prints the previously captured
 * fault (if any) so a crash that happened in the field is visible on the next
 * boot. This implements a "retained crash record" approach, adapted from the
 * ARMv8-M (Cortex-M33) original to ARMv7E-M (Cortex-M4): the M4 has the same
 * CFSR/HFSR/MMFAR/BFAR registers, so the capture maps almost 1:1 (the v8-M
 * MSPLIM/PSPLIM stack-limit fields simply do not exist here).
 *
 * Only meaningful on the ARM target (JH_STM32G474_HW).
 */

#include <stdbool.h>
#include <stdint.h>

#include "hal/core/hal_compiler.h"

/** Dedicated CCMRAM stack used only by terminal fault/reset paths. */
#define JH_STM32_FAULT_STACK_BYTES 512u

#ifdef __cplusplus
extern "C" {
#endif

/** Which fault handler captured the record. */
typedef enum {
  JH_FAULT_NONE = 0,
  JH_FAULT_HARD = 1,
  JH_FAULT_MEMMANAGE = 2,
  JH_FAULT_BUS = 3,
  JH_FAULT_USAGE = 4,
} jh_fault_kind_t;

/** Captured fault record (kept in .noinit, survives reset). */
typedef struct {
  uint32_t magic; /**< validity signature */
  uint32_t kind;  /**< jh_fault_kind_t     */
  /* Stacked exception frame. */
  uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr;
  /* SCB fault status. */
  uint32_t cfsr, hfsr, mmfar, bfar, shcsr;
  uint32_t exc_return; /**< EXC_RETURN (LR on entry): stack/FP context info */
  uint32_t raw_sp; /**< MSP/PSP selected on entry, before stack replacement */
} jh_exception_info_t;

/** Emergency-stack storage owned by the fault driver. */
extern uint8_t jh_stm32_fault_emergency_stack[JH_STM32_FAULT_STACK_BYTES];

/**
 * @brief Check for a retained fault record and, if present, dump it over the
 *        debug UART, then clear it. Call once early in main() (after
 *        g474_debug_uart_init()).
 * @return true if a fault record was found and reported.
 */
bool exception_info_report_last(void);

/**
 * @brief Print an already-latched fault record over the debug UART.
 *
 * Unlike @ref exception_info_report_last, this does not access or consume the
 * retained `.noinit` slot. It is used after early boot has safely copied that
 * slot into ordinary RAM.
 *
 * @return true when @p record was non-NULL and contained a fault kind.
 */
bool exception_info_report_record(const jh_exception_info_t *record);

/** @brief Read-only pointer to the retained record (NULL semantics via .kind).
 */
const jh_exception_info_t *exception_info_last(void);

/**
 * @brief Consume the retained fault record without printing.
 *
 * Copies the retained record into @p out (when non-NULL), then clears the
 * retained marker so the same record is not reported again.
 *
 * @return true when a valid retained record was consumed.
 */
bool exception_info_take_last(jh_exception_info_t *out);

/** Invalidate a retained record without reporting it. */
void exception_info_discard_last(void) HAL_NO_STACK_PROTECTOR;

/** Shared live/post-boot classifier for an STM32 MPU stack-guard hit. */
bool jh_stm32_fault_hits_stack_guard(const jh_exception_info_t *record)
    HAL_NO_STACK_PROTECTOR;

/** Terminal reset path used after a Cortex-M hardware stack-guard fault. */
void jh_stm32_stack_fault_reset(const jh_exception_info_t *record)
    HAL_NORETURN HAL_NO_STACK_PROTECTOR;

#ifdef __cplusplus
}
#endif
