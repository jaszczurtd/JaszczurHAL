#pragma once

/**
 * @file stm32g474_fdcan_timing.h
 * @brief STM32G4 FDCAN bit-timing ranges and register encoding.
 *
 * The search itself is the shared jh_can_compute_timing(); this header gives
 * it the NBTP/DBTP field ranges of RM0440 and encodes the result.
 */

#include "hal/can/jh_can_bit_timing.h"

typedef jh_can_timing_t jh_stm32g474_fdcan_timing_t;

/** @brief NBTP (arbitration) and DBTP (data) ranges, RM0440 44.4. */
static const jh_can_timing_limits_t kJhStm32g474FdcanNominalLimits = {
    512u, 2u, 256u, 128u, 128u, JH_CAN_NOMINAL_SP_PERMILLE};
static const jh_can_timing_limits_t kJhStm32g474FdcanDataLimits = {
    32u, 1u, 32u, 16u, 16u, JH_CAN_DATA_SP_PERMILLE};

/**
 * @brief Find the FDCAN bit timing for one phase.
 * @param kernel_clock_hz FDCAN kernel clock after CKDIV.
 * @param bitrate_hz Requested bitrate; it must be met within 0.5 %.
 * @param data_phase Use the DBTP (data) limits instead of NBTP.
 * @param sample_point_permille Target sample point; 0 selects the default of
 *        the phase.
 * @param preferred_prescaler Prescaler to use when it gives an exact bitrate
 *        (the arbitration prescaler for the data phase); 0 for none.
 * @param[out] out Chosen timing; untouched on error.
 * @return HAL_OK, HAL_EINVAL for a zero clock or bitrate or NULL @p out, or
 *         HAL_EUNSUPPORTED when no timing meets the bitrate.
 */
static inline hal_status_t jh_stm32g474_fdcan_compute_timing(
    uint32_t kernel_clock_hz, uint32_t bitrate_hz, bool data_phase,
    uint16_t sample_point_permille, uint16_t preferred_prescaler,
    jh_stm32g474_fdcan_timing_t *out) {
  return jh_can_compute_timing(kernel_clock_hz, bitrate_hz,
                               data_phase ? &kJhStm32g474FdcanDataLimits
                                          : &kJhStm32g474FdcanNominalLimits,
                               sample_point_permille, preferred_prescaler, out);
}

/**
 * @brief Transmitter delay compensation offset for a data-phase timing.
 *
 * TDC is meant for data bitrates above 1 Mbit/s with a data prescaler of 1 or
 * 2 (CiA). The offset puts the secondary sample point at the regular sample
 * point, counted in kernel clock periods, as Zephyr's CAN_CALC_TDCO does.
 *
 * @param timing Data-phase timing.
 * @return TDCO in mtq (1..127), or 0 when TDC should stay off.
 */
static inline uint8_t
jh_stm32g474_fdcan_tdc_offset(const jh_stm32g474_fdcan_timing_t *timing) {
  if (timing == NULL || timing->prescaler > 2u ||
      timing->actual_bitrate_hz <= JH_CAN_TDC_MIN_BITRATE_HZ) {
    return 0u;
  }
  const uint32_t offset =
      (1u + (uint32_t)timing->segment1) * (uint32_t)timing->prescaler;
  return (uint8_t)(offset > 127u ? 127u : offset);
}

static inline uint32_t
jh_stm32g474_fdcan_encode_nbtp(const jh_stm32g474_fdcan_timing_t *timing) {
  return ((uint32_t)(timing->sync_jump_width - 1u) << 25u) |
         ((uint32_t)(timing->prescaler - 1u) << 16u) |
         ((uint32_t)(timing->segment1 - 1u) << 8u) |
         (uint32_t)(timing->segment2 - 1u);
}

/** @brief DBTP value; @p tdc sets the TDC enable bit (23). */
static inline uint32_t
jh_stm32g474_fdcan_encode_dbtp(const jh_stm32g474_fdcan_timing_t *timing,
                               bool tdc) {
  return (tdc ? (1u << 23u) : 0u) |
         ((uint32_t)(timing->prescaler - 1u) << 16u) |
         ((uint32_t)(timing->segment1 - 1u) << 8u) |
         ((uint32_t)(timing->segment2 - 1u) << 4u) |
         (uint32_t)(timing->sync_jump_width - 1u);
}
