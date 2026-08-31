#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_BLUETOOTH_GAMEPAD

/**
 * @file hal_gamepad.h
 * @brief Bluetooth Classic HID gamepad API.
 *
 * The API exposes normalized input snapshots without leaking Bluetooth stack
 * types. Operations are nonblocking; call hal_gamepad_poll() regularly.
 */

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  /** HID buttons are stored in one 32-bit mask. */
  HAL_GAMEPAD_BUTTON_COUNT = 32u,
  /** Generic Desktop axes represented by hal_gamepad_axis_t. */
  HAL_GAMEPAD_AXIS_COUNT = 9u,
  /** Number of copied state changes retained before overflow. */
  HAL_GAMEPAD_SNAPSHOT_QUEUE_DEPTH = 16u,
};

typedef enum {
  HAL_GAMEPAD_AXIS_X = 0,
  HAL_GAMEPAD_AXIS_Y,
  HAL_GAMEPAD_AXIS_Z,
  HAL_GAMEPAD_AXIS_RX,
  HAL_GAMEPAD_AXIS_RY,
  HAL_GAMEPAD_AXIS_RZ,
  HAL_GAMEPAD_AXIS_SLIDER,
  HAL_GAMEPAD_AXIS_DIAL,
  HAL_GAMEPAD_AXIS_WHEEL,
} hal_gamepad_axis_t;

typedef enum {
  HAL_GAMEPAD_DPAD_NONE = 0u,
  HAL_GAMEPAD_DPAD_UP = (1u << 0),
  HAL_GAMEPAD_DPAD_RIGHT = (1u << 1),
  HAL_GAMEPAD_DPAD_DOWN = (1u << 2),
  HAL_GAMEPAD_DPAD_LEFT = (1u << 3),
} hal_gamepad_dpad_t;

typedef enum {
  HAL_GAMEPAD_STATE_UNINITIALIZED = 0,
  HAL_GAMEPAD_STATE_STARTING,
  HAL_GAMEPAD_STATE_READY,
  HAL_GAMEPAD_STATE_DISCOVERING,
  HAL_GAMEPAD_STATE_CONNECTING,
  HAL_GAMEPAD_STATE_CONNECTED,
  HAL_GAMEPAD_STATE_FAILED,
} hal_gamepad_state_t;

typedef struct {
  /** Changes whenever a new HID connection is established. */
  uint32_t generation;
  /** Bit 0 is HID Button 1, through bit 31 for HID Button 32. */
  uint32_t buttons;
  /** Present axes normalized to the inclusive range -32767..32767. */
  int16_t axes[HAL_GAMEPAD_AXIS_COUNT];
  /** Bit mask selecting valid entries in axes. */
  uint16_t axes_present;
  /** Bit mask composed from hal_gamepad_dpad_t directions. */
  uint8_t dpad;
  /** True while the snapshot belongs to an active HID link. */
  bool connected;
} hal_gamepad_snapshot_t;

typedef struct {
  hal_gamepad_state_t state;
  hal_status_t last_status;
  uint32_t generation;
  uint32_t dropped_snapshots;
  size_t pending_snapshots;
  bool pairing_window_open;
  bool pairing_pending;
  bool known_device;
} hal_gamepad_info_t;

/** @brief Opaque handle for the single Bluetooth gamepad profile. */
typedef struct hal_gamepad_impl_s hal_gamepad_impl_t;
typedef hal_gamepad_impl_t *hal_gamepad_t;

/** Initialize the Bluetooth gamepad profile and return its handle. */
hal_status_t hal_gamepad_open(hal_gamepad_t *out_gamepad);

/** Stop the profile and invalidate the handle. Passing NULL is invalid. */
hal_status_t hal_gamepad_close(hal_gamepad_t gamepad);

/** Service Bluetooth transport and profile state without blocking. */
hal_status_t hal_gamepad_poll(hal_gamepad_t gamepad);

/** Read profile state and bounded-queue diagnostics. */
hal_status_t hal_gamepad_get_info(hal_gamepad_t gamepad,
                                  hal_gamepad_info_t *out_info);

/** Read the latest normalized state without consuming the queue. */
hal_status_t hal_gamepad_snapshot(hal_gamepad_t gamepad,
                                  hal_gamepad_snapshot_t *out_snapshot);

/**
 * Pop one normalized state change. HAL_EOVERFLOW acknowledges lost snapshots;
 * call again to receive the newest retained sequence.
 */
hal_status_t hal_gamepad_snapshot_next(hal_gamepad_t gamepad,
                                       hal_gamepad_snapshot_t *out_snapshot);

/** Open the bounded discovery and pairing window. */
hal_status_t hal_gamepad_pairing_open(hal_gamepad_t gamepad);

/** Authorize a pending Just Works or legacy PIN 0000 request. */
hal_status_t hal_gamepad_pairing_authorize(hal_gamepad_t gamepad);

/** Reconnect the previously paired gamepad. */
hal_status_t hal_gamepad_reconnect(hal_gamepad_t gamepad);

/** Disconnect the active gamepad link. */
hal_status_t hal_gamepad_disconnect(hal_gamepad_t gamepad);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_GAMEPAD */
