/*
 * DHT transaction timing and byte decoding are based on the Bonezegei_DHT11
 * and Bonezegei_DHT22 libraries by Bonezegei (Jofel Batutay), dated November
 * 2023. This implementation keeps the proven start pulse, response sampling,
 * 30 us bit discriminator, checksum and public-value conversion flow while
 * using JaszczurHAL GPIO, timing and synchronization primitives.
 */

#include "hal/hal_target.h"
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "hal/hal_config.h"
#ifdef HAL_ENABLE_DHT

#include "hal/hal_dht.h"

#include "hal/hal_gpio.h"
#include "hal/hal_sync.h"
#include "hal/hal_system.h"
#include "hal/impl/shared/hal_mutex_once.h"

#include <stddef.h>
#include <string.h>

#define DHT_IDLE_SETTLE_MS 250u
#define DHT_START_LOW_MS 18u
#define DHT_START_HIGH_US 40u
#define DHT_RESPONSE_LOW_US 80u
#define DHT_RESPONSE_HIGH_US 80u
#define DHT_BIT_SAMPLE_US 30u
#define DHT_EDGE_TIMEOUT_US 120u
#define DHT_DATA_BYTES 5u
#define DHT_SCRATCH_BYTES 6u

struct hal_dht_impl_s {
  bool in_use;
  bool initialized;
  uint8_t pin;
  hal_dht_sensor_t sensor;
  uint8_t data[DHT_SCRATCH_BYTES];
  float humidity;
  float temperature_c;
  float temperature_f;
  hal_mutex_t mutex;
};

static hal_dht_impl_t s_pool[HAL_DHT_MAX_INSTANCES];
static hal_mutex_t s_pool_mutex = NULL;

static bool sensor_valid(hal_dht_sensor_t sensor) {
  return sensor == HAL_DHT_SENSOR_DHT11 || sensor == HAL_DHT_SENSOR_DHT22;
}

static bool handle_valid(hal_dht_t h) {
  return h != NULL && h->in_use && h->initialized && h->mutex != NULL;
}

static bool wait_while_level(uint8_t pin, bool level, uint32_t timeout_us) {
  const uint32_t start = hal_micros();
  while (hal_gpio_read(pin) == level) {
    if ((uint32_t)(hal_micros() - start) >= timeout_us) {
      return false;
    }
  }
  return true;
}

static void decode_frame_locked(hal_dht_t h,
                                const uint8_t frame[DHT_DATA_BYTES]) {
  if (h->sensor == HAL_DHT_SENSOR_DHT22) {
    const uint16_t humidity_raw =
        (uint16_t)(((uint16_t)frame[0] << 8u) | frame[1]);
    const uint16_t temp_raw = (uint16_t)(((uint16_t)frame[2] << 8u) | frame[3]);
    const uint16_t temp_magnitude = (uint16_t)(temp_raw & 0x7fffu);

    h->humidity = (float)humidity_raw / 10.0f;
    h->temperature_c = (float)temp_magnitude / 10.0f;
    if ((temp_raw & 0x8000u) != 0u) {
      h->temperature_c = -h->temperature_c;
    }
  } else {
    const float frac = (float)frame[3] / 10.0f;
    h->temperature_c = (float)frame[2] + frac;
    h->humidity = (float)frame[0];
  }

  h->temperature_f = (h->temperature_c * 9.0f / 5.0f) + 32.0f;
}

static bool read_frame_locked(hal_dht_t h) {
  uint8_t frame[DHT_SCRATCH_BYTES] = {};
  bool ok = false;

  hal_gpio_set_mode(h->pin, HAL_GPIO_INPUT_PULLUP);
  hal_gpio_write(h->pin, true);
  hal_delay_ms(DHT_IDLE_SETTLE_MS);

  hal_gpio_set_mode(h->pin, HAL_GPIO_OUTPUT_LOW);
  hal_delay_ms(DHT_START_LOW_MS);

  hal_critical_section_enter();

  hal_gpio_write(h->pin, true);
  hal_delay_us(DHT_START_HIGH_US);
  hal_gpio_set_mode(h->pin, HAL_GPIO_INPUT_PULLUP);

  if (hal_gpio_read(h->pin) == false) {
    hal_delay_us(DHT_RESPONSE_LOW_US);
    if (hal_gpio_read(h->pin) == true) {
      hal_delay_us(DHT_RESPONSE_HIGH_US);

      ok = true;
      for (uint8_t b = 0u; b < DHT_DATA_BYTES && ok; ++b) {
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
          ok = wait_while_level(h->pin, false, DHT_EDGE_TIMEOUT_US);
          if (!ok) {
            break;
          }

          hal_delay_us(DHT_BIT_SAMPLE_US);
          if (hal_gpio_read(h->pin) == true) {
            frame[b] |= (uint8_t)(1u << (7u - bit));
          }

          ok = wait_while_level(h->pin, true, DHT_EDGE_TIMEOUT_US);
        }
      }
    }
  }

  hal_critical_section_exit();

  if (!ok) {
    return false;
  }

  frame[5] = (uint8_t)((frame[0] + frame[1] + frame[2] + frame[3]) & 0xffu);
  if (frame[4] != frame[5]) {
    return false;
  }

  memcpy(h->data, frame, sizeof(h->data));
  decode_frame_locked(h, frame);
  return true;
}

