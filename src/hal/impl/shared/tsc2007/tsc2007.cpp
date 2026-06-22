/*
 * TSC2007 transaction flow is based on the Adafruit TSC2007 library by
 * Limor Fried for Adafruit Industries. This implementation preserves the
 * command byte layout, 500 us conversion wait, duplicate X/Y stability check,
 * power-down command and Z1 pressure reporting while routing transport,
 * timing and synchronization through JaszczurHAL.
 */

#include "../../../hal_target.h"
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "../../../hal_config.h"
#if defined(HAL_ENABLE_TSC2007) && defined(HAL_ENABLE_I2C)

#include "../../../hal_tsc2007.h"

#include "../../../hal_i2c.h"
#include "../../../hal_system.h"
#include "../hal_mutex_once.h"

#include <stddef.h>

#define TSC2007_CONVERSION_DELAY_US 500u

static bool tsc2007_ensure_mutex(hal_tsc2007_t *dev) {
  return (dev != NULL) && (jh_hal_mutex_create_once(&dev->mutex) != NULL);
}

static bool tsc2007_valid(hal_tsc2007_t *dev) {
  return (dev != NULL) && (dev->mutex != NULL) && dev->initialized;
}

static uint8_t tsc2007_command_byte(hal_tsc2007_function_t func,
                                    hal_tsc2007_power_t pwr,
                                    hal_tsc2007_resolution_t res) {
  return (uint8_t)(((uint8_t)func << 4u) | ((uint8_t)pwr << 2u) |
                   ((uint8_t)res << 1u));
}

static uint16_t tsc2007_command_unlocked(hal_tsc2007_t *dev,
                                         hal_tsc2007_function_t func,
                                         hal_tsc2007_power_t pwr,
                                         hal_tsc2007_resolution_t res) {
  const uint8_t cmd = tsc2007_command_byte(func, pwr, res);
  bool write_ok = false;
  const uint8_t status = hal_i2c_write_byte_bus(
      dev->cfg.i2c_bus, dev->cfg.i2c_addr, cmd, &write_ok);
  if (!write_ok || status != 0u) {
    return 0u;
  }

  hal_delay_us(TSC2007_CONVERSION_DELAY_US);

  uint8_t reply[2] = {0u, 0u};
  if (!hal_i2c_read_bytes_bus(dev->cfg.i2c_bus, dev->cfg.i2c_addr, reply,
                              sizeof(reply))) {
    return 0u;
  }

  return (uint16_t)(((uint16_t)reply[0] << 4u) | (reply[1] >> 4u));
}

