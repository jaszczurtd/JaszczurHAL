#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_BLUETOOTH_CLASSIC

/**
 * @file hal_bluetooth_classic.h
 * @brief Bluetooth Classic discovery, pairing and bonded-peer management.
 *
 * The API copies all data owned by the Bluetooth stack into bounded HAL
 * structures. Operations are nonblocking; call hal_bluetooth_classic_poll()
 * regularly.
 */

#include "hal/core/hal_status.h"
#include "hal/core/hal_text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Size of a Bluetooth device address in bytes. */
#define HAL_BLUETOOTH_CLASSIC_ADDRESS_LEN 6u
/** Buffer size required for a formatted Bluetooth address and terminator. */
#define HAL_BLUETOOTH_CLASSIC_ADDRESS_TEXT_SIZE HAL_TEXT_MAC_STRING_SIZE
/** Maximum local or remote friendly-name length, excluding the terminator. */
#define HAL_BLUETOOTH_CLASSIC_NAME_MAX_LEN 63u
/** Size of the opaque serialized bond record in bytes. */
#define HAL_BLUETOOTH_CLASSIC_BOND_BLOB_SIZE 38u

#ifndef HAL_BLUETOOTH_CLASSIC_SCAN_QUEUE_DEPTH
/** Number of copied inquiry results retained by the manager. */
#define HAL_BLUETOOTH_CLASSIC_SCAN_QUEUE_DEPTH 8u
#endif

#ifndef HAL_BLUETOOTH_CLASSIC_MAX_PEERS
/** Maximum number of bonded peers retained by one manager. */
#define HAL_BLUETOOTH_CLASSIC_MAX_PEERS 4u
#endif

#if HAL_BLUETOOTH_CLASSIC_SCAN_QUEUE_DEPTH < 2u
#error "HAL_BLUETOOTH_CLASSIC_SCAN_QUEUE_DEPTH must be at least 2"
#endif

#if HAL_BLUETOOTH_CLASSIC_MAX_PEERS < 1u
#error "HAL_BLUETOOTH_CLASSIC_MAX_PEERS must be at least 1"
#endif

/** Bluetooth device address in stack-independent byte order. */
typedef struct {
  uint8_t bytes[HAL_BLUETOOTH_CLASSIC_ADDRESS_LEN];
} hal_bluetooth_classic_address_t;

/** Bluetooth Classic manager lifecycle state. */
typedef enum {
  HAL_BLUETOOTH_CLASSIC_STATE_UNINITIALIZED = 0,
  HAL_BLUETOOTH_CLASSIC_STATE_STARTING,
  HAL_BLUETOOTH_CLASSIC_STATE_READY,
  HAL_BLUETOOTH_CLASSIC_STATE_SCANNING,
  HAL_BLUETOOTH_CLASSIC_STATE_FAILED,
} hal_bluetooth_classic_state_t;

/** Authentication method requested for the pending pairing operation. */
typedef enum {
  HAL_BLUETOOTH_CLASSIC_PAIRING_NONE = 0,
  HAL_BLUETOOTH_CLASSIC_PAIRING_JUST_WORKS,
  HAL_BLUETOOTH_CLASSIC_PAIRING_PIN,
  HAL_BLUETOOTH_CLASSIC_PAIRING_PASSKEY,
} hal_bluetooth_classic_pairing_method_t;

/** Bit flags for service classes resolved through SDP. */
typedef enum {
  HAL_BLUETOOTH_CLASSIC_SERVICE_NONE = 0u,
  HAL_BLUETOOTH_CLASSIC_SERVICE_HID = (1u << 0),
  HAL_BLUETOOTH_CLASSIC_SERVICE_PNP = (1u << 1),
  HAL_BLUETOOTH_CLASSIC_SERVICE_SERIAL_PORT = (1u << 2),
  HAL_BLUETOOTH_CLASSIC_SERVICE_AUDIO_SOURCE = (1u << 3),
  HAL_BLUETOOTH_CLASSIC_SERVICE_AUDIO_SINK = (1u << 4),
} hal_bluetooth_classic_service_t;

