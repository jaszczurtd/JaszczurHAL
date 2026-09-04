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

/** Number of samples used by hal_math_rolling_average_default_f32(). */
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
 * @param v Value to clamp.
 * @param lo Inclusive lower bound.
 * @param hi Inclusive upper bound.
 * @return @p v constrained to the inclusive range.
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
 * @param x Value to map.
 * @param in_min Input range start.
 * @param in_max Input range end.
 * @param out_min Output range start.
 * @param out_max Output range end.
 * @return Linearly mapped value, or @p out_min for a zero-width input range.
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
 *
 * @param v Value to round.
 * @param n Requested decimal places.
 * @return Rounded value.
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

/**
 * @brief Split a value into its integer part and one decimal digit.
 *
 * The integer part is truncated toward zero. A negative fractional digit is
 * reported as zero. Values whose integer part is at most -128 leave both
 * outputs unchanged, preserving the established helper behaviour.
 *
 * @param value Value to split.
 * @param whole Optional output for the integer part.
 * @param tenths Optional output for the first decimal digit.
 */
void hal_math_split_decimal_tenths(float value, int *whole, int *tenths);

/**
 * @brief Join an integer part and a decimal digit.
 * @param whole Integer part.
 * @param tenths Fractional value expressed in tenths; it is not clamped.
 * @return `whole + tenths / 10`.
 */
float hal_math_join_decimal_tenths(int whole, int tenths);

/**
 * @brief Convert a percentage to an integer value.
 * @param percent Percentage to scale; it is not clamped to 0..100.
 * @param maximum Value corresponding to 100 percent.
 * @return Truncated result of `percent * maximum / 100`.
 */
int hal_math_percent_to_value(float percent, int maximum);

/**
 * @brief Express an integer value as a percentage of a maximum.
 * @param value Value to convert.
 * @param maximum Value corresponding to 100 percent.
 * @return Integer percentage, or 0 when @p maximum is zero.
 */
int hal_math_percent_from_value(int value, int maximum);

/**
 * @brief Apply a first-order low-pass filter.
 * @param alpha Weight assigned to @p input; it is not clamped.
 * @param input New input sample.
 * @param previous_output Previous filtered value.
 * @return `alpha * input + (1 - alpha) * previous_output`.
 */
float hal_math_low_pass(float alpha, float input, float previous_output);

/**
 * @brief Blend a current value with a new value.
 * @param current_value Existing value.
 * @param new_value New value.
 * @param alpha Weight assigned to @p new_value; it is not clamped.
 * @return The same EMA result as hal_math_low_pass().
 */
float hal_math_blend(float current_value, float new_value, float alpha);

/**
 * @brief Insert a sample into a circular table and calculate its average.
 * @param index In/out next insertion index in range `0..table_count-1`.
 * @param count In/out number of initialized entries in the table.
 * @param value Sample to insert.
 * @param table Circular sample table.
 * @param table_count Number of entries in @p table.
 * @param out_average Receives the average of initialized entries.
 * @return HAL_OK, or HAL_EINVAL for invalid pointers or state.
 */
hal_status_t hal_math_rolling_average_f32_ex(size_t *index, size_t *count,
                                             float value, float *table,
                                             size_t table_count,
                                             float *out_average);

/**
 * @brief Calculate the arithmetic mean of an integer array.
 * @param values Input values.
 * @param count Number of input values; it must be non-zero.
 * @param out_average Receives the mean truncated toward zero.
 * @return HAL_OK, HAL_EINVAL for invalid input, or HAL_EOVERFLOW when the
 * result cannot be represented as `int`.
 */
hal_status_t hal_math_average_i32_ex(const int *values, size_t count,
                                     int *out_average);

/**
 * @brief Find the minimum value in an integer array.
 * @param values Input values.
 * @param count Number of input values; it must be non-zero.
 * @param out_minimum Receives the minimum value.
 * @return HAL_OK, or HAL_EINVAL for invalid input.
 */
hal_status_t hal_math_min_i32_ex(const int *values, size_t count,
                                 int *out_minimum);

/**
 * @brief Calculate the midpoint between the minimum and maximum array values.
 * @param values Input values.
 * @param count Number of input values; it must be non-zero.
 * @param out_midpoint Receives `(minimum + maximum) / 2` without `int` sum
 * overflow.
 * @return HAL_OK, or HAL_EINVAL for invalid input.
 */
hal_status_t hal_math_midpoint_min_max_i32_ex(const int *values, size_t count,
                                              int *out_midpoint);

/**
 * @brief Map a floating-point value linearly between two ranges.
 * @param value Input value; values outside the input range are extrapolated.
 * @param input_min Input range start.
 * @param input_max Input range end.
 * @param output_min Output range start.
 * @param output_max Output range end.
 * @return Mapped value, or @p output_min for a zero-width input range.
 */
float hal_math_map_f32(float value, float input_min, float input_max,
                       float output_min, float output_max);

/**
 * @brief Round a value to one decimal place.
 * @param value Value to round.
 * @return Rounded value using the C `roundf()` halfway rule.
 */
float hal_math_round_tenth(float value);

/**
 * @brief Round a value to the requested decimal precision.
 * @param value Value to round.
 * @param precision Number of decimal places; negative values act as zero.
 * @return Rounded value using the C `roundf()` halfway rule.
 */
float hal_math_round_precision(float value, int precision);

/**
 * @brief Update a rolling average with the configured default table size.
 * @param index In/out next insertion index.
 * @param count In/out number of initialized entries.
 * @param value Sample to insert.
 * @param table Table with HAL_MATH_ROLLING_AVERAGE_DEFAULT_SIZE entries.
 * @return Current average, or 0.0f for invalid state.
 */
float hal_math_rolling_average_default_f32(int *index, int *count, float value,
                                           float *table);

/**
 * @brief Calculate an integer average and clamp negative results to zero.
 * @param values Input values.
 * @param count Number of values.
 * @return Non-negative average, or zero for invalid input.
 */
int hal_math_nonnegative_average_i32(const int *values, int count);

/**
 * @brief Find the minimum integer in an array.
 * @param values Input values.
 * @param count Number of values.
 * @return Minimum value, or -1 for invalid input.
 */
int hal_math_min_i32(const int *values, int count);

/**
 * @brief Calculate the midpoint between the minimum and maximum array values.
 * @param values Input values.
 * @param count Number of values.
 * @return Midpoint, or -1 for invalid input.
 */
int hal_math_midpoint_min_max_i32(const int *values, int count);

/**
 * @brief Copy the object representation of a float into a 32-bit integer.
 * @param value Floating-point value.
 * @return Bit-preserving 32-bit representation.
 */
static inline uint32_t hal_math_float_to_u32(float value) {
  uint32_t result;
  memcpy(&result, &value, sizeof(result));
  return result;
}

/**
 * @brief Copy a 32-bit object representation into a float.
 * @param value Bit representation returned by hal_math_float_to_u32().
 * @return Bit-preserving floating-point value.
 */
static inline float hal_math_u32_to_float(uint32_t value) {
  float result;
  memcpy(&result, &value, sizeof(result));
  return result;
}

#ifdef __cplusplus
}
#endif
