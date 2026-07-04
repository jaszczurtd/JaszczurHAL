#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_HC595

#include "hal_spi.h"
#include "hal_status.h"
#include "hal_sync.h"

#include <stdbool.h>
#include <stdint.h>

#define HAL_HC595_MAX_CHIPS 4u

typedef struct {
  uint8_t spi_bus;
  uint8_t cs_pin;
  uint8_t chips;
  uint32_t clock_hz;
} hal_hc595_config_t;

typedef struct {
  hal_hc595_config_t cfg;
  hal_mutex_t mutex;
  uint32_t outputs;
  uint32_t inverted;
  bool initialized;
} hal_hc595_t;

hal_hc595_config_t hal_hc595_default_config(uint8_t cs_pin);
hal_status_t hal_hc595_init_ex(hal_hc595_t *dev, const hal_hc595_config_t *cfg);
bool hal_hc595_init(hal_hc595_t *dev, const hal_hc595_config_t *cfg);
void hal_hc595_deinit(hal_hc595_t *dev);
uint8_t hal_hc595_output_count(const hal_hc595_t *dev);
hal_status_t hal_hc595_write_pin_ex(hal_hc595_t *dev, uint8_t pin, bool on);
bool hal_hc595_write_pin(hal_hc595_t *dev, uint8_t pin, bool on);
hal_status_t hal_hc595_write_all_ex(hal_hc595_t *dev, uint32_t value);
bool hal_hc595_write_all(hal_hc595_t *dev, uint32_t value);
hal_status_t hal_hc595_config_pin_ex(hal_hc595_t *dev, uint8_t pin,
                                     bool inverted);
bool hal_hc595_config_pin(hal_hc595_t *dev, uint8_t pin, bool inverted);
uint32_t hal_hc595_output_latch(const hal_hc595_t *dev);

#endif /* HAL_ENABLE_HC595 */
#ifdef __cplusplus
}
#endif
