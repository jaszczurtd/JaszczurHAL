#pragma once

#include "hal/core/hal_config.h"
#include "hal/system/hal_board.h"

static inline hal_board_capabilities_t
jh_network_required_board_capabilities(void) {
#if defined(HAL_NETWORK_BACKEND_CYW43)
  return HAL_BOARD_CAP_CYW43 | (HAL_BOARD_HAS_EXTERNAL_RADIO_FRONTEND
                                    ? HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND
                                    : 0u);
#elif defined(HAL_NETWORK_BACKEND_ESP_IDF)
  return HAL_BOARD_CAP_NATIVE_WIFI;
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
