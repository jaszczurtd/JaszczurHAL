#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../hal_config.h"
#if defined(HAL_ENABLE_CAN) && defined(HAL_ENABLE_STM32G474_FDCAN) &&          \
    !defined(HAL_ENABLE_MCP2515) && !defined(HAL_ENABLE_MCP251XFD)

#include "../../hal_can.h"

hal_can_config_t hal_can_default_config(void) {
  hal_can_config_t cfg = {};
  cfg.backend = HAL_CAN_BACKEND_STM32G474_FDCAN;
  cfg.stm32g474_fdcan.rx_pin = 11u; /* PA11 AF9 */
  cfg.stm32g474_fdcan.tx_pin = 12u; /* PA12 AF9 */
  cfg.stm32g474_fdcan.arbitration_bitrate_hz = 500000u;
  cfg.stm32g474_fdcan.data_bitrate_hz = 2000000u;
  cfg.stm32g474_fdcan.enable_fd = true;
  cfg.stm32g474_fdcan.one_shot_tx = true;
  return cfg;
}

#endif
#endif
