#pragma once

/**
 * @file stm32g474_reset.h
 * @brief Software reset of the STM32G474 (SYSRESETREQ).
 */

#include "hal/core/hal_compiler.h"
#include "stm32g474_regs.h"

/**
 * @brief Request a system reset and wait for it.
 *
 * Writes AIRCR with VECTKEY and SYSRESETREQ, keeping the priority grouping.
 * The next boot sees RCC_CSR.SFTRSTF, which the HAL reports as
 * HAL_RESET_REASON_SOFT. Usable from fault handlers: no stack or library use.
 */
static inline HAL_NORETURN void jh_stm32g474_system_reset(void) {
  __asm volatile("dsb" ::: "memory");
  SCB_AIRCR = (SCB_AIRCR & 0x700u) | SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ;
  __asm volatile("dsb" ::: "memory");
  __asm volatile("isb" ::: "memory");
  for (;;) {
    __asm volatile("nop");
  }
}