/** Copied inquiry result with optional name, RSSI, and resolved services. */
typedef struct {
  hal_bluetooth_classic_address_t address;
  char name[HAL_BLUETOOTH_CLASSIC_NAME_MAX_LEN + 1u];
  uint32_t class_of_device;
  uint32_t services;
  int8_t rssi;
  uint8_t name_length;
  bool rssi_valid;
  bool services_resolved;
} hal_bluetooth_classic_scan_result_t;

/** Local Bluetooth Classic name and Class of Device copied by the manager. */
typedef struct {
  char name[HAL_BLUETOOTH_CLASSIC_NAME_MAX_LEN + 1u];
  /** 24-bit Bluetooth Class of Device value. */
  uint32_t class_of_device;
} hal_bluetooth_classic_identity_t;

/**
 * @brief Opaque, versioned and CRC-protected bonded-peer record.
 *
 * Providers store these bytes unchanged and never inspect link keys.
 */
typedef struct {
  uint8_t bytes[HAL_BLUETOOTH_CLASSIC_BOND_BLOB_SIZE];
} hal_bluetooth_classic_bond_blob_t;

/**
 * @brief Load one bonded-peer record.
 * @param context Provider context; may be NULL when supported by the provider.
 * @param index Slot index smaller than the provider capacity.
 * @param out_blob Receives the opaque record; must not be NULL.
 * @return HAL_OK, HAL_ENOENT for an empty slot, or a provider error.
 */
typedef hal_status_t (*hal_bluetooth_classic_bond_load_fn)(
    void *context, size_t index, hal_bluetooth_classic_bond_blob_t *out_blob);

/**
 * @brief Store one complete bonded-peer record.
 * @param context Provider context; may be NULL when supported by the provider.
 * @param index Slot index smaller than the provider capacity.
 * @param blob Opaque record to store atomically; must not be NULL.
 * @return HAL_OK or a provider error. Failure must preserve the previously
 * published record.
 */
typedef hal_status_t (*hal_bluetooth_classic_bond_store_fn)(
    void *context, size_t index, const hal_bluetooth_classic_bond_blob_t *blob);

/**
 * @brief Erase one bonded-peer record.
 * @param context Provider context; may be NULL when supported by the provider.
 * @param index Slot index smaller than the provider capacity.
 * @return HAL_OK, including for an empty slot, or a provider error.
 */
typedef hal_status_t (*hal_bluetooth_classic_bond_erase_fn)(void *context,
                                                            size_t index);

/** Persistent-storage adapter for indexed bonded-peer records. */
typedef struct {
  void *context;
  size_t capacity;
  hal_bluetooth_classic_bond_load_fn load;
  hal_bluetooth_classic_bond_store_fn store;
  hal_bluetooth_classic_bond_erase_fn erase;
} hal_bluetooth_classic_bond_provider_t;

/** Public metadata for one saved peer; never contains link-key material. */
typedef struct {
  hal_bluetooth_classic_address_t address;
  uint32_t sequence;
  uint16_t profile_id;
  size_t storage_index;
} hal_bluetooth_classic_peer_t;

/** Manager state, pairing state, and bounded-queue diagnostics. */
typedef struct {
  hal_bluetooth_classic_state_t state;
  hal_status_t last_status;
  uint32_t generation;
  uint32_t dropped_scan_results;
  size_t pending_scan_results;
  size_t peer_count;
  bool scan_active;
  bool pairing_pending;
  bool pairing_window_open;
  hal_bluetooth_classic_pairing_method_t pairing_method;
  hal_bluetooth_classic_address_t pairing_address;
} hal_bluetooth_classic_info_t;

