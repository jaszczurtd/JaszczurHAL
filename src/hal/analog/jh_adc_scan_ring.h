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

/**
 * @brief One DMA channel of the ring, as the reader sampled it.
 * @note The moment a channel finishes, the ring restarts the other one at the
 * base of its half. A reader that saw a channel busy and then read a pointer
 * at the base cannot tell "just triggered, no frame yet" from "just finished,
 * whole half written" by the pointer alone; a second look at the busy flag
 * can, and the second case must not hand out the other half, which is the
 * one being overwritten right then.
 */
typedef struct {
  bool busy_before; /**< Busy when first looked at, before the pointer. */
  bool busy_after;  /**< Busy again after the pointer was read. */
  uint32_t written; /**< Samples the pointer says landed in its half. */
} jh_adc_scan_channel_t;

/**
 * @brief Pick the newest complete frame from both channels' sampled state.
 * @param channels Both channels, indexed by half.
 * @param pin_count Samples per frame, non-zero.
 * @param block_frames Frames per half, non-zero.
 * @param sequence Blocks completed as counted by the completion interrupt.
 * @param completed_half Half that interrupt marked complete last.
 * @param out_half Non-NULL; receives the half to read.
 * @param out_frame Non-NULL; receives the frame index inside that half.
 * @return True when a complete frame exists, false before the first one.
 * @note A channel that went idle between the two looks has filled its half
 * whatever its pointer says now. A busy channel without a complete frame
 * hands over to the other half only when that half's pointer reached its
 * end; before the first block it has not. With no channel busy, a single
 * pointer at the end of its half marks the block that just completed with
 * its interrupt still pending. Two pointers at their ends cannot be ordered:
 * a finished channel keeps its pointer at the end until it is restarted a
 * block later, so between two blocks both look full. Then, and when neither
 * applies, the interrupt's bookkeeping serves; a backend that can look again
 * while no channel is busy, or while both look busy, should do so first: the
 * first is the gap between blocks, the second a snapshot torn by a stall, and
 * the bookkeeping may still name the block before. Bench 2026-09-16: the
 * re-arm race alone put a frame from the block before into one read in
 * about three hundred thousand.
 */
static inline bool
jh_adc_scan_pick_frame(const jh_adc_scan_channel_t channels[2],
                       uint8_t pin_count, uint32_t block_frames,
                       uint32_t sequence, uint8_t completed_half,
                       uint8_t *out_half, uint32_t *out_frame) {
  if (pin_count == 0u || block_frames == 0u) {
    return false;
  }
  const uint32_t full = block_frames * (uint32_t)pin_count;
  for (uint8_t b = 0u; b < 2u; ++b) {
    if (!channels[b].busy_before) {
      continue;
    }
    const uint32_t written =
        channels[b].busy_after ? channels[b].written : full;
    const uint32_t frames = written / pin_count;
    if (frames >= 1u) {
      *out_half = b;
      *out_frame = frames - 1u;
      return true;
    }
    // No complete frame in the block in progress yet. The other half holds
    // the block before exactly when its pointer reached the end: a finished
    // channel keeps it there until restarted, and before the first block it
    // still sits at the base. The interrupt's count says nothing about which
    // half, and may lag behind the ring.
    const uint8_t other = (uint8_t)(1u - b);
    if (channels[other].written < full) {
      return false;
    }
    *out_half = other;
    *out_frame = block_frames - 1u;
    return true;
  }
  const bool full0 = channels[0].written >= full;
  const bool full1 = channels[1].written >= full;
  if (full0 != full1) {
    return jh_adc_scan_latest_frame(full0 ? 0u : 1u, full, pin_count,
                                    block_frames, sequence, completed_half,
                                    out_half, out_frame);
  }
  return jh_adc_scan_latest_frame(UINT8_MAX, 0u, pin_count, block_frames,
                                  sequence, completed_half, out_half,
                                  out_frame);
}

#ifdef __cplusplus
}
#endif
