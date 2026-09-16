#pragma once
#include "hal/analog/hal_adc_scan.h"

/* Internal backend interface. Calls are serialized by the public facade,
 * which validated the configuration. The backend owns the converter, DMA
 * and completion interrupt between start and stop, decides the frame order
 * of the configured pins and keeps the state of the newest completed block.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Claim resources and start filling the first half of config->buffer.
 * positions[i] receives the frame position of config->pins[i]; frame_period_ns
 * the time between two samples of one pin as the converter runs. */
hal_status_t jh_adc_scan_start(const hal_adc_scan_config_t *config,
                               uint8_t *positions, uint32_t *frame_period_ns);
/* Idempotent, including before start and after a failed/partial start. */
hal_status_t jh_adc_scan_stop(void);
/* Fill the newest completed block; false when none completed yet. */
bool jh_adc_scan_completed(hal_adc_scan_block_t *block);
/* Newest sample at the given frame position. */
hal_status_t jh_adc_scan_latest(uint8_t position, uint16_t *raw);

/* Describe a completed half for the facade; the same shape on every backend. */
static inline void
jh_adc_scan_describe(hal_adc_scan_block_t *block, const uint16_t *samples,
                     uint32_t frames, uint8_t pin_count, uint32_t sequence,
                     uint32_t completed_us, uint32_t marker) {
  block->samples = samples;
  block->frames = frames;
  block->pin_count = pin_count;
  block->sequence = sequence;
  block->completed_us = completed_us;
  block->marker = marker;
}

#ifdef __cplusplus
}
#endif
