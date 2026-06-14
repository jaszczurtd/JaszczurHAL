#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_BH1750

/**
 * @file hal_bh1750.h
 * @brief BH1750 ambient light sensor driver over HAL I2C.
 *
 * The caller owns I2C bus setup and must call hal_i2c_init() or
 * hal_i2c_init_bus() before initialising the sensor.
 */

#include "hal_sync.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief BH1750 address when ADDR is tied low. */
#define HAL_BH1750_I2C_ADDR_LOW 0x23u

/** @brief BH1750 address when ADDR is tied high. */
#define HAL_BH1750_I2C_ADDR_HIGH 0x5Cu

/** @brief Default address preserved from the source driver constructor. */
#define HAL_BH1750_I2C_ADDR_DEFAULT HAL_BH1750_I2C_ADDR_HIGH

typedef struct {
  uint8_t i2c_bus;  /**< I2C controller index (0 = default, 1 = second). */
  uint8_t i2c_addr; /**< 7-bit BH1750 I2C address. */
} hal_bh1750_config_t;

typedef struct {
  hal_bh1750_config_t cfg;
  bool initialized;
  hal_mutex_t mutex;
} hal_bh1750_t;

/** @brief Return default config: bus 0, address 0x5C. */
hal_bh1750_config_t hal_bh1750_default_config(void);

/**
 * @brief Initialise BH1750 in continuous high-resolution mode.
 *
 * Sends the same command sequence as the source driver: one byte 0x10
 * (continuous H-resolution mode), then waits 180 ms for the first measurement.
 *
 * @param dev Destination driver state.
 * @param cfg Optional config; NULL uses hal_bh1750_default_config().
 * @return true when the sensor ACKs the mode command.
 */
bool hal_bh1750_init(hal_bh1750_t *dev, const hal_bh1750_config_t *cfg);

/** @brief Release internal resources associated with the driver state. */
void hal_bh1750_deinit(hal_bh1750_t *dev);

/**
 * @brief Read current light level in lux.
 *
 * Mirrors the source driver behavior: requests exactly two bytes and returns
 * -1.0f when the read does not provide a complete sample. A valid raw sample is
 * decoded as big-endian uint16_t divided by 1.2.
 */
float hal_bh1750_light(hal_bh1750_t *dev);

#endif /* HAL_ENABLE_BH1750 */
#ifdef __cplusplus
}
#endif
