#include "jh_ota_image.h"

#include "hal/core/hal_target.h"
#include "hal/core/jh_endian.h"
#include "hal/security/hal_crc.h"
#include "hal/security/hal_crypto.h"

#include <string.h>

#if defined(HAL_ENABLE_CRYPTO) && defined(HAL_ENABLE_CRC)

namespace {

constexpr uint8_t kImageMagic[8] = {'J', 'H', 'O', 'T', 'A', '1', '\r', '\n'};
constexpr uint8_t kStateMagic[8] = {'J', 'H', 'O', 'T', 'A', 'S', 'T', '1'};
constexpr size_t kImageCrcOffset = JH_OTA_IMAGE_HEADER_SIZE - sizeof(uint32_t);
constexpr size_t kStateCrcOffset = JH_OTA_STATE_RECORD_SIZE - sizeof(uint32_t);

bool text_valid(const char *text) {
  return text != nullptr &&
         memchr(text, '\0', JH_OTA_VERSION_TEXT_SIZE) != nullptr;
}

bool manifest_valid(const jh_ota_image_manifest_t *manifest) {
  return manifest != nullptr && manifest->target != JH_OTA_TARGET_UNKNOWN &&
         manifest->payload_size > 0u && text_valid(manifest->version);
}

hal_status_t manifest_auth_bytes(const jh_ota_image_manifest_t *manifest,
                                 uint8_t out[96]) {
  if (!manifest_valid(manifest) || out == nullptr) {
    return HAL_EINVAL;
  }
  uint8_t encoded[JH_OTA_IMAGE_HEADER_SIZE];
  jh_ota_image_manifest_t unsigned_manifest = *manifest;
  memset(unsigned_manifest.signature, 0, sizeof(unsigned_manifest.signature));
  const hal_status_t status =
      jh_ota_image_manifest_encode(&unsigned_manifest, encoded);
  if (status != HAL_OK) {
    return status;
  }
  memcpy(out, encoded, 96u);
  return HAL_OK;
}

bool state_valid(const jh_ota_boot_state_t *state) {
  return state != nullptr && state->mode >= JH_OTA_BOOT_STABLE &&
         state->mode <= JH_OTA_BOOT_RECOVERY && state->max_attempts > 0u &&
         text_valid(state->program_version) &&
         text_valid(state->staging_version);
}

bool sequence_newer(uint32_t lhs, uint32_t rhs) {
  return (int32_t)(lhs - rhs) > 0;
}

} // namespace

hal_status_t
jh_ota_image_manifest_encode(const jh_ota_image_manifest_t *manifest,
                             uint8_t out[JH_OTA_IMAGE_HEADER_SIZE]) {
  if (!manifest_valid(manifest) || out == nullptr) {
    return HAL_EINVAL;
  }
  memset(out, 0, JH_OTA_IMAGE_HEADER_SIZE);
  memcpy(out, kImageMagic, sizeof(kImageMagic));
  jh_store_le16(&out[8], JH_OTA_IMAGE_VERSION);
  jh_store_le16(&out[10], JH_OTA_IMAGE_HEADER_SIZE);
  jh_store_le16(&out[12], (uint16_t)manifest->target);
  jh_store_le16(&out[14], manifest->flags);
  jh_store_le32(&out[16], manifest->program_offset);
  jh_store_le32(&out[20], manifest->payload_size);
  jh_store_le32(&out[24], manifest->generation);
  memcpy(&out[32], manifest->sha256, sizeof(manifest->sha256));
  memcpy(&out[64], manifest->version, sizeof(manifest->version));
  memcpy(&out[96], manifest->signature, sizeof(manifest->signature));
  jh_store_le32(&out[kImageCrcOffset], hal_crc32(out, kImageCrcOffset));
  return HAL_OK;
}

