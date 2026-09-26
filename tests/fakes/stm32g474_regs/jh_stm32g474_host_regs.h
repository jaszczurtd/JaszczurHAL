#pragma once

/* Register table behind the STM32G474 register macros in host tests
 * (JH_STM32G474_HOST_REGS). Any address maps to its own cell, created on
 * first use; the test reads and writes cells through the same macros as the
 * code under test, and clears the table between cases. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

volatile uint8_t *jh_stm32g474_host_reg8(uintptr_t address);
volatile uint16_t *jh_stm32g474_host_reg16(uintptr_t address);
volatile uint32_t *jh_stm32g474_host_reg32(uintptr_t address);

/* Forget every cell. */
void jh_stm32g474_host_regs_reset(void);

#ifdef __cplusplus
}
#endif
