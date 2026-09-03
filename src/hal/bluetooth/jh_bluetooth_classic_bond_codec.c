#include "jh_bluetooth_classic_bond_codec.h"

#include "hal/core/jh_endian.h"
#include "hal/security/hal_crc.h"

#include <string.h>

enum {
  JH_CLASSIC_BOND_MAGIC_OFFSET = 0,
  JH_CLASSIC_BOND_VERSION_OFFSET = 4,
  JH_CLASSIC_BOND_RESERVED_OFFSET = 5,
  JH_CLASSIC_BOND_PROFILE_ID_OFFSET = 6,
  JH_CLASSIC_BOND_ADDR_OFFSET = 8,
  JH_CLASSIC_BOND_LINK_KEY_OFFSET = 14,
  JH_CLASSIC_BOND_LINK_KEY_TYPE_OFFSET = 30,
  JH_CLASSIC_BOND_RESERVED2_OFFSET = 31,
  JH_CLASSIC_BOND_SEQUENCE_OFFSET = 32,
  JH_CLASSIC_BOND_CRC_OFFSET = 36,
  JH_CLASSIC_BOND_CRC_COVERED_BYTES = 36,
};

/* "JHCB" (JaszczurHAL Classic Bond), little-endian. */
static const uint32_t JH_CLASSIC_BOND_MAGIC = UINT32_C(0x4243484A);
/* Legacy gamepad records used "JHGB" with the same byte layout. */
static const uint32_t JH_GAMEPAD_BOND_LEGACY_MAGIC = UINT32_C(0x4247484A);
static const uint8_t JH_CLASSIC_BOND_VERSION = 1u;

hal_status_t jh_bluetooth_classic_bond_encode(
    const jh_bluetooth_classic_bond_identity_t *identity,
    hal_bluetooth_classic_bond_blob_t *out_blob) {
  if (identity == NULL || out_blob == NULL || identity->profile_id == 0u) {
    return HAL_EINVAL;
  }
  uint8_t *raw = out_blob->bytes;
  memset(raw, 0, sizeof(out_blob->bytes));
  jh_store_le32(raw + JH_CLASSIC_BOND_MAGIC_OFFSET, JH_CLASSIC_BOND_MAGIC);
  raw[JH_CLASSIC_BOND_VERSION_OFFSET] = JH_CLASSIC_BOND_VERSION;
  jh_store_le16(raw + JH_CLASSIC_BOND_PROFILE_ID_OFFSET, identity->profile_id);
  memcpy(raw + JH_CLASSIC_BOND_ADDR_OFFSET, identity->address.bytes,
         HAL_BLUETOOTH_CLASSIC_ADDRESS_LEN);
  memcpy(raw + JH_CLASSIC_BOND_LINK_KEY_OFFSET, identity->link_key,
         JH_BLUETOOTH_CLASSIC_LINK_KEY_LEN);
  raw[JH_CLASSIC_BOND_LINK_KEY_TYPE_OFFSET] = identity->link_key_type;
  jh_store_le32(raw + JH_CLASSIC_BOND_SEQUENCE_OFFSET, identity->sequence);
  jh_store_le16(raw + JH_CLASSIC_BOND_CRC_OFFSET,
                hal_crc16_ccitt(raw, JH_CLASSIC_BOND_CRC_COVERED_BYTES,
                                HAL_CRC16_CCITT_INIT));
  return HAL_OK;
}

hal_status_t jh_bluetooth_classic_bond_decode(
    const hal_bluetooth_classic_bond_blob_t *blob,
    jh_bluetooth_classic_bond_identity_t *out_identity) {
  if (blob == NULL || out_identity == NULL) {
    return HAL_EINVAL;
  }
  const uint8_t *raw = blob->bytes;
  const uint32_t magic = jh_load_le32(raw + JH_CLASSIC_BOND_MAGIC_OFFSET);
  const uint16_t stored_crc = jh_load_le16(raw + JH_CLASSIC_BOND_CRC_OFFSET);
  if ((magic != JH_CLASSIC_BOND_MAGIC &&
       magic != JH_GAMEPAD_BOND_LEGACY_MAGIC) ||
      raw[JH_CLASSIC_BOND_VERSION_OFFSET] != JH_CLASSIC_BOND_VERSION ||
      jh_load_le16(raw + JH_CLASSIC_BOND_PROFILE_ID_OFFSET) == 0u ||
      stored_crc != hal_crc16_ccitt(raw, JH_CLASSIC_BOND_CRC_COVERED_BYTES,
                                    HAL_CRC16_CCITT_INIT)) {
    return HAL_EPROTO;
  }
  memset(out_identity, 0, sizeof(*out_identity));
  out_identity->profile_id =
      jh_load_le16(raw + JH_CLASSIC_BOND_PROFILE_ID_OFFSET);
  memcpy(out_identity->address.bytes, raw + JH_CLASSIC_BOND_ADDR_OFFSET,
         HAL_BLUETOOTH_CLASSIC_ADDRESS_LEN);
  memcpy(out_identity->link_key, raw + JH_CLASSIC_BOND_LINK_KEY_OFFSET,
         JH_BLUETOOTH_CLASSIC_LINK_KEY_LEN);
  out_identity->link_key_type = raw[JH_CLASSIC_BOND_LINK_KEY_TYPE_OFFSET];
  out_identity->sequence = jh_load_le32(raw + JH_CLASSIC_BOND_SEQUENCE_OFFSET);
  return HAL_OK;
}
