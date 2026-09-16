#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pick the half and frame holding the newest complete frame of a
 * double-buffered ADC scan ring filled by DMA.
 * @param busy_half Half being written now, or UINT8_MAX when none is.
 * @param written_samples Samples written into that half so far.
 * @param pin_count Samples per frame, non-zero.
 * @param block_frames Frames per half, non-zero.
 * @param sequence Blocks completed as counted by the completion interrupt.
 * @param completed_half Half that interrupt marked complete last.
 * @param out_half Non-NULL; receives the half to read.
 * @param out_frame Non-NULL; receives the frame index inside that half.
 * @return True when a complete frame exists, false before the first one.
 * @note A half with no complete frame yet sits right after the switch: the
 * other half was filled just before, whether or not its interrupt has run.
 * Reading the interrupt's bookkeeping there would hand out a frame from the
 * block before, up to a whole block old.
 */
static inline bool
jh_adc_scan_latest_frame(uint8_t busy_half, uint32_t written_samples,
                         uint8_t pin_count, uint32_t block_frames,
                         uint32_t sequence, uint8_t completed_half,
                         uint8_t *out_half, uint32_t *out_frame) {
  if (pin_count == 0u || block_frames == 0u) {
    return false;
  }
  if (busy_half < 2u) {
    const uint32_t frames = written_samples / pin_count;
    if (frames >= 1u) {
      *out_half = busy_half;
      *out_frame = frames - 1u;
      return true;
    }
    if (busy_half == 0u && sequence == 0u) {
      return false; /* the very first block, the other half is still empty */
    }
    *out_half = (uint8_t)(1u - busy_half);
    *out_frame = block_frames - 1u;
    return true;
  }
  if (sequence == 0u) {
    return false;
  }
  *out_half = completed_half;
  *out_frame = block_frames - 1u;
  return true;
}

#ifdef __cplusplus
}
#endif
