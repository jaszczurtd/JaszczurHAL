#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifndef HAL_DISABLE_DS18B20

/**
 * @file hal_ds18b20.h
 * @brief Hardware abstraction for DS18B20 digital temperature sensors.
 *
 * The module exposes a non-blocking workflow:
 *   1) call hal_ds18b20_request() to start conversion,
 *   2) call hal_ds18b20_poll() periodically (for example in loop()),
 *   3) call hal_ds18b20_take_latest() to fetch the newest cached sample.
 *
 * One handle controls one logical sensor.
 * - Single-device bus: leave use_rom=false (Skip ROM).
 * - Multi-device bus: set use_rom=true and provide rom_code (Match ROM).
 */

#include <stdbool.h>
#include <stdint.h>

/** @brief Maximum number of simultaneous DS18B20 handles. */
#ifndef HAL_DS18B20_MAX_INSTANCES
#define HAL_DS18B20_MAX_INSTANCES 4
#endif

/** @brief Opaque DS18B20 handle. */
typedef struct hal_ds18b20_impl_s  hal_ds18b20_impl_t;
typedef       hal_ds18b20_impl_t  *hal_ds18b20_t;

/**
 * @brief Resolution hint used for conversion-time scheduling.
 *
 * The backend measures/report resolution from the scratchpad when possible.
 * The hint is used as a safe initial fallback before the first successful read.
 */
typedef enum {
    HAL_DS18B20_RES_9_BIT  = 9,
    HAL_DS18B20_RES_10_BIT = 10,
    HAL_DS18B20_RES_11_BIT = 11,
    HAL_DS18B20_RES_12_BIT = 12,
} hal_ds18b20_resolution_t;

/**
 * @brief DS18B20 initialisation descriptor.
 */
typedef struct {
    uint8_t data_pin;                       /**< 1-Wire bus pin.              */
    bool    use_rom;                        /**< true = Match ROM, false = Skip ROM. */
    uint8_t rom_code[8];                    /**< 64-bit ROM code when use_rom=true.  */
    hal_ds18b20_resolution_t resolution_hint; /**< Initial conversion-time hint. */
} hal_ds18b20_config_t;

/**
 * @brief Create a DS18B20 handle.
 *
 * The call probes bus presence (reset/presence pulse). If no sensor responds,
 * NULL is returned.
 */
hal_ds18b20_t hal_ds18b20_init(const hal_ds18b20_config_t *cfg);

/** @brief Release handle resources and free the pool slot. */
void hal_ds18b20_deinit(hal_ds18b20_t h);

/**
 * @brief Start a temperature conversion (non-blocking).
 *
 * @return true when conversion was started; false on invalid handle,
 *         missing sensor, or when conversion is already in progress.
 */
bool hal_ds18b20_request(hal_ds18b20_t h);

/**
 * @brief Progress the internal state machine.
 *
 * Call this periodically from the main loop (or a periodic soft timer).
 * The function performs at most one short state-machine step per call.
 */
void hal_ds18b20_poll(hal_ds18b20_t h);

/** @brief Return true when conversion is still in progress. */
bool hal_ds18b20_is_busy(hal_ds18b20_t h);

/**
 * @brief Read the cached sample.
 *
 * @param h      Valid handle.
 * @param temp_c Output temperature in Celsius.
 * @param fresh  Optional output flag:
 *               true when the sample became available since previous read.
 *
 * @return true when at least one valid sample is cached.
 *
 * @note Calling this function clears the internal "fresh" flag.
 */
bool hal_ds18b20_take_latest(hal_ds18b20_t h, float *temp_c, bool *fresh);


#endif /* HAL_DISABLE_DS18B20 */
#ifdef __cplusplus
}
#endif
