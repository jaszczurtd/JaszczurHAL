#pragma once

/**
 * @file stm32g474_fdcan_timing.h
 * @brief Pure STM32G4 FDCAN bit-timing search and register encoding.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint16_t prescaler;
  uint16_t segment1;
  uint16_t segment2;
  uint16_t sync_jump_width;
  uint32_t actual_bitrate_hz;
} jh_stm32g474_fdcan_timing_t;

static inline uint32_t jh_stm32g474_abs_diff_u32(uint32_t a, uint32_t b) {
  return a > b ? a - b : b - a;
}

static inline bool
jh_stm32g474_fdcan_compute_timing(uint32_t kernel_clock_hz, uint32_t bitrate_hz,
                                  bool data_phase,
                                  jh_stm32g474_fdcan_timing_t *out) {
  if (kernel_clock_hz == 0u || bitrate_hz == 0u || out == NULL) {
    return false;
  }

  const uint32_t max_prescaler = data_phase ? 32u : 512u;
  const uint32_t max_segment1 = data_phase ? 32u : 256u;
  const uint32_t max_segment2 = data_phase ? 16u : 128u;
  const uint32_t max_sjw = data_phase ? 8u : 16u;
  const uint32_t target_sample_per_mille = data_phase ? 750u : 800u;
  const uint32_t max_total_quanta = 1u + max_segment1 + max_segment2;

  bool found = false;
  uint32_t best_bitrate_error = UINT32_MAX;
  uint32_t best_sample_error = UINT32_MAX;
  uint32_t best_total_quanta = 0u;
  jh_stm32g474_fdcan_timing_t best = {0u, 0u, 0u, 0u, 0u};

  for (uint32_t prescaler = 1u; prescaler <= max_prescaler; ++prescaler) {
    for (uint32_t total_quanta = 3u; total_quanta <= max_total_quanta;
         ++total_quanta) {
      const uint32_t divisor = prescaler * total_quanta;
      const uint32_t actual =
          (uint32_t)(((uint64_t)kernel_clock_hz + (divisor / 2u)) / divisor);
      const uint32_t bitrate_error =
          jh_stm32g474_abs_diff_u32(actual, bitrate_hz);

      uint32_t sample_quanta =
          ((total_quanta * target_sample_per_mille) + 500u) / 1000u;
      if (sample_quanta < 2u) {
        sample_quanta = 2u;
      }
      uint32_t segment1 = sample_quanta - 1u;
      uint32_t segment2 = total_quanta - sample_quanta;
      if (segment2 < 1u) {
        segment2 = 1u;
        segment1 = total_quanta - 2u;
      }
      if (segment1 > max_segment1) {
        segment1 = max_segment1;
        segment2 = total_quanta - 1u - segment1;
      }
      if (segment1 < 1u || segment2 < 1u || segment2 > max_segment2) {
        continue;
      }

      const uint32_t sample_error = jh_stm32g474_abs_diff_u32(
          (1u + segment1) * 1000u, target_sample_per_mille * total_quanta);
      const bool better_sample =
          !found || ((uint64_t)sample_error * best_total_quanta <
                     (uint64_t)best_sample_error * total_quanta);
      const bool same_sample =
          found && ((uint64_t)sample_error * best_total_quanta ==
                    (uint64_t)best_sample_error * total_quanta);
      if (!found || bitrate_error < best_bitrate_error ||
          (bitrate_error == best_bitrate_error && better_sample) ||
          (bitrate_error == best_bitrate_error && same_sample &&
           total_quanta > best_total_quanta)) {
        found = true;
        best_bitrate_error = bitrate_error;
        best_sample_error = sample_error;
        best_total_quanta = total_quanta;
        best.prescaler = (uint16_t)prescaler;
        best.segment1 = (uint16_t)segment1;
        best.segment2 = (uint16_t)segment2;
        best.sync_jump_width =
            (uint16_t)(segment2 < max_sjw ? segment2 : max_sjw);
        best.actual_bitrate_hz = actual;
      }
    }
  }

  /* Reject configurations farther than 0.5% from the requested bitrate. */
  const uint32_t max_error = bitrate_hz / 200u;
  if (!found || best_bitrate_error > (max_error > 0u ? max_error : 1u)) {
    return false;
  }

  *out = best;
  return true;
}

static inline uint32_t
jh_stm32g474_fdcan_encode_nbtp(const jh_stm32g474_fdcan_timing_t *timing) {
  return ((uint32_t)(timing->sync_jump_width - 1u) << 25u) |
         ((uint32_t)(timing->prescaler - 1u) << 16u) |
         ((uint32_t)(timing->segment1 - 1u) << 8u) |
         (uint32_t)(timing->segment2 - 1u);
}

static inline uint32_t
jh_stm32g474_fdcan_encode_dbtp(const jh_stm32g474_fdcan_timing_t *timing) {
  return ((uint32_t)(timing->prescaler - 1u) << 16u) |
         ((uint32_t)(timing->segment1 - 1u) << 8u) |
         ((uint32_t)(timing->segment2 - 1u) << 4u) |
         (uint32_t)(timing->sync_jump_width - 1u);
}
