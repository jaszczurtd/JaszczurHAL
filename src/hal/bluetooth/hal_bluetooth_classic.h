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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_BLUETOOTH_CLASSIC_ADDRESS_LEN 6u
#define HAL_BLUETOOTH_CLASSIC_ADDRESS_TEXT_SIZE 18u
#define HAL_BLUETOOTH_CLASSIC_NAME_MAX_LEN 63u
#define HAL_BLUETOOTH_CLASSIC_BOND_BLOB_SIZE 38u

#ifndef HAL_BLUETOOTH_CLASSIC_SCAN_QUEUE_DEPTH
#define HAL_BLUETOOTH_CLASSIC_SCAN_QUEUE_DEPTH 8u
#endif

#ifndef HAL_BLUETOOTH_CLASSIC_MAX_PEERS
#define HAL_BLUETOOTH_CLASSIC_MAX_PEERS 4u
#endif

#if HAL_BLUETOOTH_CLASSIC_SCAN_QUEUE_DEPTH < 2u
#error "HAL_BLUETOOTH_CLASSIC_SCAN_QUEUE_DEPTH must be at least 2"
#endif

#if HAL_BLUETOOTH_CLASSIC_MAX_PEERS < 1u
#error "HAL_BLUETOOTH_CLASSIC_MAX_PEERS must be at least 1"
#endif

typedef struct {
  uint8_t bytes[HAL_BLUETOOTH_CLASSIC_ADDRESS_LEN];
} hal_bluetooth_classic_address_t;

typedef enum {
  HAL_BLUETOOTH_CLASSIC_STATE_UNINITIALIZED = 0,
  HAL_BLUETOOTH_CLASSIC_STATE_STARTING,
  HAL_BLUETOOTH_CLASSIC_STATE_READY,
  HAL_BLUETOOTH_CLASSIC_STATE_SCANNING,
  HAL_BLUETOOTH_CLASSIC_STATE_FAILED,
} hal_bluetooth_classic_state_t;

typedef enum {
  HAL_BLUETOOTH_CLASSIC_PAIRING_NONE = 0,
  HAL_BLUETOOTH_CLASSIC_PAIRING_JUST_WORKS,
  HAL_BLUETOOTH_CLASSIC_PAIRING_PIN,
  HAL_BLUETOOTH_CLASSIC_PAIRING_PASSKEY,
} hal_bluetooth_classic_pairing_method_t;

typedef enum {
  HAL_BLUETOOTH_CLASSIC_SERVICE_NONE = 0u,
  HAL_BLUETOOTH_CLASSIC_SERVICE_HID = (1u << 0),
  HAL_BLUETOOTH_CLASSIC_SERVICE_PNP = (1u << 1),
  HAL_BLUETOOTH_CLASSIC_SERVICE_SERIAL_PORT = (1u << 2),
  HAL_BLUETOOTH_CLASSIC_SERVICE_AUDIO_SOURCE = (1u << 3),
  HAL_BLUETOOTH_CLASSIC_SERVICE_AUDIO_SINK = (1u << 4),
} hal_bluetooth_classic_service_t;

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

/**
 * Opaque, versioned and CRC-protected bonded-peer record. Providers store
 * these bytes unchanged and never inspect link keys.
 */
typedef struct {
  uint8_t bytes[HAL_BLUETOOTH_CLASSIC_BOND_BLOB_SIZE];
} hal_bluetooth_classic_bond_blob_t;

/** Load a record slot, or return HAL_ENOENT when the slot is empty. */
typedef hal_status_t (*hal_bluetooth_classic_bond_load_fn)(
    void *context, size_t index, hal_bluetooth_classic_bond_blob_t *out_blob);

/** Store a complete record in the selected slot. */
typedef hal_status_t (*hal_bluetooth_classic_bond_store_fn)(
    void *context, size_t index, const hal_bluetooth_classic_bond_blob_t *blob);

/** Erase one record slot. An already empty slot should return HAL_OK. */
typedef hal_status_t (*hal_bluetooth_classic_bond_erase_fn)(void *context,
                                                            size_t index);

typedef struct {
  void *context;
  size_t capacity;
  hal_bluetooth_classic_bond_load_fn load;
  hal_bluetooth_classic_bond_store_fn store;
  hal_bluetooth_classic_bond_erase_fn erase;
} hal_bluetooth_classic_bond_provider_t;

typedef struct {
  hal_bluetooth_classic_address_t address;
  uint32_t sequence;
  uint16_t profile_id;
  size_t storage_index;
} hal_bluetooth_classic_peer_t;

