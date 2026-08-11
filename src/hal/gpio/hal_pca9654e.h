#pragma once

#include "hal/core/hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_PCA9654E

#include "hal/core/hal_status.h"
#include "hal/system/hal_sync.h"

#include <stdbool.h>
#include <stdint.h>

#define HAL_PCA9654E_I2C_ADDR_DEFAULT 0x20u
#define HAL_PCA9654E_PIN_COUNT 8u

typedef struct {
  uint8_t i2c_bus;
  uint8_t i2c_addr;
} hal_pca9654e_config_t;

typedef struct {
  hal_pca9654e_config_t cfg;
  hal_mutex_t mutex;
  uint8_t outputs;
  uint8_t inverted;
  bool initialized;
} hal_pca9654e_t;

hal_pca9654e_config_t hal_pca9654e_default_config(void);
hal_status_t hal_pca9654e_init_ex(hal_pca9654e_t *dev,
                                  const hal_pca9654e_config_t *cfg);
bool hal_pca9654e_init(hal_pca9654e_t *dev, const hal_pca9654e_config_t *cfg);
void hal_pca9654e_deinit(hal_pca9654e_t *dev);
hal_status_t hal_pca9654e_write_pin_ex(hal_pca9654e_t *dev, uint8_t pin,
                                       bool on);
bool hal_pca9654e_write_pin(hal_pca9654e_t *dev, uint8_t pin, bool on);
hal_status_t hal_pca9654e_write_all_ex(hal_pca9654e_t *dev, uint8_t value);
bool hal_pca9654e_write_all(hal_pca9654e_t *dev, uint8_t value);
hal_status_t hal_pca9654e_config_pin_ex(hal_pca9654e_t *dev, uint8_t pin,
                                        bool inverted);
bool hal_pca9654e_config_pin(hal_pca9654e_t *dev, uint8_t pin, bool inverted);
uint8_t hal_pca9654e_output_latch(const hal_pca9654e_t *dev);

#endif /* HAL_ENABLE_PCA9654E */
#ifdef __cplusplus
}
#endif
