#include "jh_gamepad_bond_codec.h"

#include "hal/security/hal_crc.h"
#include "jh_bluetooth_gamepad_identity.h"

#include <string.h>

enum {
  JH_GAMEPAD_BOND_MAGIC_OFFSET = 0,
  JH_GAMEPAD_BOND_VERSION_OFFSET = 4,
  JH_GAMEPAD_BOND_RESERVED_OFFSET = 5,
  JH_GAMEPAD_BOND_RULES_ID_OFFSET = 6,
  JH_GAMEPAD_BOND_ADDR_OFFSET = 8,
  JH_GAMEPAD_BOND_LINK_KEY_OFFSET = 14,
  JH_GAMEPAD_BOND_LINK_KEY_TYPE_OFFSET = 30,
  JH_GAMEPAD_BOND_RESERVED2_OFFSET = 31,
  JH_GAMEPAD_BOND_SEQUENCE_OFFSET = 32,
  JH_GAMEPAD_BOND_CRC_OFFSET = 36,
  JH_GAMEPAD_BOND_CRC_COVERED_BYTES = 36,
};

/* "JHGB" (JaszczurHAL Gamepad Bond), little-endian. */
static const uint32_t JH_GAMEPAD_BOND_MAGIC = 0x4247484Au;
static const uint8_t JH_GAMEPAD_BOND_VERSION = 1u;

static void write_u16(uint8_t *raw, uint16_t value) {
  raw[0] = (uint8_t)(value & 0xFFu);
  raw[1] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void write_u32(uint8_t *raw, uint32_t value) {
  raw[0] = (uint8_t)(value & 0xFFu);
  raw[1] = (uint8_t)((value >> 8u) & 0xFFu);
  raw[2] = (uint8_t)((value >> 16u) & 0xFFu);
  raw[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static uint16_t read_u16(const uint8_t *raw) {
  return (uint16_t)raw[0] | (uint16_t)((uint16_t)raw[1] << 8u);
}

static uint32_t read_u32(const uint8_t *raw) {
  return (uint32_t)raw[0] | ((uint32_t)raw[1] << 8u) |
         ((uint32_t)raw[2] << 16u) | ((uint32_t)raw[3] << 24u);
}

hal_status_t jh_gamepad_bond_encode(const jh_gamepad_bond_identity_t *identity,
                                    uint32_t sequence,
                                    hal_gamepad_bond_blob_t *out_blob) {
  if (identity == NULL || out_blob == NULL) {
    return HAL_EINVAL;
  }

  uint8_t *raw = out_blob->bytes;
  memset(raw, 0, sizeof(out_blob->bytes));
  write_u32(raw + JH_GAMEPAD_BOND_MAGIC_OFFSET, JH_GAMEPAD_BOND_MAGIC);
  raw[JH_GAMEPAD_BOND_VERSION_OFFSET] = JH_GAMEPAD_BOND_VERSION;
  write_u16(raw + JH_GAMEPAD_BOND_RULES_ID_OFFSET,
            (uint16_t)JH_BLUETOOTH_GAMEPAD_BOND_RULES_ID);
  memcpy(raw + JH_GAMEPAD_BOND_ADDR_OFFSET, identity->bd_addr,
         JH_GAMEPAD_BOND_ADDR_LEN);
  memcpy(raw + JH_GAMEPAD_BOND_LINK_KEY_OFFSET, identity->link_key,
         JH_GAMEPAD_BOND_LINK_KEY_LEN);
  raw[JH_GAMEPAD_BOND_LINK_KEY_TYPE_OFFSET] = identity->link_key_type;
  write_u32(raw + JH_GAMEPAD_BOND_SEQUENCE_OFFSET, sequence);

  const uint16_t crc = hal_crc16_ccitt(raw, JH_GAMEPAD_BOND_CRC_COVERED_BYTES,
                                       HAL_CRC16_CCITT_INIT);
  write_u16(raw + JH_GAMEPAD_BOND_CRC_OFFSET, crc);
  return HAL_OK;
}

hal_status_t jh_gamepad_bond_decode(const hal_gamepad_bond_blob_t *blob,
                                    jh_gamepad_bond_identity_t *out_identity,
                                    uint32_t *out_sequence) {
  if (blob == NULL || out_identity == NULL) {
    return HAL_EINVAL;
  }

  const uint8_t *raw = blob->bytes;
  const uint32_t magic = read_u32(raw + JH_GAMEPAD_BOND_MAGIC_OFFSET);
  const uint8_t version = raw[JH_GAMEPAD_BOND_VERSION_OFFSET];
  const uint16_t rules_id = read_u16(raw + JH_GAMEPAD_BOND_RULES_ID_OFFSET);
  if (magic != JH_GAMEPAD_BOND_MAGIC || version != JH_GAMEPAD_BOND_VERSION ||
      rules_id != (uint16_t)JH_BLUETOOTH_GAMEPAD_BOND_RULES_ID) {
    return HAL_EPROTO;
  }

  const uint16_t expected_crc = hal_crc16_ccitt(
      raw, JH_GAMEPAD_BOND_CRC_COVERED_BYTES, HAL_CRC16_CCITT_INIT);
  const uint16_t stored_crc = read_u16(raw + JH_GAMEPAD_BOND_CRC_OFFSET);
  if (expected_crc != stored_crc) {
    return HAL_EPROTO;
  }

  memcpy(out_identity->bd_addr, raw + JH_GAMEPAD_BOND_ADDR_OFFSET,
         JH_GAMEPAD_BOND_ADDR_LEN);
  memcpy(out_identity->link_key, raw + JH_GAMEPAD_BOND_LINK_KEY_OFFSET,
         JH_GAMEPAD_BOND_LINK_KEY_LEN);
  out_identity->link_key_type = raw[JH_GAMEPAD_BOND_LINK_KEY_TYPE_OFFSET];
  if (out_sequence != NULL) {
    *out_sequence = read_u32(raw + JH_GAMEPAD_BOND_SEQUENCE_OFFSET);
  }
  return HAL_OK;
}
