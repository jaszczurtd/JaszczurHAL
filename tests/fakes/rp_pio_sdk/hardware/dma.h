#pragma once

/* Simulated DMA for PIO-paced channels. TRANS_COUNT behaves as on the
 * modelled chip: RP2040 counts all 32 bits down; RP2350 keeps a MODE field in
 * bits 31:28, counts only bits 27:0 in normal mode and never changes the
 * register in ENDLESS mode (0xF). */

#include "pico/platform.h"

#include <stdbool.h>
#include <stdint.h>

#if !PICO_RP2040
#define DMA_CH0_TRANS_COUNT_MODE_BITS 0xf0000000u
#define DMA_CH0_TRANS_COUNT_COUNT_BITS 0x0fffffffu
#endif

enum dma_channel_transfer_size {
  DMA_SIZE_8 = 0,
  DMA_SIZE_16 = 1,
  DMA_SIZE_32 = 2
};

typedef struct {
  uint data_size;
  bool read_increment;
  bool write_increment;
  uint dreq;
  bool ring_write;
  uint ring_bits;
} dma_channel_config;

typedef struct {
  volatile uint32_t transfer_count;
} dma_channel_hw_t;

dma_channel_hw_t *dma_channel_hw_addr(uint channel);
dma_channel_config dma_channel_get_default_config(uint channel);

static inline void
channel_config_set_transfer_data_size(dma_channel_config *config,
                                      enum dma_channel_transfer_size size) {
  config->data_size = (uint)size;
}
static inline void channel_config_set_read_increment(dma_channel_config *config,
                                                     bool increment) {
  config->read_increment = increment;
}
static inline void
channel_config_set_write_increment(dma_channel_config *config, bool increment) {
  config->write_increment = increment;
}
static inline void channel_config_set_dreq(dma_channel_config *config,
                                           uint dreq) {
  config->dreq = dreq;
}
static inline void channel_config_set_ring(dma_channel_config *config,
                                           bool write, uint size_bits) {
  config->ring_write = write;
  config->ring_bits = size_bits;
}

void dma_channel_configure(uint channel, const dma_channel_config *config,
                           volatile void *write_addr,
                           const volatile void *read_addr, uint transfer_count,
                           bool trigger);
int dma_claim_unused_channel(bool required);
void dma_channel_unclaim(uint channel);
void dma_channel_abort(uint channel);