hal_dht_config_t hal_dht_default_config(uint8_t data_pin) {
  hal_dht_config_t cfg = {
      data_pin,
      HAL_DHT_SENSOR_DHT11,
  };
  return cfg;
}

hal_dht_t hal_dht_init(const hal_dht_config_t *cfg) {
  if (cfg == NULL || !sensor_valid(cfg->sensor)) {
    return NULL;
  }

  (void)jh_hal_mutex_create_once(&s_pool_mutex);
  hal_mutex_lock(s_pool_mutex);

  hal_dht_t h = NULL;
  for (size_t i = 0u; i < HAL_DHT_MAX_INSTANCES; ++i) {
    if (!s_pool[i].in_use) {
      h = &s_pool[i];
      memset(h, 0, sizeof(*h));
      h->in_use = true;
      break;
    }
  }

  hal_mutex_unlock(s_pool_mutex);

  if (h == NULL) {
    return NULL;
  }

  h->mutex = hal_mutex_create();
  if (h->mutex == NULL) {
    hal_dht_deinit(h);
    return NULL;
  }

  h->pin = cfg->data_pin;
  h->sensor = cfg->sensor;
  h->humidity = 0.0f;
  h->temperature_c = 0.0f;
  h->temperature_f = 32.0f;

  hal_mutex_lock(h->mutex);
  hal_gpio_set_mode(h->pin, HAL_GPIO_INPUT_PULLUP);
  hal_gpio_write(h->pin, true);
  h->initialized = true;
  hal_mutex_unlock(h->mutex);

  return h;
}

void hal_dht_deinit(hal_dht_t h) {
  if (h == NULL) {
    return;
  }

  hal_mutex_t mutex = h->mutex;
  if (mutex != NULL) {
    hal_mutex_lock(mutex);
    h->initialized = false;
    hal_mutex_unlock(mutex);
    hal_mutex_destroy(mutex);
  }

  (void)jh_hal_mutex_create_once(&s_pool_mutex);
  hal_mutex_lock(s_pool_mutex);
  memset(h, 0, sizeof(*h));
  hal_mutex_unlock(s_pool_mutex);
}

bool hal_dht_read(hal_dht_t h) {
  if (!handle_valid(h)) {
    return false;
  }

  hal_mutex_lock(h->mutex);
  const bool ok = read_frame_locked(h);
  hal_mutex_unlock(h->mutex);
  return ok;
}

float hal_dht_get_temperature_c(hal_dht_t h) {
  if (!handle_valid(h)) {
    return 0.0f;
  }

  hal_mutex_lock(h->mutex);
  const float value = h->temperature_c;
  hal_mutex_unlock(h->mutex);
  return value;
}

float hal_dht_get_temperature_f(hal_dht_t h) {
  if (!handle_valid(h)) {
    return 32.0f;
  }

  hal_mutex_lock(h->mutex);
  const float value = h->temperature_f;
  hal_mutex_unlock(h->mutex);
  return value;
}

float hal_dht_get_humidity(hal_dht_t h) {
  if (!handle_valid(h)) {
    return 0.0f;
  }

  hal_mutex_lock(h->mutex);
  const float value = h->humidity;
  hal_mutex_unlock(h->mutex);
  return value;
}

bool hal_dht_get_sample(hal_dht_t h, hal_dht_sample_t *out) {
  if (!handle_valid(h) || out == NULL) {
    return false;
  }

  hal_mutex_lock(h->mutex);
  out->temperature_c = h->temperature_c;
  out->temperature_f = h->temperature_f;
  out->humidity = h->humidity;
  hal_mutex_unlock(h->mutex);
  return true;
}

#endif /* HAL_ENABLE_DHT */
#endif /* supported target */
