#pragma once

/* Test control of the simulated RP2040 DMA, its interrupt and the ADC
 * converter feeding a scan through the FIFO. The converter produces samples
 * only when a test feeds them; each sample carries its own index as its
 * value, so a test can tell which frame any value came from. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void jh_fake_dma_reset(void);

/* Region the DMA may write; writes elsewhere are counted, not performed. */
void jh_fake_dma_set_legal_region(const void *begin, size_t bytes);
uint32_t jh_fake_dma_writes_outside_region(void);

/* Converter output: each sample goes to the busy channel reading the FIFO. */
void jh_fake_adc_feed(uint32_t samples);
uint32_t jh_fake_adc_samples_fed(void);
uint32_t jh_fake_adc_samples_dropped(void);

/* Feed samples right before the n-th register access after the counter was
 * last reset (0 = before the first access); fires once. */
void jh_fake_dma_schedule_feed(uint32_t access_index, uint32_t samples);
void jh_fake_dma_reset_access_count(void);
uint32_t jh_fake_dma_access_count(void);

uint32_t jh_fake_dma_claimed_channels(void);
uint32_t jh_fake_dma_channel_triggers(unsigned int channel);

uint32_t jh_fake_irq_handler_calls(void);
bool jh_fake_irq_masked(void);
bool jh_fake_irq_enabled(void);

bool jh_fake_adc_running(void);
unsigned int jh_fake_adc_round_robin(void);
float jh_fake_adc_clkdiv(void);
bool jh_fake_adc_temperature_enabled(void);