/** Incomplete implementation type for the opaque manager handle. */
typedef struct hal_bluetooth_classic_impl_s hal_bluetooth_classic_impl_t;
/** Opaque handle for the process-wide Bluetooth Classic manager. */
typedef hal_bluetooth_classic_impl_t *hal_bluetooth_classic_t;

/**
 * @brief Open the Classic manager with RAM-only peer storage.
 * @param out_classic Receives the manager handle and is cleared on entry; must
 * not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EBUSY when already open,
 * HAL_ENOMEM for runtime allocation failure, HAL_ECONFIG for an incomplete
 * backend, HAL_EUNSUPPORTED without Classic hardware, or a startup error.
 */
hal_status_t hal_bluetooth_classic_open(hal_bluetooth_classic_t *out_classic);

/**
 * @brief Open the Classic manager and restore records from a provider.
 * @param out_classic Receives the manager handle and is cleared on entry; must
 * not be NULL.
 * @param bond_provider Optional provider copied by the manager; NULL selects
 * RAM-only storage. Capacity must be within HAL_BLUETOOTH_CLASSIC_MAX_PEERS
 * and all callbacks must be present.
 * @return The same statuses as hal_bluetooth_classic_open(), HAL_EINVAL for an
 * invalid provider, or an error returned while loading/restoring records.
 */
hal_status_t hal_bluetooth_classic_open_ex(
    hal_bluetooth_classic_t *out_classic,
    const hal_bluetooth_classic_bond_provider_t *bond_provider);

/**
 * @brief Close the manager and erase in-memory link-key copies.
 * @param classic Live manager handle.
 * @return HAL_OK, HAL_EUNINIT for an invalid or stale handle, HAL_EBUSY while
 * a profile remains attached, HAL_ENOMEM when the runtime lock cannot be
 * created, or a backend shutdown error.
 */
hal_status_t hal_bluetooth_classic_close(hal_bluetooth_classic_t classic);

/**
 * @brief Service the controller and commit pending peers outside callbacks.
 * @param classic Live manager handle.
 * @return HAL_OK, HAL_EUNINIT for an invalid handle, HAL_EBUSY during another
 * operation, HAL_EOVERFLOW when no peer slot is available, a provider error,
 * or a backend service/restore error.
 */
hal_status_t hal_bluetooth_classic_poll(hal_bluetooth_classic_t classic);

/**
 * @brief Configure the shared local name and Class of Device.
 * @param classic Live manager handle.
 * @param identity Identity copied before the backend call; must not be NULL.
 * The name must be nonempty and terminated within
 * HAL_BLUETOOTH_CLASSIC_NAME_MAX_LEN bytes. Only the low 24 bits of
 * class_of_device may be set.
 * @return HAL_OK, HAL_EINVAL for invalid identity, HAL_EUNINIT for an invalid
 * handle, HAL_EBUSY during another operation, HAL_ENOMEM, or a backend error.
 */
hal_status_t hal_bluetooth_classic_set_identity(
    hal_bluetooth_classic_t classic,
    const hal_bluetooth_classic_identity_t *identity);

/**
 * @brief Open a bounded incoming pairing and inquiry window.
 *
 * The radio remains connectable after the window so restored peers can
 * reconnect, but it stops being discoverable and rejects pairing by unknown
 * peers when the deadline expires.
 *
 * @param classic Live manager handle.
 * @param duration_ms Window duration from 1000 through 300000 milliseconds.
 * @return HAL_OK, HAL_EINVAL for an invalid duration, HAL_EUNINIT for an
 * invalid handle, HAL_ESTATE until the manager is ready, HAL_EBUSY when a
 * window is already open or during another operation, HAL_ENOMEM, or a
 * backend error.
 */
hal_status_t
hal_bluetooth_classic_pairing_window_open(hal_bluetooth_classic_t classic,
                                          uint32_t duration_ms);