static uint16_t tsc2007_abs_diff(uint16_t a, uint16_t b) {
  return (a >= b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

hal_tsc2007_config_t hal_tsc2007_default_config(void) {
  hal_tsc2007_config_t cfg = {
      0u,
      HAL_TSC2007_I2C_ADDR_DEFAULT,
  };
  return cfg;
}

bool hal_tsc2007_init(hal_tsc2007_t *dev, const hal_tsc2007_config_t *cfg) {
  if (!tsc2007_ensure_mutex(dev)) {
    return false;
  }

  hal_tsc2007_config_t effective =
      (cfg != NULL) ? *cfg : hal_tsc2007_default_config();

  hal_mutex_lock(dev->mutex);

  dev->cfg = effective;
  dev->initialized = false;

  const bool present =
      !hal_i2c_is_busy_bus(dev->cfg.i2c_bus, dev->cfg.i2c_addr);
  if (present) {
    (void)tsc2007_command_unlocked(dev, HAL_TSC2007_MEASURE_TEMP0,
                                   HAL_TSC2007_POWERDOWN_IRQON,
                                   HAL_TSC2007_ADC_12BIT);
    dev->initialized = true;
  }

  hal_mutex_unlock(dev->mutex);

  if (!present) {
    hal_tsc2007_deinit(dev);
  }
  return present;
}

void hal_tsc2007_deinit(hal_tsc2007_t *dev) {
  if (dev == NULL || dev->mutex == NULL) {
    return;
  }

  hal_mutex_t mutex = dev->mutex;
  hal_mutex_lock(mutex);
  dev->initialized = false;
  hal_mutex_unlock(mutex);

  hal_mutex_destroy(mutex);
  dev->mutex = NULL;
}

uint16_t hal_tsc2007_command(hal_tsc2007_t *dev, hal_tsc2007_function_t func,
                             hal_tsc2007_power_t pwr,
                             hal_tsc2007_resolution_t res) {
  if (!tsc2007_valid(dev)) {
    return 0u;
  }

  hal_mutex_lock(dev->mutex);
  const uint16_t value = tsc2007_command_unlocked(dev, func, pwr, res);
  hal_mutex_unlock(dev->mutex);
  return value;
}

bool hal_tsc2007_read_touch(hal_tsc2007_t *dev, uint16_t *x, uint16_t *y,
                            uint16_t *z1, uint16_t *z2) {
  if (!tsc2007_valid(dev) || x == NULL || y == NULL || z1 == NULL ||
      z2 == NULL) {
    return false;
  }

  hal_mutex_lock(dev->mutex);

  *z1 =
      tsc2007_command_unlocked(dev, HAL_TSC2007_MEASURE_Z1,
                               HAL_TSC2007_ADON_IRQOFF, HAL_TSC2007_ADC_12BIT);
  *z2 =
      tsc2007_command_unlocked(dev, HAL_TSC2007_MEASURE_Z2,
                               HAL_TSC2007_ADON_IRQOFF, HAL_TSC2007_ADC_12BIT);
  const uint16_t x1 =
      tsc2007_command_unlocked(dev, HAL_TSC2007_MEASURE_X,
                               HAL_TSC2007_ADON_IRQOFF, HAL_TSC2007_ADC_12BIT);
  const uint16_t y1 =
      tsc2007_command_unlocked(dev, HAL_TSC2007_MEASURE_Y,
                               HAL_TSC2007_ADON_IRQOFF, HAL_TSC2007_ADC_12BIT);
  const uint16_t x2 =
      tsc2007_command_unlocked(dev, HAL_TSC2007_MEASURE_X,
                               HAL_TSC2007_ADON_IRQOFF, HAL_TSC2007_ADC_12BIT);
  const uint16_t y2 =
      tsc2007_command_unlocked(dev, HAL_TSC2007_MEASURE_Y,
                               HAL_TSC2007_ADON_IRQOFF, HAL_TSC2007_ADC_12BIT);
  (void)tsc2007_command_unlocked(dev, HAL_TSC2007_MEASURE_TEMP0,
                                 HAL_TSC2007_POWERDOWN_IRQON,
                                 HAL_TSC2007_ADC_12BIT);

  bool ok = false;
  if (tsc2007_abs_diff(x1, x2) <= HAL_TSC2007_STABILITY_THRESHOLD &&
      tsc2007_abs_diff(y1, y2) <= HAL_TSC2007_STABILITY_THRESHOLD) {
    *x = x1;
    *y = y1;
    ok = (*x != HAL_TSC2007_TOUCH_INVALID) && (*y != HAL_TSC2007_TOUCH_INVALID);
  }

  hal_mutex_unlock(dev->mutex);
  return ok;
}

hal_tsc2007_point_t hal_tsc2007_get_point(hal_tsc2007_t *dev) {
  uint16_t x = 0u;
  uint16_t y = 0u;
  uint16_t z1 = 0u;
  uint16_t z2 = 0u;

  hal_tsc2007_point_t point = {0, 0, 0};
  if (hal_tsc2007_read_touch(dev, &x, &y, &z1, &z2)) {
    point.x = (int16_t)x;
    point.y = (int16_t)y;
    point.z = (int16_t)z1;
  }
  return point;
}

#endif /* HAL_ENABLE_TSC2007 && HAL_ENABLE_I2C */
#endif /* supported target */