typedef struct {
  hal_bluetooth_classic_state_t state;
  hal_status_t last_status;
  uint32_t generation;
  uint32_t dropped_scan_results;
  size_t pending_scan_results;
  size_t peer_count;
  bool scan_active;
  bool pairing_pending;
  hal_bluetooth_classic_pairing_method_t pairing_method;
  hal_bluetooth_classic_address_t pairing_address;
} hal_bluetooth_classic_info_t;

typedef struct hal_bluetooth_classic_impl_s hal_bluetooth_classic_impl_t;
typedef hal_bluetooth_classic_impl_t *hal_bluetooth_classic_t;

/** Open the Classic manager with RAM-only peer storage. */
hal_status_t hal_bluetooth_classic_open(hal_bluetooth_classic_t *out_classic);

/** Open the Classic manager and restore records from an optional provider. */
hal_status_t hal_bluetooth_classic_open_ex(
    hal_bluetooth_classic_t *out_classic,
    const hal_bluetooth_classic_bond_provider_t *bond_provider);

/** Close the manager. Active profile handles must be closed first. */
hal_status_t hal_bluetooth_classic_close(hal_bluetooth_classic_t classic);

/** Service the controller and commit pending peer records outside callbacks. */
hal_status_t hal_bluetooth_classic_poll(hal_bluetooth_classic_t classic);

/** Read manager state and bounded-queue diagnostics. */
hal_status_t
hal_bluetooth_classic_get_info(hal_bluetooth_classic_t classic,
                               hal_bluetooth_classic_info_t *out_info);

/** Start a bounded inquiry window. duration_ms must be in 1..120000. */
hal_status_t hal_bluetooth_classic_scan_start(hal_bluetooth_classic_t classic,
                                              uint32_t duration_ms);

/** Stop the active inquiry window. */
hal_status_t hal_bluetooth_classic_scan_stop(hal_bluetooth_classic_t classic);

/** Pop one copied inquiry/SDP result. */
hal_status_t hal_bluetooth_classic_scan_result_next(
    hal_bluetooth_classic_t classic,
    hal_bluetooth_classic_scan_result_t *out_result);

/** Resolve the supported SDP service classes for one discovered address. */
hal_status_t
hal_bluetooth_classic_sdp_query(hal_bluetooth_classic_t classic,
                                const hal_bluetooth_classic_address_t *address);

/** Start dedicated bonding with a discovered peer. */
hal_status_t
hal_bluetooth_classic_pair(hal_bluetooth_classic_t classic,
                           const hal_bluetooth_classic_address_t *address);

/** Approve the current pairing request. PIN pairing uses "0000". */
hal_status_t
hal_bluetooth_classic_pairing_authorize(hal_bluetooth_classic_t classic);

/** Reject the current pairing request. */
hal_status_t
hal_bluetooth_classic_pairing_reject(hal_bluetooth_classic_t classic);

/**
 * Save a paired peer after its profile has validated the connection.
 * profile_id identifies the profile's verification rules. The manager owns
 * the only persisted copy of the link key. Re-saving an already known peer
 * with the same profile_id is an idempotent no-op and does not require a new
 * pairing authorization.
 */
hal_status_t
hal_bluetooth_classic_peer_save(hal_bluetooth_classic_t classic,
                                const hal_bluetooth_classic_address_t *address,
                                uint16_t profile_id);

/** Return the number of restored or newly saved peers. */
hal_status_t hal_bluetooth_classic_peer_count(hal_bluetooth_classic_t classic,
                                              size_t *out_count);

/** Copy one peer by its dense runtime index. */
hal_status_t
hal_bluetooth_classic_peer_get(hal_bluetooth_classic_t classic, size_t index,
                               hal_bluetooth_classic_peer_t *out_peer);

/**
 * Forget one peer in RAM, the controller and persistent storage.
 *
 * Persistent storage is erased first. If that operation fails, the runtime
 * peer remains available so the caller can retry without allowing the bond to
 * reappear unexpectedly after a restart.
 */
hal_status_t hal_bluetooth_classic_peer_forget(
    hal_bluetooth_classic_t classic,
    const hal_bluetooth_classic_address_t *address);

/** Format an address as XX:XX:XX:XX:XX:XX including the trailing NUL. */
hal_status_t hal_bluetooth_classic_format_address(
    const hal_bluetooth_classic_address_t *address, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_CLASSIC */
