#include "jh_gamepad_bond_codec.h"

#include "jh_bluetooth_gamepad_identity.h"

#include <string.h>

hal_status_t jh_gamepad_bond_encode(const jh_gamepad_bond_identity_t *identity,
                                    uint32_t sequence,
                                    hal_gamepad_bond_blob_t *out_blob) {
  if (identity == NULL || out_blob == NULL) {
    return HAL_EINVAL;
  }
  jh_bluetooth_classic_bond_identity_t classic = {0};
  memcpy(classic.address.bytes, identity->bd_addr, JH_GAMEPAD_BOND_ADDR_LEN);
  memcpy(classic.link_key, identity->link_key, JH_GAMEPAD_BOND_LINK_KEY_LEN);
  classic.link_key_type = identity->link_key_type;
  classic.profile_id = (uint16_t)JH_BLUETOOTH_GAMEPAD_BOND_RULES_ID;
  classic.sequence = sequence;
  return jh_bluetooth_classic_bond_encode(&classic, out_blob);
}

hal_status_t jh_gamepad_bond_decode(const hal_gamepad_bond_blob_t *blob,
                                    jh_gamepad_bond_identity_t *out_identity,
                                    uint32_t *out_sequence) {
  if (blob == NULL || out_identity == NULL) {
    return HAL_EINVAL;
  }

  jh_bluetooth_classic_bond_identity_t classic = {0};
  const hal_status_t status = jh_bluetooth_classic_bond_decode(blob, &classic);
  if (status != HAL_OK ||
      classic.profile_id != (uint16_t)JH_BLUETOOTH_GAMEPAD_BOND_RULES_ID) {
    return status == HAL_OK ? HAL_EPROTO : status;
  }
  memcpy(out_identity->bd_addr, classic.address.bytes,
         JH_GAMEPAD_BOND_ADDR_LEN);
  memcpy(out_identity->link_key, classic.link_key,
         JH_GAMEPAD_BOND_LINK_KEY_LEN);
  out_identity->link_key_type = classic.link_key_type;
  if (out_sequence != NULL) {
    *out_sequence = classic.sequence;
  }
  return HAL_OK;
}
