#pragma once

#include "hal/core/hal_config.h"
#include "hal/system/hal_board.h"

static inline hal_board_capabilities_t
jh_ble_required_board_capabilities(void) {
  return HAL_BOARD_CAP_BLUETOOTH_CONTROLLER |
         (HAL_BOARD_HAS_EXTERNAL_RADIO_FRONTEND
              ? HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND
              : 0u);
}

static inline hal_status_t jh_ble_require_hardware(void) {
  const hal_status_t status =
      hal_board_require_capabilities(jh_ble_required_board_capabilities());
  return status == HAL_EUNINIT ? HAL_OK : status;
}
