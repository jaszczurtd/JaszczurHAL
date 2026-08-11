#pragma once

#include "hal/core/hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_MCP4725

#include "hal/core/hal_status.h"
#include "hal/system/hal_sync.h"

#include <stdbool.h>
#include <stdint.h>

#define HAL_MCP4725_I2C_ADDR_DEFAULT 0x60u
#define HAL_MCP4725_MAX_VALUE 4095u

typedef struct {
  uint8_t i2c_bus;
  uint8_t i2c_addr;
  bool wake_on_init;
} hal_mcp4725_config_t;

typedef struct {
  hal_mcp4725_config_t cfg;
  hal_mutex_t mutex;
  uint16_t value;
  bool initialized;
} hal_mcp4725_t;

hal_mcp4725_config_t hal_mcp4725_default_config(void);
hal_status_t hal_mcp4725_init_ex(hal_mcp4725_t *dev,
                                 const hal_mcp4725_config_t *cfg);
bool hal_mcp4725_init(hal_mcp4725_t *dev, const hal_mcp4725_config_t *cfg);
void hal_mcp4725_deinit(hal_mcp4725_t *dev);
hal_status_t hal_mcp4725_write_ex(hal_mcp4725_t *dev, uint16_t value);
bool hal_mcp4725_write(hal_mcp4725_t *dev, uint16_t value);
uint16_t hal_mcp4725_output_latch(const hal_mcp4725_t *dev);

#endif /* HAL_ENABLE_MCP4725 */
#ifdef __cplusplus
}
#endif
