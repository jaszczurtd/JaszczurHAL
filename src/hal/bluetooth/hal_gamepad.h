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

/** Generic Desktop axis slots exposed in normalized snapshots. */
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

/** Direction bits used by hal_gamepad_snapshot_t::dpad. */
typedef enum {
  HAL_GAMEPAD_DPAD_NONE = 0u,
  HAL_GAMEPAD_DPAD_UP = (1u << 0),
  HAL_GAMEPAD_DPAD_RIGHT = (1u << 1),
  HAL_GAMEPAD_DPAD_DOWN = (1u << 2),
  HAL_GAMEPAD_DPAD_LEFT = (1u << 3),
} hal_gamepad_dpad_t;

/** Gamepad adapter lifecycle and connection state. */
typedef enum {
  HAL_GAMEPAD_STATE_UNINITIALIZED = 0,
  HAL_GAMEPAD_STATE_STARTING,
  HAL_GAMEPAD_STATE_READY,
  HAL_GAMEPAD_STATE_DISCOVERING,
  HAL_GAMEPAD_STATE_CONNECTING,
  HAL_GAMEPAD_STATE_CONNECTED,
  HAL_GAMEPAD_STATE_FAILED,
} hal_gamepad_state_t;

/** Latest normalized gamepad inputs or one queued state change. */
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

/** Adapter state, pairing state, and bounded-queue diagnostics. */
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

/** @brief Incomplete implementation type for the opaque gamepad handle. */
typedef struct hal_gamepad_impl_s hal_gamepad_impl_t;
/** @brief Opaque handle for the single Bluetooth gamepad profile. */
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
 * @param context Provider context; may be NULL when supported by the provider.
 * @param out_blob Receives the opaque record; must not be NULL.
 * @return HAL_OK with *out_blob filled when a bond is stored, HAL_ENOENT
 * when none is stored yet, or an I/O status on failure.
 */
typedef hal_status_t (*hal_gamepad_bond_load_fn)(
    void *context, hal_gamepad_bond_blob_t *out_blob);

/**
 * @brief Persist a complete bond blob, replacing any previous one.
 * @param context Provider context; may be NULL when supported by the provider.
 * @param blob Opaque record to store atomically; must not be NULL.
 * @return HAL_OK or an I/O status. Failure must preserve the previous record.
 */
typedef hal_status_t (*hal_gamepad_bond_store_fn)(
    void *context, const hal_gamepad_bond_blob_t *blob);

/**
 * @brief Remove any persisted bond blob.
 * @param context Provider context; may be NULL when supported by the provider.
 * @return HAL_OK, including when no record exists, or an I/O status.
 */
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

/**
 * @brief Open the Bluetooth gamepad profile with RAM-only bonding.
 * @param out_gamepad Receives the handle and is cleared on entry; must not be
 * NULL.
 * @return The same statuses as hal_gamepad_open_ex().
 */
hal_status_t hal_gamepad_open(hal_gamepad_t *out_gamepad);

/**
 * @brief Open the Bluetooth gamepad profile with optional persistent bonding.
 *
 * @param out_gamepad Receives the handle and is cleared on entry; must not be
 * NULL.
 * @param bond_provider NULL for RAM-only bonding (cleared on close/restart),
 * or a complete provider copied during open. Its load function is called once
 * to restore a previously bonded peer.
 * @return HAL_OK, HAL_EINVAL for invalid input, HAL_EBUSY when already open,
 * HAL_ENOMEM for runtime allocation failure, or a Classic/HID/provider startup
 * error.
 */
hal_status_t
hal_gamepad_open_ex(hal_gamepad_t *out_gamepad,
                    const hal_gamepad_bond_provider_t *bond_provider);

/**
 * @brief Stop the profile, clear its selected device, and invalidate its
 * handle.
 * @param gamepad Live gamepad handle.
 * @return HAL_OK, HAL_EUNINIT for an invalid or stale handle, HAL_EBUSY during
 * another operation, HAL_ENOMEM, or a Classic/HID shutdown error.
 */
hal_status_t hal_gamepad_close(hal_gamepad_t gamepad);

