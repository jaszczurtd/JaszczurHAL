/*
 * BH1750 transaction flow and lux conversion are based on the
 * ArtronShop_BH1750 Arduino library by ArtronShop Co., Ltd. (maintainer:
 * Sonthaya.NT). This implementation keeps the proven mode command,
 * first-measurement delay, exact two-byte sample read and raw/1.2 lux scaling,
 * while using only JaszczurHAL I2C, timing and synchronization primitives.
 */

#include "hal/core/hal_target.h"
#if (HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "hal/core/hal_config.h"
#if defined(HAL_ENABLE_BH1750) && defined(HAL_ENABLE_I2C)

#include "hal/sensors/hal_bh1750.h"

#include "hal/core/jh_endian.h"
#include "hal/i2c/hal_i2c.h"
#include "hal/system/hal_system.h"

#include <stddef.h>

#define BH1750_CMD_CONTINUOUS_H_RESOLUTION 0x10u
#define BH1750_FIRST_MEASUREMENT_DELAY_MS 180u

static bool bh1750_valid(hal_bh1750_t *dev) {
  return (dev != NULL) && (dev->mutex != NULL);
}

hal_bh1750_config_t hal_bh1750_default_config(void) {
  hal_bh1750_config_t cfg = {
      0u,
      HAL_BH1750_I2C_ADDR_DEFAULT,
  };
  return cfg;
}

hal_status_t hal_bh1750_init_ex(hal_bh1750_t *dev,
                                const hal_bh1750_config_t *cfg) {
  if (dev == NULL) {
    return HAL_EINVAL;
  }

  hal_bh1750_config_t effective =
      (cfg != NULL) ? *cfg : hal_bh1750_default_config();

  dev->cfg = effective;
  dev->initialized = false;
  dev->mutex = hal_mutex_create();
  if (dev->mutex == NULL) {
    return HAL_ENOMEM;
  }

  hal_mutex_lock(dev->mutex);

  hal_i2c_begin_transmission_bus(dev->cfg.i2c_bus, dev->cfg.i2c_addr);
  (void)hal_i2c_write_bus(dev->cfg.i2c_bus, BH1750_CMD_CONTINUOUS_H_RESOLUTION);
  const bool ok = (hal_i2c_end_transmission_bus(dev->cfg.i2c_bus) == 0u);
  if (ok) {
    hal_delay_ms(BH1750_FIRST_MEASUREMENT_DELAY_MS);
    dev->initialized = true;
  }

  hal_mutex_unlock(dev->mutex);

  if (!ok) {
    hal_bh1750_deinit(dev);
  }
  return hal_status_from_bool(ok, HAL_EBUS);
}

bool hal_bh1750_init(hal_bh1750_t *dev, const hal_bh1750_config_t *cfg) {
  return hal_status_to_bool(hal_bh1750_init_ex(dev, cfg));
}

void hal_bh1750_deinit(hal_bh1750_t *dev) {
  if (dev == NULL || dev->mutex == NULL) {
    return;
  }
  hal_mutex_destroy(dev->mutex);
  dev->mutex = NULL;
  dev->initialized = false;
}

hal_status_t hal_bh1750_light_ex(hal_bh1750_t *dev, float *out_lux) {
  if (out_lux == NULL) {
    return HAL_EINVAL;
  }
  *out_lux = -1.0f;

  if (!bh1750_valid(dev)) {
    return HAL_EUNINIT;
  }

  hal_mutex_lock(dev->mutex);

  uint8_t data[2] = {0u, 0u};
  const bool ok = hal_i2c_read_bytes_bus(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                                         data, sizeof(data));

  hal_mutex_unlock(dev->mutex);

  if (!ok) {
    return HAL_EBUS;
  }

  const uint16_t raw = jh_load_be16(data);
  *out_lux = (float)raw / 1.2f;
  return HAL_OK;
}

float hal_bh1750_light(hal_bh1750_t *dev) {
  float lux = -1.0f;
  (void)hal_bh1750_light_ex(dev, &lux);
  return lux;
}

#endif /* HAL_ENABLE_BH1750 && HAL_ENABLE_I2C */
#endif /* supported target */
