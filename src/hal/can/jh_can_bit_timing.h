#pragma once

/**
 * @file jh_can_bit_timing.h
 * @brief Bit-timing search shared by the CAN controllers (internal).
 *
 * Each controller describes its bit-time register ranges in a
 * jh_can_timing_limits_t (values as the hardware uses them, i.e. register
 * value + 1). The search follows the CiA CAN FD bit-timing recommendations:
 * an exact bitrate first, then the lowest prescaler (most time quanta) whose
 * sample point lies within JH_CAN_SP_TOLERANCE_PERMILLE of the target; without
 * such a candidate the sample point nearest the target wins. SJW is as large
 * as both phase segments allow.
 */

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Bit timing of one phase, in time quanta. */
typedef struct {
  uint16_t prescaler;
  uint16_t segment1; /**< Prop_Seg + Phase_Seg1 in time quanta. */
  uint16_t segment2; /**< Phase_Seg2 in time quanta. */
  uint16_t sync_jump_width;
  uint32_t actual_bitrate_hz;
  uint16_t sample_point_permille;
} jh_can_timing_t;

/** @brief Register ranges of one phase of a controller. */
typedef struct {
  uint16_t max_prescaler;
  uint16_t min_segment1;
  uint16_t max_segment1;
  uint16_t max_segment2;
  uint16_t max_sjw;
  /** Sample point used when the caller passes 0. */
  uint16_t default_sample_point_permille;
} jh_can_timing_limits_t;

/** @brief Default sample point of the arbitration phase (CiA: at most 80 %). */
#define JH_CAN_NOMINAL_SP_PERMILLE 800u
/** @brief Default sample point of the data phase. */
#define JH_CAN_DATA_SP_PERMILLE 750u
/** @brief Sample point distance a low prescaler may cost. */
#define JH_CAN_SP_TOLERANCE_PERMILLE 15u
/** @brief Data bitrates above this use transmitter delay compensation. */
#define JH_CAN_TDC_MIN_BITRATE_HZ 1000000u

static inline uint32_t jh_can_abs_diff_u32(uint32_t a, uint32_t b) {
  return a > b ? a - b : b - a;
}

/**
 * @brief Find the bit timing for one phase.
 * @param clock_hz Controller clock the prescaler divides.
 * @param bitrate_hz Requested bitrate; it must be met within 0.5 %.
 * @param limits Register ranges of the phase.
 * @param sample_point_permille Target sample point; 0 selects the default of
 *        @p limits.
 * @param preferred_prescaler Prescaler to use when it gives an exact bitrate
 *        (the arbitration prescaler for the data phase); 0 for none.
 * @param[out] out Chosen timing; untouched on error.
 * @return HAL_OK, HAL_EINVAL for a zero clock or bitrate or a NULL pointer,
 *         or HAL_EUNSUPPORTED when no timing meets the bitrate.
 */
static inline hal_status_t
jh_can_compute_timing(uint32_t clock_hz, uint32_t bitrate_hz,
                      const jh_can_timing_limits_t *limits,
                      uint16_t sample_point_permille,
                      uint16_t preferred_prescaler, jh_can_timing_t *out) {
  if (clock_hz == 0u || bitrate_hz == 0u || limits == NULL || out == NULL) {
    return HAL_EINVAL;
  }

  const uint32_t min_segment1 = limits->min_segment1;
  const uint32_t max_segment1 = limits->max_segment1;
  const uint32_t max_segment2 = limits->max_segment2;
  const uint32_t target = sample_point_permille != 0u
                              ? sample_point_permille
                              : limits->default_sample_point_permille;
  const uint32_t max_total_quanta = 1u + max_segment1 + max_segment2;
  const uint32_t max_error = bitrate_hz / 200u > 0u ? bitrate_hz / 200u : 1u;

  bool found = false;
  uint32_t best_rate_error = 0u;
  uint32_t best_sample_error = 0u;
  jh_can_timing_t best = {0u, 0u, 0u, 0u, 0u, 0u};

  for (uint32_t prescaler = 1u; prescaler <= limits->max_prescaler;
       ++prescaler) {
    const uint32_t quanta_hz = clock_hz / prescaler;
    if (quanta_hz == 0u) {
      break;
    }
    /* Nearest whole number of quanta per bit. */
    const uint32_t total = (quanta_hz + (bitrate_hz / 2u)) / bitrate_hz;
    if (total < 4u || total > max_total_quanta) {
      continue;
    }
    const uint64_t divisor = (uint64_t)prescaler * total;
    const uint32_t actual =
        (uint32_t)(((uint64_t)clock_hz + (divisor / 2u)) / divisor);
    const uint32_t rate_error = jh_can_abs_diff_u32(actual, bitrate_hz);
    if (rate_error > max_error) {
      continue;
    }
    uint32_t sample_quanta = ((total * target) + 500u) / 1000u;
    if (sample_quanta < min_segment1 + 1u) {
      sample_quanta = min_segment1 + 1u;
    }
    if (sample_quanta > total - 1u) {
      sample_quanta = total - 1u;
    }
    uint32_t segment1 = sample_quanta - 1u;
    uint32_t segment2 = total - sample_quanta;
    if (segment1 > max_segment1) {
      segment1 = max_segment1;
      segment2 = total - 1u - segment1;
    }
    if (segment2 > max_segment2) {
      segment2 = max_segment2;
      segment1 = total - 1u - segment2;
    }
    if (segment1 < min_segment1 || segment1 > max_segment1 || segment2 < 1u ||
        segment2 > max_segment2) {
      continue;
    }
    const uint32_t sample_permille = ((1u + segment1) * 1000u) / total;
    const uint32_t sample_error = jh_can_abs_diff_u32(sample_permille, target);
    const bool sample_ok = sample_error <= JH_CAN_SP_TOLERANCE_PERMILLE;
    const bool best_ok =
        found && best_sample_error <= JH_CAN_SP_TOLERANCE_PERMILLE;
    const bool preferred = preferred_prescaler != 0u &&
                           prescaler == preferred_prescaler &&
                           rate_error == 0u && sample_ok;
    /* Prescalers grow, so an equal candidate never replaces a lower one. */
    bool better = !found || preferred || rate_error < best_rate_error;
    if (!better && rate_error == best_rate_error) {
      better =
          sample_ok ? !best_ok : (!best_ok && sample_error < best_sample_error);
    }
    if (better) {
      found = true;
      best_rate_error = rate_error;
      best_sample_error = sample_error;
      best.prescaler = (uint16_t)prescaler;
      best.segment1 = (uint16_t)segment1;
      best.segment2 = (uint16_t)segment2;
      uint32_t sjw = segment2 < segment1 ? segment2 : segment1;
      best.sync_jump_width =
          (uint16_t)(sjw < limits->max_sjw ? sjw : limits->max_sjw);
      best.actual_bitrate_hz = actual;
      best.sample_point_permille = (uint16_t)sample_permille;
      if (preferred) {
        break;
      }
    }
  }
  if (!found) {
    return HAL_EUNSUPPORTED;
  }
  *out = best;
  return HAL_OK;
}