/**
 * @brief Service Bluetooth transport and profile state without blocking.
 * @param gamepad Live gamepad handle.
 * @return HAL_OK, HAL_EUNINIT for an invalid handle, HAL_EBUSY during another
 * operation, HAL_EAGAIN while awaiting input, HAL_EOVERFLOW after lost queued
 * state, HAL_ENOMEM, or a Classic/HID/parser error.
 */
hal_status_t hal_gamepad_poll(hal_gamepad_t gamepad);

/**
 * @brief Read profile state and bounded-queue diagnostics.
 * @param gamepad Live gamepad handle.
 * @param out_info Receives a snapshot; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EUNINIT for an invalid
 * handle, HAL_EBUSY during another operation, or HAL_ENOMEM.
 */
hal_status_t hal_gamepad_get_info(hal_gamepad_t gamepad,
                                  hal_gamepad_info_t *out_info);

/**
 * @brief Read the latest normalized state without consuming the queue.
 * @param gamepad Live gamepad handle.
 * @param out_snapshot Receives the latest state; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EUNINIT for an invalid
 * handle, HAL_EAGAIN before a state is available, HAL_EBUSY, or HAL_ENOMEM.
 */
hal_status_t hal_gamepad_snapshot(hal_gamepad_t gamepad,
                                  hal_gamepad_snapshot_t *out_snapshot);

/**
 * @brief Pop one normalized state change.
 *
 * HAL_EOVERFLOW acknowledges lost snapshots;
 * call again to receive the newest retained sequence.
 *
 * @param gamepad Live gamepad handle.
 * @param out_snapshot Receives the oldest retained state; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EUNINIT for an invalid
 * handle, HAL_EAGAIN when empty, HAL_EOVERFLOW after lost snapshots,
 * HAL_EBUSY, or HAL_ENOMEM.
 */
hal_status_t hal_gamepad_snapshot_next(hal_gamepad_t gamepad,
                                       hal_gamepad_snapshot_t *out_snapshot);

/**
 * @brief Open the two-minute pairing window, including device replacement.
 * @param gamepad Live gamepad handle.
 * @return HAL_OK, HAL_EUNINIT for an invalid handle, HAL_ESTATE unless the
 * adapter is ready and idle, HAL_EBUSY during another operation, HAL_ENOMEM,
 * or a Classic scan error.
 */
hal_status_t hal_gamepad_pairing_open(hal_gamepad_t gamepad);

/**
 * @brief Authorize a pending Just Works or legacy PIN 0000 request.
 * @param gamepad Live gamepad handle.
 * @return HAL_OK, HAL_EUNINIT for an invalid handle, HAL_ESTATE without a
 * pending request, HAL_EBUSY during another operation, HAL_ENOMEM, or a
 * Classic pairing error.
 */
hal_status_t hal_gamepad_pairing_authorize(hal_gamepad_t gamepad);

/**
 * @brief Start reconnection to the previously paired gamepad.
 * @param gamepad Live gamepad handle.
 * @return HAL_OK when queued, HAL_EUNINIT for an invalid handle, HAL_ESTATE
 * without a saved peer or unless HID is ready, HAL_EBUSY during another
 * operation, HAL_ENOMEM, or a HID connection error.
 */
hal_status_t hal_gamepad_reconnect(hal_gamepad_t gamepad);

/**
 * @brief Request asynchronous disconnection of the active gamepad link.
 * @param gamepad Live gamepad handle.
 * @return HAL_OK when queued, HAL_EUNINIT for an invalid handle, HAL_ESTATE
 * without an active link, HAL_EBUSY during another operation, HAL_ENOMEM, or a
 * HID disconnection error.
 */
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
 * @param gamepad Live gamepad handle.
 * @return HAL_OK on success, HAL_EBUSY when another gamepad or Classic
 * operation is active, HAL_EUNINIT for an invalid handle, HAL_ENOMEM, or the
 * provider's erase status. HAL_EBUSY can be retried. A provider failure keeps
 * the known peer available for a later retry; the active HID session and input
 * state are still cleared.
 */
hal_status_t hal_gamepad_forget(hal_gamepad_t gamepad);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_GAMEPAD */
