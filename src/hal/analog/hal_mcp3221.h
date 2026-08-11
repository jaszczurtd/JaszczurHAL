#pragma once

#include "hal/core/hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_MCP3221

#include "hal/core/hal_status.h"
#include "hal/system/hal_sync.h"

#include <stdbool.h>
#include <stdint.h>

#define HAL_MCP3221_I2C_ADDR_DEFAULT 0x4Du

typedef struct {
  uint8_t i2c_bus;
  uint8_t i2c_addr;
} hal_mcp3221_config_t;

typedef struct {
  hal_mcp3221_config_t cfg;
  hal_mutex_t mutex;
  uint16_t value;
  bool initialized;
} hal_mcp3221_t;

hal_mcp3221_config_t hal_mcp3221_default_config(void);
hal_status_t hal_mcp3221_init_ex(hal_mcp3221_t *dev,
                                 const hal_mcp3221_config_t *cfg);
bool hal_mcp3221_init(hal_mcp3221_t *dev, const hal_mcp3221_config_t *cfg);
void hal_mcp3221_deinit(hal_mcp3221_t *dev);
hal_status_t hal_mcp3221_read_ex(hal_mcp3221_t *dev, uint16_t *out_value);
uint16_t hal_mcp3221_read(hal_mcp3221_t *dev);

#endif /* HAL_ENABLE_MCP3221 */
#ifdef __cplusplus
}
#endif
