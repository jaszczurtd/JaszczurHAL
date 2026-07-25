/*
 * TSC2007 transaction flow is based on the Adafruit TSC2007 library by
 * Limor Fried for Adafruit Industries. This implementation preserves the
 * command byte layout, 500 us conversion wait, duplicate X/Y stability check,
 * power-down command and Z1 pressure reporting while routing transport,
 * timing and synchronization through JaszczurHAL.
 */

#include "hal/hal_target.h"
#if (HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "hal/hal_config.h"
#if defined(HAL_ENABLE_TSC2007) && defined(HAL_ENABLE_I2C)

#include "hal/hal_tsc2007.h"

#include "hal/hal_i2c.h"
#include "hal/hal_system.h"
#include "hal/impl/shared/hal_mutex_once.h"

#include <stddef.h>

#define TSC2007_CONVERSION_DELAY_US 500u

static hal_status_t tsc2007_ensure_mutex(hal_tsc2007_t *dev) {
  if (dev == NULL) {
    return HAL_EINVAL;
  }
  return jh_hal_mutex_create_once(&dev->mutex) != NULL ? HAL_OK : HAL_ENOMEM;
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

static hal_status_t tsc2007_command_unlocked_ex(hal_tsc2007_t *dev,
                                                hal_tsc2007_function_t func,
                                                hal_tsc2007_power_t pwr,
                                                hal_tsc2007_resolution_t res,
                                                uint16_t *out_value) {
  if (out_value == NULL) {
    return HAL_EINVAL;
  }
  *out_value = 0u;

  const uint8_t cmd = tsc2007_command_byte(func, pwr, res);
  bool write_ok = false;
  const uint8_t status = hal_i2c_write_byte_bus(
      dev->cfg.i2c_bus, dev->cfg.i2c_addr, cmd, &write_ok);
  if (!write_ok || status != 0u) {
    return HAL_EBUS;
  }

  hal_delay_us(TSC2007_CONVERSION_DELAY_US);

  uint8_t reply[2] = {0u, 0u};
  if (!hal_i2c_read_bytes_bus(dev->cfg.i2c_bus, dev->cfg.i2c_addr, reply,
                              sizeof(reply))) {
    return HAL_EBUS;
  }

  *out_value = (uint16_t)(((uint16_t)reply[0] << 4u) | (reply[1] >> 4u));
  return HAL_OK;
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

hal_status_t hal_tsc2007_init_ex(hal_tsc2007_t *dev,
                                 const hal_tsc2007_config_t *cfg) {
  hal_status_t status = tsc2007_ensure_mutex(dev);
  if (status != HAL_OK) {
    return status;
  }

  hal_tsc2007_config_t effective =
      (cfg != NULL) ? *cfg : hal_tsc2007_default_config();

  hal_mutex_lock(dev->mutex);

  dev->cfg = effective;
  dev->initialized = false;

  const bool present =
      !hal_i2c_is_busy_bus(dev->cfg.i2c_bus, dev->cfg.i2c_addr);
  if (present) {
    uint16_t ignored = 0u;
    (void)tsc2007_command_unlocked_ex(dev, HAL_TSC2007_MEASURE_TEMP0,
                                      HAL_TSC2007_POWERDOWN_IRQON,
                                      HAL_TSC2007_ADC_12BIT, &ignored);
    status = HAL_OK;
    dev->initialized = true;
  } else {
    status = HAL_ENOENT;
  }

  hal_mutex_unlock(dev->mutex);

  if (status != HAL_OK) {
    hal_tsc2007_deinit(dev);
  }
  return status;
}

bool hal_tsc2007_init(hal_tsc2007_t *dev, const hal_tsc2007_config_t *cfg) {
  return hal_status_to_bool(hal_tsc2007_init_ex(dev, cfg));
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
  uint16_t value = 0u;
  (void)hal_tsc2007_command_ex(dev, func, pwr, res, &value);
  return value;
}

hal_status_t hal_tsc2007_command_ex(hal_tsc2007_t *dev,
                                    hal_tsc2007_function_t func,
                                    hal_tsc2007_power_t pwr,
                                    hal_tsc2007_resolution_t res,
                                    uint16_t *out_value) {
  if (out_value == NULL) {
    return HAL_EINVAL;
  }
  *out_value = 0u;

  if (!tsc2007_valid(dev)) {
    return HAL_EUNINIT;
  }

  hal_mutex_lock(dev->mutex);
  const hal_status_t status =
      tsc2007_command_unlocked_ex(dev, func, pwr, res, out_value);
  hal_mutex_unlock(dev->mutex);
  return status;
}

hal_status_t hal_tsc2007_read_touch_ex(hal_tsc2007_t *dev, uint16_t *x,
                                       uint16_t *y, uint16_t *z1,
                                       uint16_t *z2) {
  if (x == NULL || y == NULL || z1 == NULL || z2 == NULL) {
    return HAL_EINVAL;
  }
  if (!tsc2007_valid(dev)) {
    return HAL_EUNINIT;
  }

  hal_mutex_lock(dev->mutex);

  uint16_t x1 = 0u;
  uint16_t y1 = 0u;
  uint16_t x2 = 0u;
  uint16_t y2 = 0u;
  uint16_t ignored = 0u;
  hal_status_t status = tsc2007_command_unlocked_ex(dev, HAL_TSC2007_MEASURE_Z1,
                                                    HAL_TSC2007_ADON_IRQOFF,
                                                    HAL_TSC2007_ADC_12BIT, z1);
  if (status == HAL_OK) {
    status = tsc2007_command_unlocked_ex(dev, HAL_TSC2007_MEASURE_Z2,
                                         HAL_TSC2007_ADON_IRQOFF,
                                         HAL_TSC2007_ADC_12BIT, z2);
  }
  if (status == HAL_OK) {
    status = tsc2007_command_unlocked_ex(dev, HAL_TSC2007_MEASURE_X,
                                         HAL_TSC2007_ADON_IRQOFF,
                                         HAL_TSC2007_ADC_12BIT, &x1);
  }
  if (status == HAL_OK) {
    status = tsc2007_command_unlocked_ex(dev, HAL_TSC2007_MEASURE_Y,
                                         HAL_TSC2007_ADON_IRQOFF,
                                         HAL_TSC2007_ADC_12BIT, &y1);
  }
  if (status == HAL_OK) {
    status = tsc2007_command_unlocked_ex(dev, HAL_TSC2007_MEASURE_X,
                                         HAL_TSC2007_ADON_IRQOFF,
                                         HAL_TSC2007_ADC_12BIT, &x2);
  }
  if (status == HAL_OK) {
    status = tsc2007_command_unlocked_ex(dev, HAL_TSC2007_MEASURE_Y,
                                         HAL_TSC2007_ADON_IRQOFF,
                                         HAL_TSC2007_ADC_12BIT, &y2);
  }
  if (status == HAL_OK) {
    (void)tsc2007_command_unlocked_ex(dev, HAL_TSC2007_MEASURE_TEMP0,
                                      HAL_TSC2007_POWERDOWN_IRQON,
                                      HAL_TSC2007_ADC_12BIT, &ignored);
  }

  if (status == HAL_OK &&
      tsc2007_abs_diff(x1, x2) <= HAL_TSC2007_STABILITY_THRESHOLD &&
      tsc2007_abs_diff(y1, y2) <= HAL_TSC2007_STABILITY_THRESHOLD) {
    *x = x1;
    *y = y1;
    if (*x == HAL_TSC2007_TOUCH_INVALID || *y == HAL_TSC2007_TOUCH_INVALID) {
      status = HAL_ENOENT;
    }
  } else if (status == HAL_OK) {
    status = HAL_ENOENT;
  }

  hal_mutex_unlock(dev->mutex);
  return status;
}

bool hal_tsc2007_read_touch(hal_tsc2007_t *dev, uint16_t *x, uint16_t *y,
                            uint16_t *z1, uint16_t *z2) {
  return hal_status_to_bool(hal_tsc2007_read_touch_ex(dev, x, y, z1, z2));
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
