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

/* Function access (JH_REG32_RD/JH_REG32_WR). Without a hook it reads and
 * writes the cell; a hook models the register's side effects. */
uint32_t jh_stm32g474_host_read32(uintptr_t address);
void jh_stm32g474_host_write32(uintptr_t address, uint32_t value);

/* Called on every function read; returns the value the code sees. The cell
 * holds what was last stored. */
typedef uint32_t (*jh_stm32g474_host_read_hook_t)(uintptr_t address,
                                                  uint32_t cell);
/* Called on every function write; stores into *cell what the register keeps
 * (or leaves it alone for a write the hardware ignores). */
typedef void (*jh_stm32g474_host_write_hook_t)(uintptr_t address,
                                               uint32_t value, uint32_t *cell);
void jh_stm32g474_host_regs_set_hooks(jh_stm32g474_host_read_hook_t read,
                                      jh_stm32g474_host_write_hook_t write);

/* Forget every cell and hook. */
void jh_stm32g474_host_regs_reset(void);

#ifdef __cplusplus
}
#endif
