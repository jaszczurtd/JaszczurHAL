/*
 * BH1750 transaction flow and lux conversion are based on the
 * ArtronShop_BH1750 Arduino library by ArtronShop Co., Ltd. (maintainer:
 * Sonthaya.NT). This implementation keeps the proven mode command,
 * first-measurement delay, exact two-byte sample read and raw/1.2 lux scaling,
 * while using only JaszczurHAL I2C, timing and synchronization primitives.
 */

#include "../../../hal_target.h"
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "../../../hal_config.h"
#if defined(HAL_ENABLE_BH1750) && defined(HAL_ENABLE_I2C)

#include "../../../hal_bh1750.h"

#include "../../../hal_i2c.h"
#include "../../../hal_system.h"

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

bool hal_bh1750_init(hal_bh1750_t *dev, const hal_bh1750_config_t *cfg) {
  if (dev == NULL) {
    return false;
  }

  hal_bh1750_config_t effective =
      (cfg != NULL) ? *cfg : hal_bh1750_default_config();

  dev->cfg = effective;
  dev->initialized = false;
  dev->mutex = hal_mutex_create();
  if (dev->mutex == NULL) {
    return false;
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
  return ok;
}

void hal_bh1750_deinit(hal_bh1750_t *dev) {
  if (dev == NULL || dev->mutex == NULL) {
    return;
  }
  hal_mutex_destroy(dev->mutex);
  dev->mutex = NULL;
  dev->initialized = false;
}

float hal_bh1750_light(hal_bh1750_t *dev) {
  if (!bh1750_valid(dev)) {
    return -1.0f;
  }

  hal_mutex_lock(dev->mutex);

  uint8_t data[2] = {0u, 0u};
  const bool ok = hal_i2c_read_bytes_bus(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                                         data, sizeof(data));

  hal_mutex_unlock(dev->mutex);

  if (!ok) {
    return -1.0f;
  }

  const uint16_t raw = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
  return (float)raw / 1.2f;
}

#endif /* HAL_ENABLE_BH1750 && HAL_ENABLE_I2C */
#endif /* supported target */
