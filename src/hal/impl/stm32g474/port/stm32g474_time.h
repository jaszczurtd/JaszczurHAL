#pragma once

/**
 * @file stm32g474_time.h
 * @brief Small helpers shared by the STM32G474 time source and host tests.
 */

#include <stdint.h>

static inline uint64_t jh_stm32g474_compose_micros(uint32_t millis_high,
                                                   uint32_t millis_low,
                                                   uint32_t micros_in_millis) {
  const uint64_t millis = ((uint64_t)millis_high << 32u) | millis_low;
  return (millis * 1000u) + micros_in_millis;
}

static inline void jh_stm32g474_increment_millis(uint32_t *millis_high,
                                                 uint32_t *millis_low) {
  *millis_low += 1u;
  if (*millis_low == 0u) {
    *millis_high += 1u;
  }
}
