#pragma once

#include "../../../hal_board.h"
#include "../../../hal_config.h"

static inline hal_board_capabilities_t
jh_network_required_board_capabilities(void) {
#if defined(HAL_NETWORK_BACKEND_CYW43)
  return HAL_BOARD_CAP_CYW43 | (HAL_BOARD_HAS_EXTERNAL_RADIO_FRONTEND
                                    ? HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND
                                    : 0u);
#else
  return 0u;
#endif
}

static inline hal_status_t jh_network_require_hardware(void) {
  const hal_board_capabilities_t capabilities =
      jh_network_required_board_capabilities();
  if (capabilities == 0u) {
    return HAL_OK;
  }
  const hal_status_t status = hal_board_require_capabilities(capabilities);
  return status == HAL_EUNINIT ? HAL_OK : status;
}

static inline hal_status_t jh_network_require_ready(void) {
  const hal_board_capabilities_t capabilities =
      jh_network_required_board_capabilities();
  return capabilities == 0u ? HAL_OK
                            : hal_board_require_capabilities(capabilities);
}
