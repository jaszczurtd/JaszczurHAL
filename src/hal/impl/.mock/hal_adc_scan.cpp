#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_ADC_SCAN
#include "hal/analog/jh_adc_scan_backend.h"
#include "hal/system/hal_system.h"
#include "hal_mock.h"

#include <string.h>

namespace {

// The mock has no converter: tests hand in finished blocks, which land in
// the two halves of the caller buffer alternately like the DMA ring would.
// Frames keep the configured pin order, the simplest of the backend orders.
struct {
  hal_adc_scan_config_t config;
  uint32_t samples_per_block;
  bool started;
  uint8_t next;
  uint32_t sequence;
  uint8_t completed;
  uint32_t completed_us;
  uint32_t marker;
} s = {};

uint16_t *half(uint8_t index) {
  return s.config.buffer + ((uint32_t)index * s.samples_per_block);
}

} // namespace

hal_status_t jh_adc_scan_start(const hal_adc_scan_config_t *config,
                               uint8_t *positions, uint32_t *frame_period_ns) {
  if (s.started) {
    return HAL_EBUSY;
  }
  s.config = *config;
  s.samples_per_block = config->block_frames * (uint32_t)config->pin_count;
  s.started = true;
  s.next = 0u;
  s.sequence = 0u;
  s.completed = 0u;
  s.completed_us = 0u;
  s.marker = 0u;
  for (uint8_t i = 0u; i < config->pin_count; ++i) {
    positions[i] = i;
  }
  *frame_period_ns = config->conversion_period_ns * (uint32_t)config->pin_count;
  return HAL_OK;
}

hal_status_t jh_adc_scan_stop(void) {
  s.started = false;
  return HAL_OK;
}

bool jh_adc_scan_completed(hal_adc_scan_block_t *block) {
  if (!s.started || s.sequence == 0u) {
    return false;
  }
  jh_adc_scan_describe(block, half(s.completed), s.config.block_frames,
                       s.config.pin_count, s.sequence, s.completed_us,
                       s.marker);
  return true;
}

hal_status_t jh_adc_scan_latest(uint8_t position, uint16_t *raw) {
  if (!s.started || position >= s.config.pin_count) {
    return HAL_ESTATE;
  }
  if (s.sequence == 0u) {
    return HAL_EAGAIN;
  }
  *raw = half(s.completed)[((s.config.block_frames - 1u) * s.config.pin_count) +
                           position];
  return HAL_OK;
}

hal_status_t hal_mock_adc_scan_complete(const uint16_t *samples,
                                        uint32_t frames) {
  if (samples == NULL) {
    return HAL_EINVAL;
  }
  if (!s.started) {
    return HAL_ESTATE;
  }
  if (frames != s.config.block_frames) {
    return HAL_EINVAL;
  }
  (void)memcpy(half(s.next), samples,
               (size_t)s.samples_per_block * sizeof(uint16_t));
  s.marker =
      s.config.marker != NULL ? s.config.marker(s.config.marker_user) : 0u;
  s.completed_us = hal_micros();
  s.completed = s.next;
  s.next = (uint8_t)(1u - s.next);
  ++s.sequence;
  return HAL_OK;
}

#endif // HAL_ENABLE_ADC_SCAN
#endif // HAL_TARGET_IS_MOCK
