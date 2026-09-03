#include "hal/core/hal_math.h"

#include <limits.h>
#include <math.h>

void hal_math_split_decimal_tenths(float value, int *whole, int *tenths) {
  const int integer = (int)value;
  if (integer <= -128) {
    return;
  }
  if (whole != nullptr) {
    *whole = integer;
  }
  if (tenths != nullptr) {
    const int fraction = (int)((value - (float)integer) * 10.0f);
    *tenths = fraction >= 0 ? fraction : 0;
  }
}

float hal_math_join_decimal_tenths(int whole, int tenths) {
  return (float)whole + ((float)tenths / 10.0f);
}

int hal_math_percent_to_value(float percent, int maximum) {
  return (int)((percent / 100.0f) * (float)maximum);
}

int hal_math_percent_from_value(int value, int maximum) {
  return maximum == 0 ? 0 : (value * 100) / maximum;
}

float hal_math_low_pass(float alpha, float input, float previous_output) {
  return alpha * input + (1.0f - alpha) * previous_output;
}

float hal_math_blend(float current_value, float new_value, float alpha) {
  return hal_math_low_pass(alpha, new_value, current_value);
}

hal_status_t hal_math_rolling_average_f32_ex(size_t *index, size_t *count,
                                             float value, float *table,
                                             size_t table_count,
                                             float *out_average) {
  if (index == nullptr || count == nullptr || table == nullptr ||
      out_average == nullptr || table_count == 0u || *index >= table_count ||
      *count > table_count) {
    return HAL_EINVAL;
  }

  table[*index] = value;
  *index = (*index + 1u) % table_count;
  if (*count < table_count) {
    ++(*count);
  }

  float sum = 0.0f;
  for (size_t i = 0u; i < *count; ++i) {
    sum += table[i];
  }
  *out_average = sum / (float)*count;
  return HAL_OK;
}

hal_status_t hal_math_average_i32_ex(const int *values, size_t count,
                                     int *out_average) {
  if (values == nullptr || out_average == nullptr || count == 0u) {
    return HAL_EINVAL;
  }

  int64_t sum = 0;
  for (size_t i = 0u; i < count; ++i) {
    sum += values[i];
  }
  const int64_t average = sum / (int64_t)count;
  if (average < INT_MIN || average > INT_MAX) {
    return HAL_EOVERFLOW;
  }
  *out_average = (int)average;
  return HAL_OK;
}

hal_status_t hal_math_min_i32_ex(const int *values, size_t count,
                                 int *out_minimum) {
  if (values == nullptr || out_minimum == nullptr || count == 0u) {
    return HAL_EINVAL;
  }

  int minimum = values[0];
  for (size_t i = 1u; i < count; ++i) {
    if (values[i] < minimum) {
      minimum = values[i];
    }
  }
  *out_minimum = minimum;
  return HAL_OK;
}

hal_status_t hal_math_midpoint_min_max_i32_ex(const int *values, size_t count,
                                              int *out_midpoint) {
  if (values == nullptr || out_midpoint == nullptr || count == 0u) {
    return HAL_EINVAL;
  }

  int minimum = values[0];
  int maximum = values[0];
  for (size_t i = 1u; i < count; ++i) {
    if (values[i] < minimum) {
      minimum = values[i];
    } else if (values[i] > maximum) {
      maximum = values[i];
    }
  }
  *out_midpoint = (int)(((int64_t)minimum + (int64_t)maximum) / 2);
  return HAL_OK;
}

float hal_math_map_f32(float value, float input_min, float input_max,
                       float output_min, float output_max) {
  if (input_max == input_min) {
    return output_min;
  }
  return (value - input_min) * (output_max - output_min) /
             (input_max - input_min) +
         output_min;
}

float hal_math_round_tenth(float value) {
  return roundf(value * 10.0f) / 10.0f;
}

float hal_math_round_precision(float value, int precision) {
  float multiplier = 1.0f;
  for (int i = 0; i < precision; ++i) {
    multiplier *= 10.0f;
  }
  return roundf(value * multiplier) / multiplier;
}

float hal_math_rolling_average_default_f32(int *index, int *count, float value,
                                           float *table) {
  if (index == nullptr || count == nullptr || table == nullptr || *index < 0 ||
      *count < 0) {
    return 0.0f;
  }
  size_t current_index = (size_t)*index;
  size_t current_count = (size_t)*count;
  float average = 0.0f;
  if (hal_math_rolling_average_f32_ex(
          &current_index, &current_count, value, table,
          HAL_MATH_ROLLING_AVERAGE_DEFAULT_SIZE, &average) != HAL_OK) {
    return 0.0f;
  }
  *index = (int)current_index;
  *count = (int)current_count;
  return average;
}

int hal_math_nonnegative_average_i32(const int *values, int count) {
  int average = 0;
  if (count <= 0 ||
      hal_math_average_i32_ex(values, (size_t)count, &average) != HAL_OK) {
    return 0;
  }
  return average < 0 ? 0 : average;
}

int hal_math_min_i32(const int *values, int count) {
  int minimum = -1;
  return count > 0 &&
                 hal_math_min_i32_ex(values, (size_t)count, &minimum) == HAL_OK
             ? minimum
             : -1;
}

int hal_math_midpoint_min_max_i32(const int *values, int count) {
  int midpoint = -1;
  return count > 0 && hal_math_midpoint_min_max_i32_ex(values, (size_t)count,
                                                       &midpoint) == HAL_OK
             ? midpoint
             : -1;
}
