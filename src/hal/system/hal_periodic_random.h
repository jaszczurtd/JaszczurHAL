#pragma once

/** @file Stateful pseudo-random values refreshed at a fixed interval. */

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** State of an interval-refreshed integer pseudo-random value. */
typedef struct {
  uint32_t last_update_ms; /**< Timestamp of the last refresh. */
  uint32_t state;          /**< PRNG state. */
  int cached_value;        /**< Value returned between refreshes. */
  bool initialized;        /**< Whether the state was initialized. */
} hal_periodic_random_int_t;

/** State of an interval-refreshed floating-point pseudo-random value. */
typedef struct {
  uint32_t last_update_ms; /**< Timestamp of the last refresh. */
  uint32_t state;          /**< PRNG state. */
  float cached_value;      /**< Value returned between refreshes. */
  bool initialized;        /**< Whether the state was initialized. */
} hal_periodic_random_float_t;

/**
 * @brief Initialize integer periodic-random state.
 * @param random State to initialize; NULL is ignored.
 * @param seed Initial PRNG seed. Zero selects a fixed non-zero state on use.
 */
void hal_periodic_random_int_init(hal_periodic_random_int_t *random,
                                  uint32_t seed);

/**
 * @brief Initialize floating-point periodic-random state.
 * @param random State to initialize; NULL is ignored.
 * @param seed Initial PRNG seed. Zero selects a fixed non-zero state on use.
 */
void hal_periodic_random_float_init(hal_periodic_random_float_t *random,
                                    uint32_t seed);

/**
 * @brief Return an integer value refreshed after a wrap-safe interval.
 *
 * An uninitialized state is seeded from the runtime clock. Before the first
 * elapsed interval, the initialized sentinel value is returned.
 *
 * @param random Mutable periodic-random state.
 * @param now_ms Current 32-bit monotonic timestamp.
 * @param interval_ms Minimum interval between refreshes.
 * @param maximum Exclusive upper bound; it must be positive.
 * @param out_value Receives the cached or newly generated value.
 * @return HAL_OK, or HAL_EINVAL for invalid input.
 */
hal_status_t hal_periodic_random_int_get_ex(hal_periodic_random_int_t *random,
                                            uint32_t now_ms,
                                            uint32_t interval_ms, int maximum,
                                            int *out_value);

/**
 * @brief Return a floating-point value refreshed after a wrap-safe interval.
 * @param random Mutable periodic-random state.
 * @param now_ms Current 32-bit monotonic timestamp.
 * @param interval_ms Minimum interval between refreshes.
 * @param maximum Positive upper bound of generated values.
 * @param out_value Receives the cached or newly generated value.
 * @return HAL_OK, or HAL_EINVAL for invalid input.
 */
hal_status_t
hal_periodic_random_float_get_ex(hal_periodic_random_float_t *random,
                                 uint32_t now_ms, uint32_t interval_ms,
                                 float maximum, float *out_value);

/**
 * @brief Use shared state for an interval-refreshed integer value.
 * @param interval_ms Minimum interval between refreshes.
 * @param maximum Exclusive positive upper bound.
 * @return Cached/generated value, or zero for an invalid bound.
 */
int hal_periodic_random_int_get(uint32_t interval_ms, int maximum);

/**
 * @brief Use shared state for an interval-refreshed floating-point value.
 * @param interval_ms Minimum interval between refreshes.
 * @param maximum Positive upper bound.
 * @return Cached/generated value, or the initial sentinel before refresh.
 */
float hal_periodic_random_float_get(uint32_t interval_ms, float maximum);

#ifdef __cplusplus
}
#endif
