#include "hal/network/ota/jh_ota_image.h"
#include "hal/security/hal_crc.h"
#include "utils/unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static jh_ota_boot_state_t make_state(uint32_t sequence,
                                      jh_ota_boot_mode_t mode) {
  jh_ota_boot_state_t state = {};
  state.sequence = sequence;
  state.mode = mode;
  state.max_attempts = 3u;
  state.program_size = 4096u;
  state.staging_size = 8192u;
  state.program_generation = 4u;
  state.staging_generation = 5u;
  memset(state.program_sha256, 0x11, sizeof(state.program_sha256));
  memset(state.staging_sha256, 0x22, sizeof(state.staging_sha256));
  memcpy(state.program_version, "1.0.0", 6u);
  memcpy(state.staging_version, "1.1.0", 6u);
  return state;
}

void test_manifest_roundtrip(void) {
  jh_ota_image_manifest_t manifest = {};
  manifest.target = JH_OTA_TARGET_RP2350_ARM;
  manifest.flags = 0x1234u;
  manifest.program_offset = 0x4000u;
  manifest.payload_size = 123456u;
  manifest.generation = 42u;
  memset(manifest.sha256, 0xA5, sizeof(manifest.sha256));
  memcpy(manifest.version, "2026.07.28", 11u);
  static const uint8_t key[] = "ota-test-key";
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_ota_image_manifest_sign(&manifest, key, sizeof(key) - 1u));

  uint8_t encoded[JH_OTA_IMAGE_HEADER_SIZE];
  jh_ota_image_manifest_t decoded = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ota_image_manifest_encode(&manifest, encoded));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ota_image_manifest_decode(encoded, &decoded));
  TEST_ASSERT_EQUAL_INT(manifest.target, decoded.target);
  TEST_ASSERT_EQUAL_HEX16(manifest.flags, decoded.flags);
  TEST_ASSERT_EQUAL_HEX32(manifest.program_offset, decoded.program_offset);
  TEST_ASSERT_EQUAL_UINT32(manifest.payload_size, decoded.payload_size);
  TEST_ASSERT_EQUAL_UINT32(manifest.generation, decoded.generation);
  TEST_ASSERT_EQUAL_MEMORY(manifest.sha256, decoded.sha256,
                           sizeof(decoded.sha256));
  TEST_ASSERT_EQUAL_STRING(manifest.version, decoded.version);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_ota_image_manifest_verify(&decoded, key, sizeof(key) - 1u));
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH, jh_ota_image_manifest_verify(
                                       &decoded, (const uint8_t *)"wrong", 5u));
}

void test_manifest_rejects_corruption_and_wrong_shape(void) {
  jh_ota_image_manifest_t manifest = {};
  manifest.target = JH_OTA_TARGET_RP2040;
  manifest.program_offset = 0x4000u;
  manifest.payload_size = 1024u;
  memcpy(manifest.version, "test", 5u);
  uint8_t encoded[JH_OTA_IMAGE_HEADER_SIZE];

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ota_image_manifest_encode(&manifest, encoded));
  encoded[37] ^= 0x80u;
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_ota_image_manifest_decode(encoded, &manifest));

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ota_image_manifest_encode(&manifest, encoded));
  memset(&encoded[64], 'x', JH_OTA_VERSION_TEXT_SIZE);
  const size_t crc_offset = JH_OTA_IMAGE_HEADER_SIZE - sizeof(uint32_t);
  const uint32_t crc = hal_crc32(encoded, crc_offset);
  encoded[crc_offset] = (uint8_t)crc;
  encoded[crc_offset + 1u] = (uint8_t)(crc >> 8);
  encoded[crc_offset + 2u] = (uint8_t)(crc >> 16);
  encoded[crc_offset + 3u] = (uint8_t)(crc >> 24);
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_ota_image_manifest_decode(encoded, &manifest));

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_ota_image_manifest_encode(nullptr, encoded));
}

void test_boot_state_roundtrip_and_newest_selection(void) {
  jh_ota_boot_state_t first_state = make_state(10u, JH_OTA_BOOT_PENDING);
  jh_ota_boot_state_t second_state = make_state(11u, JH_OTA_BOOT_TRIAL);
  second_state.attempts = 1u;
  uint8_t first[JH_OTA_STATE_RECORD_SIZE];
  uint8_t second[JH_OTA_STATE_RECORD_SIZE];
  jh_ota_boot_state_t selected = {};
  uint8_t selected_index = 0xFFu;

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ota_boot_state_encode(&first_state, first));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ota_boot_state_encode(&second_state, second));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ota_boot_state_select(
                                    first, second, &selected, &selected_index));
  TEST_ASSERT_EQUAL_UINT8(1u, selected_index);
  TEST_ASSERT_EQUAL_UINT32(second_state.sequence, selected.sequence);
  TEST_ASSERT_EQUAL_INT(JH_OTA_BOOT_TRIAL, selected.mode);
  TEST_ASSERT_EQUAL_UINT8(1u, selected.attempts);
}

void test_boot_state_survives_one_torn_record_and_wraparound(void) {
  jh_ota_boot_state_t old_state =
      make_state(UINT32_MAX - 1u, JH_OTA_BOOT_STABLE);
  jh_ota_boot_state_t wrapped_state = make_state(2u, JH_OTA_BOOT_PENDING);
  uint8_t first[JH_OTA_STATE_RECORD_SIZE];
  uint8_t second[JH_OTA_STATE_RECORD_SIZE];
  jh_ota_boot_state_t selected = {};
  uint8_t selected_index = 0xFFu;

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ota_boot_state_encode(&old_state, first));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ota_boot_state_encode(&wrapped_state, second));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ota_boot_state_select(
                                    first, second, &selected, &selected_index));
  TEST_ASSERT_EQUAL_UINT8(1u, selected_index);

  second[200] ^= 0x01u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ota_boot_state_select(
                                    first, second, &selected, &selected_index));
  TEST_ASSERT_EQUAL_UINT8(0u, selected_index);

  first[201] ^= 0x01u;
  TEST_ASSERT_EQUAL_INT(
      HAL_ENOENT,
      jh_ota_boot_state_select(first, second, &selected, &selected_index));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_manifest_roundtrip);
  RUN_TEST(test_manifest_rejects_corruption_and_wrong_shape);
  RUN_TEST(test_boot_state_roundtrip_and_newest_selection);
  RUN_TEST(test_boot_state_survives_one_torn_record_and_wraparound);
  return UNITY_END();
}
