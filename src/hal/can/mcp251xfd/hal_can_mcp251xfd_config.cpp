#include "hal/core/hal_config.h"
#if defined(HAL_ENABLE_CAN) && defined(HAL_ENABLE_MCP251XFD) &&                \
    !defined(HAL_ENABLE_MCP2515)

#include "hal/can/hal_can.h"

hal_can_config_t hal_can_default_config(void) {
  hal_can_config_t cfg = {};
  cfg.backend = HAL_CAN_BACKEND_MCP251XFD;
  cfg.mcp251xfd.spi_bus = 0u;
  cfg.mcp251xfd.cs_pin = 0u;
  cfg.mcp251xfd.arbitration_bitrate_hz = 500000u;
  cfg.mcp251xfd.data_bitrate_hz = 2000000u;
  cfg.mcp251xfd.oscillator_hz = 40000000u;
  cfg.mcp251xfd.spi_clock_hz = 10000000u;
  cfg.mcp251xfd.enable_fd = true;
  cfg.mcp251xfd.one_shot_tx = true;
  cfg.mcp251xfd.sleep_wakeup = true;
  return cfg;
}

#endif /* HAL_ENABLE_CAN && HAL_ENABLE_MCP251XFD && !HAL_ENABLE_MCP2515 */
