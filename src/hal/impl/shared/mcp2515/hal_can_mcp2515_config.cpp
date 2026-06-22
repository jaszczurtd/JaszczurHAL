#include "../../../hal_config.h"
#if defined(HAL_ENABLE_CAN) && defined(HAL_ENABLE_MCP2515)

#include "../../../hal_can.h"

hal_can_config_t hal_can_default_config(void) {
  hal_can_config_t cfg = {};
  cfg.backend = HAL_CAN_BACKEND_MCP2515;
  cfg.mcp2515.spi_bus = 0u;
  cfg.mcp2515.cs_pin = 0u;
  cfg.mcp2515.bitrate_hz = 500000u;
  cfg.mcp2515.oscillator_hz = 8000000u;
  cfg.mcp2515.one_shot_tx = true;
  cfg.mcp2515.sleep_wakeup = true;
  return cfg;
}

#endif /* HAL_ENABLE_CAN && HAL_ENABLE_MCP2515 */
