#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET

/**
 * @file hal_bluetooth_avrcp_target.h
 * @brief Minimal Bluetooth Classic AVRCP Target absolute-volume API.
 */

#include "hal/bluetooth/hal_bluetooth_classic.h"
#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** AVRCP control-channel lifecycle state. */
typedef enum {
  HAL_BLUETOOTH_AVRCP_TARGET_STATE_CLOSED = 0,
  HAL_BLUETOOTH_AVRCP_TARGET_STATE_READY,
  HAL_BLUETOOTH_AVRCP_TARGET_STATE_CONNECTED,
  HAL_BLUETOOTH_AVRCP_TARGET_STATE_FAILED,
} hal_bluetooth_avrcp_target_state_t;

/** Minimal AVRCP Target state and diagnostics. */
typedef struct {
  hal_bluetooth_avrcp_target_state_t state;
  hal_status_t last_status;
  hal_bluetooth_classic_address_t peer_address;
  uint32_t generation;
  uint32_t volume_changes;
  uint32_t overwritten_volume_changes;
  uint8_t volume; /**< Absolute volume from 0 through 127. */
  bool volume_pending;
} hal_bluetooth_avrcp_target_info_t;

typedef struct hal_bluetooth_avrcp_target_impl_s
    hal_bluetooth_avrcp_target_impl_t;
/** Opaque handle for one attached AVRCP Target profile. */
typedef hal_bluetooth_avrcp_target_impl_t *hal_bluetooth_avrcp_target_t;

/**
 * @brief Attach one minimal AVRCP Target to an open Classic manager.
 * @param classic Live Classic manager retained by the caller.
 * @param initial_volume Initial absolute volume from 0 through 127.
 * @param out_target Receives the profile handle and is cleared on entry; must
 * not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid input, HAL_EUNINIT for an invalid
 * manager, HAL_EBUSY when already attached, HAL_ENOMEM, or a backend error.
 */
hal_status_t
hal_bluetooth_avrcp_target_open(hal_bluetooth_classic_t classic,
                                uint8_t initial_volume,
                                hal_bluetooth_avrcp_target_t *out_target);

/**
 * @brief Close AVRCP without closing the shared Classic manager.
 * @param target Live profile handle.
 * @return HAL_OK, HAL_EUNINIT for a stale handle, HAL_ENOMEM, or a backend
 * detach error.
 */
hal_status_t
hal_bluetooth_avrcp_target_close(hal_bluetooth_avrcp_target_t target);

/**
 * @brief Read AVRCP connection and volume diagnostics.
 * @param target Live profile handle.
 * @param out_info Receives a snapshot; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EUNINIT for a stale
 * handle, or HAL_ENOMEM.
 */
hal_status_t hal_bluetooth_avrcp_target_get_info(
    hal_bluetooth_avrcp_target_t target,
    hal_bluetooth_avrcp_target_info_t *out_info);

/**
 * @brief Pop the newest volume requested by the connected Controller.
 * @param target Live profile handle.
 * @param out_absolute_volume Receives a value from 0 through 127; must not be
 * NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EUNINIT for a stale
 * handle, HAL_EAGAIN when no change is pending, or HAL_ENOMEM.
 */
hal_status_t
hal_bluetooth_avrcp_target_volume_next(hal_bluetooth_avrcp_target_t target,
                                       uint8_t *out_absolute_volume);

/**
 * @brief Set current volume and notify a subscribed Controller.
 * @param target Live profile handle.
 * @param absolute_volume Volume from 0 through 127.
 * @return HAL_OK, HAL_EINVAL above 127, HAL_EUNINIT for a stale handle,
 * HAL_ENOMEM, or a backend error. Before connection the value is retained and
 * HAL_OK is returned without sending a packet.
 */
hal_status_t
hal_bluetooth_avrcp_target_set_volume(hal_bluetooth_avrcp_target_t target,
                                      uint8_t absolute_volume);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_AVRCP_TARGET */
