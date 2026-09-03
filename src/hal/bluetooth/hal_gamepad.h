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

#include "hal/bluetooth/hal_bluetooth_classic.h"
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

enum {
  /** Fixed size of the opaque, versioned and CRC-protected bond blob a
   *  provider persists and returns as-is; it never needs to interpret the
   *  contents. */
  HAL_GAMEPAD_BOND_BLOB_SIZE = HAL_BLUETOOTH_CLASSIC_BOND_BLOB_SIZE,
};

/** @brief Compatibility alias for the Classic manager's opaque bond record.
 *  A provider stores and loads these bytes verbatim; the shared Classic
 *  manager owns encoding, validation and the only persisted link-key copy. */
typedef hal_bluetooth_classic_bond_blob_t hal_gamepad_bond_blob_t;

/**
 * @brief Load the persisted bond blob, if any.
 * @return HAL_OK with *out_blob filled when a bond is stored, HAL_ENOENT
 *         when none is stored yet, or an I/O status on failure.
 */
typedef hal_status_t (*hal_gamepad_bond_load_fn)(
    void *context, hal_gamepad_bond_blob_t *out_blob);

/** @brief Persist the given bond blob, replacing any previous one. */
typedef hal_status_t (*hal_gamepad_bond_store_fn)(
    void *context, const hal_gamepad_bond_blob_t *blob);

/** @brief Remove any persisted bond blob (factory reset / replace pad). */
typedef hal_status_t (*hal_gamepad_bond_erase_fn)(void *context);

/**
 * @brief Optional persistence hooks for the gamepad's bonded-peer identity.
 *
 * This is the legacy one-slot adapter for the indexed Classic bond provider.
 * The gamepad adapter decides when a peer has passed its profile checks; the
 * shared Classic manager owns record encoding, link keys and persistence
 * timing. Pass a provider backed by hal_kv, an external EEPROM, or another
 * persistent medium. Passing NULL to hal_gamepad_open_ex() (or calling
 * hal_gamepad_open()) keeps bonding RAM-only for the current runtime.
 *
 * store() and erase() are called only from hal_gamepad_poll(), after the
 * backend has returned from any Bluetooth stack callback and released the
 * radio lock -- never from inside a stack callback or while a lock is held.
 * All three functions must tolerate being called from application/task
 * context only (never from an ISR).
 */
typedef struct {
  void *context;
  hal_gamepad_bond_load_fn load;
  hal_gamepad_bond_store_fn store;
  hal_gamepad_bond_erase_fn erase;
} hal_gamepad_bond_provider_t;

/** Initialize the Bluetooth gamepad profile and return its handle.
 *  Equivalent to hal_gamepad_open_ex(out_gamepad, NULL). */
hal_status_t hal_gamepad_open(hal_gamepad_t *out_gamepad);

/**
 * Initialize the Bluetooth gamepad profile with an optional bond provider.
 *
 * @param bond_provider NULL for RAM-only bonding (cleared on close/restart),
 *        or a provider to persist the bonded peer across reboots. When
 *        given, its load() is consulted once during open() to restore a
 *        previously bonded peer's link key into the controller.
 */
hal_status_t
hal_gamepad_open_ex(hal_gamepad_t *out_gamepad,
                    const hal_gamepad_bond_provider_t *bond_provider);

/** Stop the profile, clear its selected device, and invalidate the handle. */
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

/** Open the bounded pairing window, including to replace a known device. */
hal_status_t hal_gamepad_pairing_open(hal_gamepad_t gamepad);

/** Authorize a pending Just Works or legacy PIN 0000 request. */
hal_status_t hal_gamepad_pairing_authorize(hal_gamepad_t gamepad);

/** Reconnect the previously paired gamepad. */
hal_status_t hal_gamepad_reconnect(hal_gamepad_t gamepad);

/** Request asynchronous disconnection of the active gamepad link. */
hal_status_t hal_gamepad_disconnect(hal_gamepad_t gamepad);

/**
 * @brief Factory reset: forget the bonded peer.
 *
 * Disconnects any active link, clears the known peer from the controller's
 * link key database and from RAM, and -- when a bond provider was passed to
 * hal_gamepad_open_ex() -- erases the persisted bond blob. A subsequent
 * hal_gamepad_pairing_open() starts a fresh pairing; reconnect is refused
 * until a new peer is bonded.
 *
 * @return HAL_OK on success, or the provider's erase() status on failure
 *         (RAM/controller state is still cleared even when erase() fails).
 */
hal_status_t hal_gamepad_forget(hal_gamepad_t gamepad);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_GAMEPAD */
