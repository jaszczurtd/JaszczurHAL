#pragma once

/** @file Stateful pseudo-random values refreshed at a fixed interval. */

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t last_update_ms;
  uint32_t state;
  int cached_value;
  bool initialized;
} hal_periodic_random_int_t;

typedef struct {
  uint32_t last_update_ms;
  uint32_t state;
  float cached_value;
  bool initialized;
} hal_periodic_random_float_t;

void hal_periodic_random_int_init(hal_periodic_random_int_t *random,
                                  uint32_t seed);
void hal_periodic_random_float_init(hal_periodic_random_float_t *random,
                                    uint32_t seed);
hal_status_t hal_periodic_random_int_get_ex(hal_periodic_random_int_t *random,
                                            uint32_t now_ms,
                                            uint32_t interval_ms, int maximum,
                                            int *out_value);
hal_status_t
hal_periodic_random_float_get_ex(hal_periodic_random_float_t *random,
                                 uint32_t now_ms, uint32_t interval_ms,
                                 float maximum, float *out_value);

/** Shared-state convenience helper refreshed at the requested interval. */
int hal_periodic_random_int_get(uint32_t interval_ms, int maximum);

/** Shared-state floating-point convenience helper. */
float hal_periodic_random_float_get(uint32_t interval_ms, float maximum);

#ifdef __cplusplus
}
#endif