hal_status_t
jh_ota_image_manifest_decode(const uint8_t raw[JH_OTA_IMAGE_HEADER_SIZE],
                             jh_ota_image_manifest_t *out_manifest) {
  if (raw == nullptr || out_manifest == nullptr) {
    return HAL_EINVAL;
  }
  if (memcmp(raw, kImageMagic, sizeof(kImageMagic)) != 0 ||
      jh_load_le16(&raw[8]) != JH_OTA_IMAGE_VERSION ||
      jh_load_le16(&raw[10]) != JH_OTA_IMAGE_HEADER_SIZE ||
      memchr(&raw[64], '\0', JH_OTA_VERSION_TEXT_SIZE) == nullptr ||
      jh_load_le32(&raw[kImageCrcOffset]) != hal_crc32(raw, kImageCrcOffset)) {
    return HAL_EPROTO;
  }

  jh_ota_image_manifest_t manifest = {};
  manifest.target = (jh_ota_target_t)jh_load_le16(&raw[12]);
  manifest.flags = jh_load_le16(&raw[14]);
  manifest.program_offset = jh_load_le32(&raw[16]);
  manifest.payload_size = jh_load_le32(&raw[20]);
  manifest.generation = jh_load_le32(&raw[24]);
  memcpy(manifest.sha256, &raw[32], sizeof(manifest.sha256));
  memcpy(manifest.version, &raw[64], sizeof(manifest.version));
  memcpy(manifest.signature, &raw[96], sizeof(manifest.signature));
  if (!manifest_valid(&manifest)) {
    return HAL_EPROTO;
  }
  *out_manifest = manifest;
  return HAL_OK;
}

hal_status_t jh_ota_image_manifest_sign(jh_ota_image_manifest_t *manifest,
                                        const uint8_t *key, size_t key_size) {
  if (manifest == nullptr || (key == nullptr && key_size > 0u)) {
    return HAL_EINVAL;
  }
  uint8_t authenticated[96];
  hal_status_t status = manifest_auth_bytes(manifest, authenticated);
  if (status != HAL_OK) {
    return status;
  }
  return hal_status_from_bool(hal_hmac_sha256(key, key_size, authenticated,
                                              sizeof(authenticated),
                                              manifest->signature),
                              HAL_EINTERNAL);
}

hal_status_t
jh_ota_image_manifest_verify(const jh_ota_image_manifest_t *manifest,
                             const uint8_t *key, size_t key_size) {
  if (manifest == nullptr || (key == nullptr && key_size > 0u)) {
    return HAL_EINVAL;
  }
  jh_ota_image_manifest_t expected = *manifest;
  const hal_status_t status =
      jh_ota_image_manifest_sign(&expected, key, key_size);
  if (status != HAL_OK) {
    return status;
  }
  uint8_t difference = 0u;
  for (size_t index = 0u; index < sizeof(expected.signature); ++index) {
    difference |= expected.signature[index] ^ manifest->signature[index];
  }
  return difference == 0u ? HAL_OK : HAL_EAUTH;
}

jh_ota_target_t jh_ota_current_target(void) {
#if HAL_TARGET_IS_RP2040
  return JH_OTA_TARGET_RP2040;
#elif HAL_TARGET_IS_RP2350_ARM
  return JH_OTA_TARGET_RP2350_ARM;
#elif HAL_TARGET_IS_RP2350_RISCV
  return JH_OTA_TARGET_RP2350_RISCV;
#else
  return JH_OTA_TARGET_UNKNOWN;
#endif
}

