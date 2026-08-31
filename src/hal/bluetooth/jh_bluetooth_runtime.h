#pragma once

#include "hal/core/hal_config.h"
#include "hal/system/hal_board.h"
#include "hal/system/jh_board_runtime.h"

static inline hal_board_capabilities_t
jh_bluetooth_required_le_board_capabilities(void) {
  return HAL_BOARD_CAP_BLUETOOTH_LE_CONTROLLER |
         (HAL_BOARD_HAS_EXTERNAL_RADIO_FRONTEND
              ? HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND
              : 0u);
}

static inline hal_board_capabilities_t
jh_bluetooth_required_classic_board_capabilities(void) {
  return HAL_BOARD_CAP_BLUETOOTH_CLASSIC_CONTROLLER |
         (HAL_BOARD_HAS_EXTERNAL_RADIO_FRONTEND
              ? HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND
              : 0u);
}

static inline hal_status_t
jh_bluetooth_require_capabilities(hal_board_capabilities_t capabilities) {
  const hal_status_t status = hal_board_require_capabilities(capabilities);
  return status == HAL_EUNINIT ? HAL_OK : status;
}

static inline hal_status_t jh_bluetooth_require_le_hardware(void) {
  return jh_bluetooth_require_capabilities(
      jh_bluetooth_required_le_board_capabilities());
}

static inline hal_status_t jh_bluetooth_require_classic_hardware(void) {
  return jh_bluetooth_require_capabilities(
      jh_bluetooth_required_classic_board_capabilities());
}

static inline void
jh_bluetooth_publish_available(hal_board_capabilities_t profile_capability) {
  (void)jh_board_runtime_set_available(profile_capability);
  (void)jh_board_runtime_set_available(HAL_BOARD_CAP_BLUETOOTH_CONTROLLER);
}

static inline void
jh_bluetooth_publish_failed(hal_board_capabilities_t profile_capability) {
  (void)jh_board_runtime_set_failed(profile_capability);
  (void)jh_board_runtime_set_failed(HAL_BOARD_CAP_BLUETOOTH_CONTROLLER);
}

static inline void
jh_bluetooth_publish_inactive(hal_board_capabilities_t profile_capability) {
  (void)jh_board_runtime_set_inactive(profile_capability);
  (void)jh_board_runtime_set_inactive(HAL_BOARD_CAP_BLUETOOTH_CONTROLLER);
}
