#pragma once

#include <stdint.h>

/* Masking interrupts holds back the simulated DMA interrupt; the DMA itself
 * keeps moving, as on the chip. */
uint32_t save_and_disable_interrupts(void);
void restore_interrupts(uint32_t status);
