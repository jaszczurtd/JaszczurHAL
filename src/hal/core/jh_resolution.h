#pragma once

#include "hal/core/hal_assert.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Clamp a resolution to 1..16 bits and optionally assert on correction.
 * @param bits Requested resolution.
 * @param below_range_message Assertion message for values below one, or NULL.
 * @param above_range_message Assertion message for values above 16, or NULL.
 * @return Resolution clamped to the supported range.
 */
static inline uint8_t
jh_resolution_clamp_1_16(uint8_t bits, const char *below_range_message,
                         const char *above_range_message) {
  if (bits < 1u) {
    if (below_range_message != NULL) {
      HAL_ASSERT(false, below_range_message);
    }
    return 1u;
  }
  if (bits > 16u) {
    if (above_range_message != NULL) {
      HAL_ASSERT(false, above_range_message);
    }
    return 16u;
  }
  return bits;
}
