#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_DHT

/**
 * @file hal_dht.h
 * @brief DHT11/DHT22 temperature and humidity sensor driver over HAL GPIO.
 */

#include <stdbool.h>
#include <stdint.h>

/** @brief Maximum number of simultaneous DHT handles. */
#ifndef HAL_DHT_MAX_INSTANCES
#define HAL_DHT_MAX_INSTANCES 4
#endif

/** @brief DHT sensor family. */
typedef enum {
  HAL_DHT_SENSOR_DHT11 = 0,
  HAL_DHT_SENSOR_DHT22 = 1,
} hal_dht_sensor_t;

/** @brief DHT initialisation descriptor. */
typedef struct {
  uint8_t data_pin;        /**< Single-wire data pin. */
  hal_dht_sensor_t sensor; /**< Sensor family. */
} hal_dht_config_t;

/** @brief Opaque DHT handle. */
typedef struct hal_dht_impl_s hal_dht_impl_t;
typedef hal_dht_impl_t *hal_dht_t;

/** @brief Last decoded DHT sample. */
typedef struct {
  float temperature_c; /**< Temperature in degrees Celsius. */
  float temperature_f; /**< Temperature in degrees Fahrenheit. */
  float humidity;      /**< Relative humidity in percent. */
} hal_dht_sample_t;

/** @brief Return default config for a DHT11 sensor on the given data pin. */
hal_dht_config_t hal_dht_default_config(uint8_t data_pin);

/** @brief Initialise a DHT handle and configure the data pin idle state. */
hal_dht_t hal_dht_init(const hal_dht_config_t *cfg);

/** @brief Release handle resources and free the pool slot. */
void hal_dht_deinit(hal_dht_t h);

/**
 * @brief Read and decode one DHT frame.
 *
 * The transaction mirrors the Bonezegei DHT timing flow: idle-high settle,
 * 18 ms host-low start pulse, 40 us host-high release, then 5 bytes sampled
 * with a 30 us bit discriminator.
 *
 * @return true when the frame checksum is valid and a sample was updated.
 */
bool hal_dht_read(hal_dht_t h);

/** @brief Return last temperature in Celsius. */
float hal_dht_get_temperature_c(hal_dht_t h);

/** @brief Return last temperature in Fahrenheit. */
float hal_dht_get_temperature_f(hal_dht_t h);

/** @brief Return last relative humidity in percent. */
float hal_dht_get_humidity(hal_dht_t h);

/** @brief Copy the last decoded sample. */
bool hal_dht_get_sample(hal_dht_t h, hal_dht_sample_t *out);

#endif /* HAL_ENABLE_DHT */
#ifdef __cplusplus
}
#endif
