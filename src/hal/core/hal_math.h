#pragma once

/**
 * @file hal_math.h
 * @brief Platform-independent numeric and signal helpers.
 *
 * The range helpers remain macros for type independence. Functions below own
 * reusable floating-point, percentage, filtering and array calculations.
 */

#include "hal/core/hal_status.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAL_MATH_ROLLING_AVERAGE_DEFAULT_SIZE
#ifdef HAL_TOOLS_TEMPERATURE_TABLES_SIZE
#define HAL_MATH_ROLLING_AVERAGE_DEFAULT_SIZE HAL_TOOLS_TEMPERATURE_TABLES_SIZE
#else
#define HAL_MATH_ROLLING_AVERAGE_DEFAULT_SIZE 5u
#endif
#endif

/**
 * @brief Clamp a value to the range [lo, hi] (type-independent).
 *
 * Generic replacement for Arduino constrain().
 * No hardware dependency - safe to include anywhere.
 *
 * @note Arguments may be evaluated more than once. Avoid side effects.
 */
#ifndef hal_constrain
#define hal_constrain(v, lo, hi)                                               \
  (((v) < (lo)) ? (lo) : (((v) > (hi)) ? (hi) : (v)))
#endif

/**
 * @brief Re-map a value from one range to another (type-independent).
 *
 * Generic replacement for Arduino map().
 * No hardware dependency - safe to include anywhere.
 *
 * When @p in_min equals @p in_max the macro returns @p out_min to avoid
 * division by zero (matches hal_math_map_f32()).
 *
 * @note Arguments may be evaluated more than once. Avoid side effects.
 */
#ifndef hal_map
#define hal_map(x, in_min, in_max, out_min, out_max)                           \
  ((in_max) == (in_min)                                                        \
       ? (out_min)                                                             \
       : (((x) - (in_min)) * ((out_max) - (out_min)) / ((in_max) - (in_min)) + \
          (out_min)))
#endif

/**
 * @brief Round a floating-point value to @p n decimal places.
 *
 * Rules:
 * - n < 0 is treated as 0.
 * - n > 6 is clamped to 6 to avoid excessive scaling.
 * - Half values are rounded away from zero.
 */
static inline float hal_math_round_to_n(float v, int n) {
  if (n < 0) {
    n = 0;
  } else if (n > 6) {
    n = 6;
  }

  float scale = 1.0f;
  for (int i = 0; i < n; ++i) {
    scale *= 10.0f;
  }

  float scaled = v * scale;
  long rounded =
      (scaled >= 0.0f) ? (long)(scaled + 0.5f) : (long)(scaled - 0.5f);

  return ((float)rounded) / scale;
}

void hal_math_split_decimal_tenths(float value, int *whole, int *tenths);
float hal_math_join_decimal_tenths(int whole, int tenths);
int hal_math_percent_to_value(float percent, int maximum);
int hal_math_percent_from_value(int value, int maximum);
float hal_math_low_pass(float alpha, float input, float previous_output);
float hal_math_blend(float current_value, float new_value, float alpha);
hal_status_t hal_math_rolling_average_f32_ex(size_t *index, size_t *count,
                                             float value, float *table,
                                             size_t table_count,
                                             float *out_average);
hal_status_t hal_math_average_i32_ex(const int *values, size_t count,
                                     int *out_average);
hal_status_t hal_math_min_i32_ex(const int *values, size_t count,
                                 int *out_minimum);
hal_status_t hal_math_midpoint_min_max_i32_ex(const int *values, size_t count,
                                              int *out_midpoint);
float hal_math_map_f32(float value, float input_min, float input_max,
                       float output_min, float output_max);
float hal_math_round_tenth(float value);
float hal_math_round_precision(float value, int precision);

/**
 * @brief Update a rolling average with the configured default table size.
 * @return Current average, or 0.0f for invalid state.
 */
float hal_math_rolling_average_default_f32(int *index, int *count, float value,
                                           float *table);

/** @brief Return an integer average clamped to zero, or zero on error. */
int hal_math_nonnegative_average_i32(const int *values, int count);

/** @brief Return the minimum value, or -1 for invalid input. */
int hal_math_min_i32(const int *values, int count);

/** @brief Return the midpoint between minimum and maximum, or -1 on error. */
int hal_math_midpoint_min_max_i32(const int *values, int count);

static inline uint32_t hal_math_float_to_u32(float value) {
  uint32_t result;
  memcpy(&result, &value, sizeof(result));
  return result;
}

static inline float hal_math_u32_to_float(uint32_t value) {
  float result;
  memcpy(&result, &value, sizeof(result));
  return result;
}

#ifdef __cplusplus
}
#endif
