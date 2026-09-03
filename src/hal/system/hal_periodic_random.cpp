#include "hal/system/hal_periodic_random.h"

#include "hal/system/hal_system.h"

namespace {

uint32_t next_value(uint32_t *state) {
  uint32_t value = *state;
  if (value == 0u) {
    value = UINT32_C(0x6D2B79F5);
  }
  value ^= value << 13u;
  value ^= value >> 17u;
  value ^= value << 5u;
  *state = value;
  return value;
}

uint32_t runtime_seed(void) {
  const uint32_t seed = hal_millis() ^ hal_micros();
  return seed == 0u ? UINT32_C(0x6D2B79F5) : seed;
}

} // namespace

void hal_periodic_random_int_init(hal_periodic_random_int_t *random,
                                  uint32_t seed) {
  if (random == nullptr) {
    return;
  }
  random->last_update_ms = 0u;
  random->state = seed;
  random->cached_value = -1;
  random->initialized = true;
}

void hal_periodic_random_float_init(hal_periodic_random_float_t *random,
                                    uint32_t seed) {
  if (random == nullptr) {
    return;
  }
  random->last_update_ms = 0u;
  random->state = seed;
  random->cached_value = -1.0f;
  random->initialized = true;
}

hal_status_t hal_periodic_random_int_get_ex(hal_periodic_random_int_t *random,
                                            uint32_t now_ms,
                                            uint32_t interval_ms, int maximum,
                                            int *out_value) {
  if (random == nullptr || out_value == nullptr || maximum <= 0) {
    return HAL_EINVAL;
  }
  if (!random->initialized) {
    hal_periodic_random_int_init(random, runtime_seed());
  }
  if (hal_elapsed_u32(now_ms, random->last_update_ms, interval_ms)) {
    random->last_update_ms = now_ms;
    random->cached_value =
        (int)(next_value(&random->state) % (uint32_t)maximum);
  }
  *out_value = random->cached_value;
  return HAL_OK;
}

hal_status_t
hal_periodic_random_float_get_ex(hal_periodic_random_float_t *random,
                                 uint32_t now_ms, uint32_t interval_ms,
                                 float maximum, float *out_value) {
  if (random == nullptr || out_value == nullptr || maximum <= 0.0f) {
    return HAL_EINVAL;
  }
  if (!random->initialized) {
    hal_periodic_random_float_init(random, runtime_seed());
  }
  if (hal_elapsed_u32(now_ms, random->last_update_ms, interval_ms)) {
    random->last_update_ms = now_ms;
    random->cached_value =
        ((float)next_value(&random->state) / (float)UINT32_MAX) * maximum;
  }
  *out_value = random->cached_value;
  return HAL_OK;
}

int hal_periodic_random_int_get(uint32_t interval_ms, int maximum) {
  static hal_periodic_random_int_t random = {};
  if (maximum <= 0) {
    return 0;
  }
  int value = 0;
  (void)hal_periodic_random_int_get_ex(&random, hal_millis(), interval_ms,
                                       maximum, &value);
  return value;
}

float hal_periodic_random_float_get(uint32_t interval_ms, float maximum) {
  static hal_periodic_random_float_t random = {};
  float value = 0.0f;
  (void)hal_periodic_random_float_get_ex(&random, hal_millis(), interval_ms,
                                         maximum, &value);
  return value;
}
