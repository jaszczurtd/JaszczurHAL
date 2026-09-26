#pragma once

/* Simulated RP2040 DMA register block. Every register is an object whose
 * reads and writes pass through the fake, so a test can move the simulated
 * DMA between any two accesses of the code under test. Registers hold a
 * whole host pointer where the chip holds a 32-bit address. */

#include <stdint.h>

#define NUM_DMA_CHANNELS 12u
#define DMA_CH0_CTRL_TRIG_BUSY_BITS 0x01000000u

struct jh_fake_dma_reg {
  uintptr_t value;
  operator uintptr_t() const;
  jh_fake_dma_reg &operator=(uintptr_t next);
};

typedef struct {
  jh_fake_dma_reg read_addr;
  jh_fake_dma_reg write_addr;
  jh_fake_dma_reg transfer_count;
  jh_fake_dma_reg ctrl_trig;
  jh_fake_dma_reg al1_ctrl;
  jh_fake_dma_reg al1_read_addr;
  jh_fake_dma_reg al1_write_addr;
  jh_fake_dma_reg al1_transfer_count_trig;
  jh_fake_dma_reg al2_ctrl;
  jh_fake_dma_reg al2_transfer_count;
  jh_fake_dma_reg al2_read_addr;
  jh_fake_dma_reg al2_write_addr_trig;
  jh_fake_dma_reg al3_ctrl;
  jh_fake_dma_reg al3_write_addr;
  jh_fake_dma_reg al3_transfer_count;
  jh_fake_dma_reg al3_read_addr_trig;
} dma_channel_hw_t;

typedef struct {
  dma_channel_hw_t ch[NUM_DMA_CHANNELS];
  jh_fake_dma_reg inte0;
  jh_fake_dma_reg ints0;
} dma_hw_t;

extern dma_hw_t jh_fake_dma_hw;
#define dma_hw (&jh_fake_dma_hw)
