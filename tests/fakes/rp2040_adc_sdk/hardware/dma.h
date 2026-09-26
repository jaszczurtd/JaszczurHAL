#pragma once

#include "hardware/structs/dma.h"

#include <stdbool.h>
#include <stdint.h>

typedef unsigned int uint;

enum dma_channel_transfer_size {
  DMA_SIZE_8 = 0,
  DMA_SIZE_16 = 1,
  DMA_SIZE_32 = 2
};

#define DREQ_ADC 36u
#define DREQ_FORCE 0x3fu

typedef struct {
  uint channel;
  uint data_size;
  bool read_increment;
  bool write_increment;
  uint dreq;
  uint chain_to;
  bool irq_quiet;
} dma_channel_config;

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
static inline void channel_config_set_chain_to(dma_channel_config *config,
                                               uint chain_to) {
  config->chain_to = chain_to;
}
static inline void channel_config_set_irq_quiet(dma_channel_config *config,
                                                bool quiet) {
  config->irq_quiet = quiet;
}

void dma_channel_configure(uint channel, const dma_channel_config *config,
                           volatile void *write_addr,
                           const volatile void *read_addr, uint transfer_count,
                           bool trigger);
void dma_channel_set_irq0_enabled(uint channel, bool enabled);
int dma_claim_unused_channel(bool required);
void dma_channel_unclaim(uint channel);
void dma_channel_start(uint channel);
void dma_channel_abort(uint channel);
bool dma_channel_is_busy(uint channel);
