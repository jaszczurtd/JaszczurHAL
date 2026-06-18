#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_TSC2007

/**
 * @file hal_tsc2007.h
 * @brief TSC2007 resistive touch controller driver over HAL I2C.
 *
 * The caller owns I2C bus setup and must call hal_i2c_init() or
 * hal_i2c_init_bus() before initialising the controller.
 *
 * Thread-safety: public driver calls serialize access with an instance mutex.
 * The mutex is created through the HAL create-once helper in the shared
 * implementation, so first use is safe when two tasks/cores race to touch the
 * same zero-initialized driver object. Do not call hal_tsc2007_deinit()
 * concurrently with other operations on the same instance.
 */

#include "hal_sync.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Default 7-bit I2C address used by TSC2007 boards. */
#define HAL_TSC2007_I2C_ADDR_DEFAULT 0x48u

/** @brief Raw coordinate value used by the source driver to reject samples. */
#define HAL_TSC2007_TOUCH_INVALID 4095u

/** @brief Maximum accepted difference between duplicate X/Y samples. */
#define HAL_TSC2007_STABILITY_THRESHOLD 100u

typedef enum {
  HAL_TSC2007_MEASURE_TEMP0 = 0,
  HAL_TSC2007_MEASURE_AUX = 2,
  HAL_TSC2007_MEASURE_TEMP1 = 4,
  HAL_TSC2007_ACTIVATE_X = 8,
  HAL_TSC2007_ACTIVATE_Y = 9,
  HAL_TSC2007_ACTIVATE_YPLUS_X = 10,
  HAL_TSC2007_SETUP_COMMAND = 11,
  HAL_TSC2007_MEASURE_X = 12,
  HAL_TSC2007_MEASURE_Y = 13,
  HAL_TSC2007_MEASURE_Z1 = 14,
  HAL_TSC2007_MEASURE_Z2 = 15,
} hal_tsc2007_function_t;

typedef enum {
  HAL_TSC2007_POWERDOWN_IRQON = 0,
  HAL_TSC2007_ADON_IRQOFF = 1,
  HAL_TSC2007_ADOFF_IRQON = 2,
} hal_tsc2007_power_t;

typedef enum {
  HAL_TSC2007_ADC_12BIT = 0,
  HAL_TSC2007_ADC_8BIT = 1,
} hal_tsc2007_resolution_t;

typedef struct {
  uint8_t i2c_bus;  /**< I2C controller index (0 = default, 1 = second). */
  uint8_t i2c_addr; /**< 7-bit TSC2007 I2C address. */
} hal_tsc2007_config_t;

typedef struct {
  int16_t x;
  int16_t y;
  int16_t z;
} hal_tsc2007_point_t;

typedef struct {
  hal_tsc2007_config_t cfg;
  bool initialized;
  hal_mutex_t mutex;
} hal_tsc2007_t;

/** @brief Return default config: bus 0, address 0x48. */
hal_tsc2007_config_t hal_tsc2007_default_config(void);

/**
 * @brief Probe and initialise a TSC2007 controller.
 *
 * Mirrors the source driver begin() flow: probe the I2C address, then issue
 * MEASURE_TEMP0 with POWERDOWN_IRQON and 12-bit ADC mode.
 *
 * @param dev Destination driver state. Must be zero-initialized before first
 *        use.
 * @param cfg Optional config; NULL uses hal_tsc2007_default_config().
 * @return true when the I2C address responds to the probe.
 */
bool hal_tsc2007_init(hal_tsc2007_t *dev, const hal_tsc2007_config_t *cfg);

/** @brief Release internal resources associated with the driver state. */
void hal_tsc2007_deinit(hal_tsc2007_t *dev);

/**
 * @brief Send one TSC2007 command and return the 12-bit decoded reply.
 *
 * The command byte layout, 500 us conversion wait, two-byte read and
 * failure-as-zero behavior match the source driver.
 */
uint16_t hal_tsc2007_command(hal_tsc2007_t *dev, hal_tsc2007_function_t func,
                             hal_tsc2007_power_t pwr,
                             hal_tsc2007_resolution_t res);

/**
 * @brief Read a debounced touch sample.
 *
 * Performs the same sequence as the source driver: Z1, Z2, X, Y, X, Y, then
 * MEASURE_TEMP0/POWERDOWN. X/Y are accepted only when duplicate samples differ
 * by no more than HAL_TSC2007_STABILITY_THRESHOLD and neither accepted
 * coordinate is HAL_TSC2007_TOUCH_INVALID.
 */
bool hal_tsc2007_read_touch(hal_tsc2007_t *dev, uint16_t *x, uint16_t *y,
                            uint16_t *z1, uint16_t *z2);

/**
 * @brief Read a point with z equal to Z1.
 *
 * Returns {0, 0, 0} when hal_tsc2007_read_touch() rejects the sample.
 */
hal_tsc2007_point_t hal_tsc2007_get_point(hal_tsc2007_t *dev);

#endif /* HAL_ENABLE_TSC2007 */
#ifdef __cplusplus
}
#endif