/**
 * @brief Close the pairing window while retaining known-peer reconnects.
 * @param classic Live manager handle.
 * @return HAL_OK, HAL_EUNINIT for an invalid handle, HAL_ESTATE when no window
 * is open, HAL_EBUSY during another operation, HAL_ENOMEM, or a backend error.
 */
hal_status_t
hal_bluetooth_classic_pairing_window_close(hal_bluetooth_classic_t classic);

/**
 * @brief Read manager state and bounded-queue diagnostics.
 * @param classic Live manager handle.
 * @param out_info Receives the snapshot; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EUNINIT for an invalid
 * handle, HAL_EBUSY during another operation, or HAL_ENOMEM.
 */
hal_status_t
hal_bluetooth_classic_get_info(hal_bluetooth_classic_t classic,
                               hal_bluetooth_classic_info_t *out_info);

/**
 * @brief Start a bounded inquiry window.
 * @param classic Live manager handle.
 * @param duration_ms Inquiry duration from 1 through 120000 milliseconds.
 * @return HAL_OK when queued; HAL_EINVAL for invalid duration, HAL_EUNINIT for
 * an invalid handle, HAL_EBUSY during another operation, HAL_ESTATE unless the
 * manager is ready and idle, or a backend error.
 */
hal_status_t hal_bluetooth_classic_scan_start(hal_bluetooth_classic_t classic,
                                              uint32_t duration_ms);

/**
 * @brief Stop the active inquiry window.
 * @param classic Live manager handle.
 * @return HAL_OK when queued; HAL_EUNINIT for an invalid handle, HAL_EBUSY
 * during another operation, HAL_ESTATE when no scan is active, or a backend
 * error.
 */
hal_status_t hal_bluetooth_classic_scan_stop(hal_bluetooth_classic_t classic);

/**
 * @brief Pop one copied inquiry/SDP result.
 * @param classic Live manager handle.
 * @param out_result Receives the oldest retained result; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EUNINIT for an invalid
 * handle, HAL_EAGAIN when empty, HAL_EOVERFLOW once after dropped results, or
 * HAL_ENOMEM.
 */
hal_status_t hal_bluetooth_classic_scan_result_next(
    hal_bluetooth_classic_t classic,
    hal_bluetooth_classic_scan_result_t *out_result);

/**
 * @brief Resolve supported SDP service classes for a discovered address.
 * @param classic Live manager handle.
 * @param address Nonzero peer address; must not be NULL.
 * @return HAL_OK when queued; HAL_EINVAL for invalid input, HAL_EUNINIT for an
 * invalid handle, HAL_EBUSY during another operation, or a backend error.
 */
hal_status_t
hal_bluetooth_classic_sdp_query(hal_bluetooth_classic_t classic,
                                const hal_bluetooth_classic_address_t *address);

/**
 * @brief Start dedicated bonding with a discovered peer.
 * @param classic Live manager handle.
 * @param address Nonzero peer address; must not be NULL.
 * @return HAL_OK when queued; HAL_EINVAL for invalid input, HAL_EUNINIT for an
 * invalid handle, HAL_EBUSY during another operation, or a backend error.
 */
hal_status_t
hal_bluetooth_classic_pair(hal_bluetooth_classic_t classic,
                           const hal_bluetooth_classic_address_t *address);

/**
 * @brief Approve the current pairing request; PIN pairing uses `0000`.
 * @param classic Live manager handle.
 * @return HAL_OK when queued, HAL_EUNINIT for an invalid handle, HAL_ESTATE
 * without a pending request, HAL_EBUSY during another operation, HAL_ENOMEM,
 * or a backend error.
 */
hal_status_t
hal_bluetooth_classic_pairing_authorize(hal_bluetooth_classic_t classic);

/**
 * @brief Reject the current pairing request.
 * @param classic Live manager handle.
 * @return HAL_OK when queued, HAL_EUNINIT for an invalid handle, HAL_ESTATE
 * without a pending request, HAL_EBUSY during another operation, HAL_ENOMEM,
 * or a backend error.
 */
