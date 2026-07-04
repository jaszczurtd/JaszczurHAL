#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_PCF8574

#include "hal_status.h"
#include "hal_sync.h"

#include <stdbool.h>
#include <stdint.h>

#define HAL_PCF8574_I2C_ADDR_DEFAULT 0x20u
#define HAL_PCF8574_PIN_COUNT 8u

typedef struct {
  uint8_t i2c_bus;
  uint8_t i2c_addr;
  uint8_t initial_latch;
} hal_pcf8574_config_t;

typedef struct {
  hal_pcf8574_config_t cfg;
  hal_mutex_t mutex;
  uint8_t latch;
  uint8_t inverted;
  bool initialized;
} hal_pcf8574_t;

hal_pcf8574_config_t hal_pcf8574_default_config(void);
hal_status_t hal_pcf8574_init_ex(hal_pcf8574_t *dev,
                                 const hal_pcf8574_config_t *cfg);
bool hal_pcf8574_init(hal_pcf8574_t *dev, const hal_pcf8574_config_t *cfg);
void hal_pcf8574_deinit(hal_pcf8574_t *dev);
hal_status_t hal_pcf8574_write_all_ex(hal_pcf8574_t *dev, uint8_t value);
bool hal_pcf8574_write_all(hal_pcf8574_t *dev, uint8_t value);
hal_status_t hal_pcf8574_write_pin_ex(hal_pcf8574_t *dev, uint8_t pin, bool on);
bool hal_pcf8574_write_pin(hal_pcf8574_t *dev, uint8_t pin, bool on);
hal_status_t hal_pcf8574_read_all_ex(hal_pcf8574_t *dev, uint8_t *out_value);
uint8_t hal_pcf8574_read_all(hal_pcf8574_t *dev);
hal_status_t hal_pcf8574_read_pin_ex(hal_pcf8574_t *dev, uint8_t pin,
                                     bool *out_value);
bool hal_pcf8574_read_pin(hal_pcf8574_t *dev, uint8_t pin);
hal_status_t hal_pcf8574_config_pin_ex(hal_pcf8574_t *dev, uint8_t pin,
                                       bool inverted);
bool hal_pcf8574_config_pin(hal_pcf8574_t *dev, uint8_t pin, bool inverted);
uint8_t hal_pcf8574_output_latch(const hal_pcf8574_t *dev);

#endif /* HAL_ENABLE_PCF8574 */
#ifdef __cplusplus
}
#endif
