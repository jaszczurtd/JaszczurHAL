#pragma once

#include "hal/bluetooth/hal_bluetooth_classic.h"
#include "hal/core/hal_status.h"

#include <stdint.h>

/* The private C4-C6 hardware probe intentionally builds without the public
 * Classic feature. Keep its codec on the same wire format without forcing the
 * public manager into that isolated fixture. */
#ifndef HAL_ENABLE_BLUETOOTH_CLASSIC
#define HAL_BLUETOOTH_CLASSIC_ADDRESS_LEN 6u
#define HAL_BLUETOOTH_CLASSIC_BOND_BLOB_SIZE 38u
typedef struct {
  uint8_t bytes[HAL_BLUETOOTH_CLASSIC_ADDRESS_LEN];
} hal_bluetooth_classic_address_t;
typedef struct {
  uint8_t bytes[HAL_BLUETOOTH_CLASSIC_BOND_BLOB_SIZE];
} hal_bluetooth_classic_bond_blob_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
  JH_BLUETOOTH_CLASSIC_LINK_KEY_LEN = 16u,
};

typedef struct {
  hal_bluetooth_classic_address_t address;
  uint8_t link_key[JH_BLUETOOTH_CLASSIC_LINK_KEY_LEN];
  uint32_t sequence;
  uint16_t profile_id;
  uint8_t link_key_type;
} jh_bluetooth_classic_bond_identity_t;

hal_status_t jh_bluetooth_classic_bond_encode(
    const jh_bluetooth_classic_bond_identity_t *identity,
    hal_bluetooth_classic_bond_blob_t *out_blob);

hal_status_t jh_bluetooth_classic_bond_decode(
    const hal_bluetooth_classic_bond_blob_t *blob,
    jh_bluetooth_classic_bond_identity_t *out_identity);

#ifdef __cplusplus
}
#endif