hal_status_t
hal_bluetooth_classic_pairing_reject(hal_bluetooth_classic_t classic);

/**
 * @brief Save a paired peer after its profile has validated the connection.
 *
 * profile_id identifies the profile's verification rules. The manager owns
 * the only persisted copy of the link key. Re-saving an already known peer
 * with the same profile_id is an idempotent no-op and does not require a new
 * pairing authorization.
 *
 * @param classic Live manager handle.
 * @param address Nonzero authenticated peer address; must not be NULL.
 * @param profile_id Nonzero identifier owned by the validating profile.
 * @return HAL_OK when staged or already saved; HAL_EINVAL for invalid input,
 * HAL_EUNINIT for an invalid handle, HAL_EBUSY during another operation,
 * HAL_EAUTH without local authorization, or HAL_ENOMEM.
 */
hal_status_t
hal_bluetooth_classic_peer_save(hal_bluetooth_classic_t classic,
                                const hal_bluetooth_classic_address_t *address,
                                uint16_t profile_id);

/**
 * @brief Return the number of restored or newly saved peers.
 * @param classic Live manager handle.
 * @param out_count Receives a value up to HAL_BLUETOOTH_CLASSIC_MAX_PEERS;
 * must not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EUNINIT for an invalid
 * handle, or HAL_ENOMEM.
 */
hal_status_t hal_bluetooth_classic_peer_count(hal_bluetooth_classic_t classic,
                                              size_t *out_count);

/**
 * @brief Copy one peer by its dense runtime index.
 * @param classic Live manager handle.
 * @param index Dense index smaller than the value returned by
 * hal_bluetooth_classic_peer_count().
 * @param out_peer Receives public peer metadata without the link key; must not
 * be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_ENOENT for an out-of-range
 * index, HAL_EUNINIT for an invalid handle, or HAL_ENOMEM.
 */
hal_status_t
hal_bluetooth_classic_peer_get(hal_bluetooth_classic_t classic, size_t index,
                               hal_bluetooth_classic_peer_t *out_peer);

/**
 * @brief Forget one peer in RAM, the controller and persistent storage.
 *
 * Persistent storage is erased first. If that operation fails, the runtime
 * peer remains available so the caller can retry without allowing the bond to
 * reappear unexpectedly after a restart.
 *
 * @param classic Live manager handle.
 * @param address Nonzero saved peer address; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid input, HAL_ENOENT for an unknown
 * peer, HAL_EUNINIT for an invalid handle, HAL_EBUSY during another operation,
 * HAL_ENOMEM, or a provider/backend erase error.
 */
hal_status_t hal_bluetooth_classic_peer_forget(
    hal_bluetooth_classic_t classic,
    const hal_bluetooth_classic_address_t *address);

/**
 * @brief Forget every peer and close the current pairing window.
 *
 * Erasure stops on the first provider/backend error. Records already erased
 * stay erased and the remaining records can be retried. A new pairing window
 * is never opened automatically.
 *
 * @param classic Live manager handle.
 * @return HAL_OK when all records are gone, HAL_EUNINIT for an invalid handle,
 * HAL_EBUSY during another operation, HAL_ENOMEM, or the first erase/backend
 * error.
 */
hal_status_t
hal_bluetooth_classic_peer_forget_all(hal_bluetooth_classic_t classic);

/**
 * @brief Format an address as `XX:XX:XX:XX:XX:XX`.
 * @param address Address to format; must not be NULL.
 * @param out Destination buffer; must not be NULL.
 * @param out_size Capacity including the terminator; at least
 * HAL_BLUETOOTH_CLASSIC_ADDRESS_TEXT_SIZE bytes.
 * @return HAL_OK, HAL_EINVAL for invalid input, or a formatting error.
 */
hal_status_t hal_bluetooth_classic_format_address(
    const hal_bluetooth_classic_address_t *address, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_CLASSIC */