hal_status_t jh_ota_boot_state_encode(const jh_ota_boot_state_t *state,
                                      uint8_t out[JH_OTA_STATE_RECORD_SIZE]) {
  if (!state_valid(state) || out == nullptr) {
    return HAL_EINVAL;
  }
  memset(out, 0, JH_OTA_STATE_RECORD_SIZE);
  memcpy(out, kStateMagic, sizeof(kStateMagic));
  jh_store_le16(&out[8], JH_OTA_STATE_VERSION);
  jh_store_le16(&out[10], JH_OTA_STATE_RECORD_SIZE);
  jh_store_le32(&out[12], state->sequence);
  out[16] = (uint8_t)state->mode;
  out[17] = state->attempts;
  out[18] = state->max_attempts;
  jh_store_le32(&out[20], state->program_size);
  jh_store_le32(&out[24], state->staging_size);
  jh_store_le32(&out[28], state->program_generation);
  jh_store_le32(&out[32], state->staging_generation);
  memcpy(&out[36], state->program_sha256, sizeof(state->program_sha256));
  memcpy(&out[68], state->staging_sha256, sizeof(state->staging_sha256));
  memcpy(&out[100], state->program_version, sizeof(state->program_version));
  memcpy(&out[132], state->staging_version, sizeof(state->staging_version));
  jh_store_le32(&out[kStateCrcOffset], hal_crc32(out, kStateCrcOffset));
  return HAL_OK;
}

hal_status_t
jh_ota_boot_state_decode(const uint8_t raw[JH_OTA_STATE_RECORD_SIZE],
                         jh_ota_boot_state_t *out_state) {
  if (raw == nullptr || out_state == nullptr) {
    return HAL_EINVAL;
  }
  if (memcmp(raw, kStateMagic, sizeof(kStateMagic)) != 0 ||
      jh_load_le16(&raw[8]) != JH_OTA_STATE_VERSION ||
      jh_load_le16(&raw[10]) != JH_OTA_STATE_RECORD_SIZE ||
      jh_load_le32(&raw[kStateCrcOffset]) != hal_crc32(raw, kStateCrcOffset)) {
    return HAL_EPROTO;
  }

  jh_ota_boot_state_t state = {};
  state.sequence = jh_load_le32(&raw[12]);
  state.mode = (jh_ota_boot_mode_t)raw[16];
  state.attempts = raw[17];
  state.max_attempts = raw[18];
  state.program_size = jh_load_le32(&raw[20]);
  state.staging_size = jh_load_le32(&raw[24]);
  state.program_generation = jh_load_le32(&raw[28]);
  state.staging_generation = jh_load_le32(&raw[32]);
  memcpy(state.program_sha256, &raw[36], sizeof(state.program_sha256));
  memcpy(state.staging_sha256, &raw[68], sizeof(state.staging_sha256));
  memcpy(state.program_version, &raw[100], sizeof(state.program_version));
  memcpy(state.staging_version, &raw[132], sizeof(state.staging_version));
  state.program_version[JH_OTA_VERSION_TEXT_SIZE - 1u] = '\0';
  state.staging_version[JH_OTA_VERSION_TEXT_SIZE - 1u] = '\0';
  if (!state_valid(&state)) {
    return HAL_EPROTO;
  }
  *out_state = state;
  return HAL_OK;
}

hal_status_t
jh_ota_boot_state_select(const uint8_t first[JH_OTA_STATE_RECORD_SIZE],
                         const uint8_t second[JH_OTA_STATE_RECORD_SIZE],
                         jh_ota_boot_state_t *out_state,
                         uint8_t *out_selected_index) {
  if (first == nullptr || second == nullptr || out_state == nullptr ||
      out_selected_index == nullptr) {
    return HAL_EINVAL;
  }
  jh_ota_boot_state_t states[2] = {};
  const bool valid_first =
      jh_ota_boot_state_decode(first, &states[0]) == HAL_OK;
  const bool valid_second =
      jh_ota_boot_state_decode(second, &states[1]) == HAL_OK;
  if (!valid_first && !valid_second) {
    return HAL_ENOENT;
  }
  const uint8_t selected =
      valid_second && (!valid_first ||
                       sequence_newer(states[1].sequence, states[0].sequence))
          ? 1u
          : 0u;
  *out_state = states[selected];
  *out_selected_index = selected;
  return HAL_OK;
}

#endif
