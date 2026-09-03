#pragma once

#include "hal/bluetooth/hal_gamepad.h"
#include "hal/bluetooth/jh_bluetooth_classic_bond_codec.h"
#include "hal/core/hal_status.h"

#include <stdint.h>

#ifndef HAL_ENABLE_BLUETOOTH_GAMEPAD
enum {
  HAL_GAMEPAD_BOND_BLOB_SIZE = HAL_BLUETOOTH_CLASSIC_BOND_BLOB_SIZE,
};
typedef hal_bluetooth_classic_bond_blob_t hal_gamepad_bond_blob_t;
typedef hal_status_t (*hal_gamepad_bond_load_fn)(
    void *context, hal_gamepad_bond_blob_t *out_blob);
typedef hal_status_t (*hal_gamepad_bond_store_fn)(
    void *context, const hal_gamepad_bond_blob_t *blob);
typedef hal_status_t (*hal_gamepad_bond_erase_fn)(void *context);
typedef struct {
  void *context;
  hal_gamepad_bond_load_fn load;
  hal_gamepad_bond_store_fn store;
  hal_gamepad_bond_erase_fn erase;
} hal_gamepad_bond_provider_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
  JH_GAMEPAD_BOND_ADDR_LEN = HAL_BLUETOOTH_CLASSIC_ADDRESS_LEN,
  JH_GAMEPAD_BOND_LINK_KEY_LEN = JH_BLUETOOTH_CLASSIC_LINK_KEY_LEN,
};

/** @brief Decoded bonded-peer identity: BD_ADDR + BR/EDR link key + type. */
typedef struct {
  uint8_t bd_addr[JH_GAMEPAD_BOND_ADDR_LEN];
  uint8_t link_key[JH_GAMEPAD_BOND_LINK_KEY_LEN];
  uint8_t link_key_type;
} jh_gamepad_bond_identity_t;

/**
 * @brief Encode a bonded peer's identity into the opaque, versioned,
 * CRC-protected blob a provider persists (see hal_gamepad_bond_blob_t).
 *
 * @param identity Peer identity to encode.
 * @param sequence Caller-owned sequence number (e.g. incremented on every
 *                 new bond); round-trips through decode for diagnostics.
 * @param out_blob Destination blob.
 * @return HAL_OK on success, HAL_EINVAL for a NULL argument.
 */
hal_status_t jh_gamepad_bond_encode(const jh_gamepad_bond_identity_t *identity,
                                    uint32_t sequence,
                                    hal_gamepad_bond_blob_t *out_blob);

/**
 * @brief Decode and validate a persisted bond blob.
 *
 * Checks the magic, format version, self CRC, and the peer-verification
 * rules id (JH_BLUETOOTH_GAMEPAD_BOND_RULES_ID) baked into this build.
 *
 * @param blob         Blob previously produced by jh_gamepad_bond_encode()
 *                     (or loaded from a provider).
 * @param out_identity Destination identity, filled only on HAL_OK.
 * @param out_sequence Destination sequence number, filled only on HAL_OK.
 *                     May be NULL if not needed.
 * @return HAL_OK on a structurally valid blob written under the current
 *         peer-verification rules, HAL_EINVAL for a NULL required argument,
 *         or HAL_EPROTO for a bad magic, version, CRC, or a rules id from a
 *         previous/incompatible verification policy -- callers must treat
 *         HAL_EPROTO the same as "no bond": discard and require a fresh
 *         pairing.
 */
hal_status_t jh_gamepad_bond_decode(const hal_gamepad_bond_blob_t *blob,
                                    jh_gamepad_bond_identity_t *out_identity,
                                    uint32_t *out_sequence);

#ifdef __cplusplus
}
#endif
