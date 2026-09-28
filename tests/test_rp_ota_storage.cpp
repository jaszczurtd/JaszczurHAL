#include <hal/impl/rp2040/drivers/flash/rp_flash_transaction.h>
#include <hal/impl/rp2040/drivers/flash/rp_ota_storage.h>
#include <hal/network/ota/jh_ota_image.h>
#include <hal/security/hal_crypto.h>
#include <hardware/flash.h>
#include <utils/unity.h>

#include <cstring>
#include <initializer_list>

uint8_t test_rp_flash[4u * 1024u * 1024u];

// Outside the program slot, so the factory state is never built from them.
extern "C" {
uint8_t __flash_binary_start;
uint8_t __flash_binary_end;
}

extern "C" void flash_range_erase(uint32_t offset, size_t size) {
  TEST_ASSERT_EQUAL_UINT32(0u, offset % FLASH_SECTOR_SIZE);
  TEST_ASSERT_TRUE(size <= sizeof(test_rp_flash) - offset);
  std::memset(test_rp_flash + offset, 0xff, size);
}

extern "C" void flash_range_program(uint32_t offset, const uint8_t *data,
                                    size_t size) {
  TEST_ASSERT_EQUAL_UINT32(0u, offset % FLASH_PAGE_SIZE);
  TEST_ASSERT_TRUE(size <= sizeof(test_rp_flash) - offset);
  for (size_t index = 0u; index < size; ++index) {
    test_rp_flash[offset + index] &= data[index];
  }
}

extern "C" hal_status_t
jh_rp_flash_transaction_execute(jh_rp_flash_operation_t operation,
                                void *context, uint32_t timeout_ms) {
  (void)timeout_ms;
  return operation(context);
}

namespace {

const uint8_t kKey[] = {'s', 'e', 'c', 'r', 'e', 't'};
constexpr uint32_t kPayloadSize = 2u * FLASH_SECTOR_SIZE + 100u;
uint8_t s_container[JH_OTA_IMAGE_HEADER_SIZE + kPayloadSize];

void build_container(uint8_t fill) {
  uint8_t *payload = s_container + JH_OTA_IMAGE_HEADER_SIZE;
  std::memset(payload, fill, kPayloadSize);
  jh_ota_image_manifest_t manifest = {};
  manifest.target = JH_OTA_TARGET_RP2040;
  manifest.program_offset = HAL_RP_OTA_PROGRAM_OFFSET;
  manifest.payload_size = kPayloadSize;
  manifest.generation = fill;
  std::memcpy(manifest.version, "test", 5u);
  hal_sha256_context_t sha = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sha256_init_ex(&sha));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_sha256_update_ex(&sha, payload, kPayloadSize));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_sha256_final_ex(&sha, manifest.sha256));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_ota_image_manifest_sign(&manifest, kKey, sizeof(kKey)));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ota_image_manifest_encode(&manifest, s_container));
}

hal_status_t upload(uint8_t fill) {
  build_container(fill);
  hal_status_t status =
      jh_rp_ota_storage_begin(sizeof(s_container), kKey, sizeof(kKey));
  if (status != HAL_OK) {
    return status;
  }
  size_t written = 0u;
  status = jh_rp_ota_storage_write(s_container, sizeof(s_container), &written);
  if (status != HAL_OK) {
    jh_rp_ota_storage_abort();
    return status;
  }
  return jh_rp_ota_storage_finish();
}

void store_state(jh_ota_boot_mode_t mode) {
  jh_ota_boot_state_t state = {};
  state.sequence = 5u;
  state.mode = mode;
  state.max_attempts = 3u;
  state.program_size = kPayloadSize;
  state.staging_size = kPayloadSize;
  uint8_t encoded[JH_OTA_STATE_RECORD_SIZE];
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ota_boot_state_encode(&state, encoded));
  flash_range_erase(HAL_RP_OTA_STATE_A_OFFSET, FLASH_SECTOR_SIZE);
  flash_range_program(HAL_RP_OTA_STATE_A_OFFSET, encoded, sizeof(encoded));
}

jh_ota_boot_mode_t stored_mode(void) {
  jh_ota_boot_state_t state = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_rp_ota_storage_get_state(&state));
  return state.mode;
}

void assert_staging_filled(uint8_t fill) {
  const uint8_t *staging = test_rp_flash + HAL_RP_OTA_STAGING_OFFSET;
  for (uint32_t index = 0u; index < kPayloadSize; ++index) {
    TEST_ASSERT_EQUAL_HEX8(fill, staging[index]);
  }
}

} // namespace

void setUp(void) {
  std::memset(test_rp_flash, 0xff, sizeof(test_rp_flash));
  jh_rp_ota_storage_abort();
}

void tearDown(void) {}

void test_stable_upload_stages_image_and_marks_pending(void) {
  store_state(JH_OTA_BOOT_STABLE);
  TEST_ASSERT_EQUAL_INT(HAL_OK, upload(0x5A));
  TEST_ASSERT_EQUAL_INT(JH_OTA_BOOT_PENDING, stored_mode());
  assert_staging_filled(0x5A);
}

void test_upload_while_pending_keeps_staged_image(void) {
  store_state(JH_OTA_BOOT_STABLE);
  TEST_ASSERT_EQUAL_INT(HAL_OK, upload(0x5A));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, upload(0xC3));
  TEST_ASSERT_EQUAL_INT(JH_OTA_BOOT_PENDING, stored_mode());
  assert_staging_filled(0x5A);
}

void test_upload_during_trial_keeps_rollback_copy(void) {
  store_state(JH_OTA_BOOT_TRIAL);
  std::memset(test_rp_flash + HAL_RP_OTA_STAGING_OFFSET, 0x11, kPayloadSize);
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, upload(0xC3));
  TEST_ASSERT_EQUAL_INT(JH_OTA_BOOT_TRIAL, stored_mode());
  assert_staging_filled(0x11);
}

void test_upload_is_refused_in_rollback_and_recovery(void) {
  for (jh_ota_boot_mode_t mode : {JH_OTA_BOOT_ROLLBACK, JH_OTA_BOOT_RECOVERY}) {
    store_state(mode);
    TEST_ASSERT_EQUAL_INT(
        HAL_ESTATE,
        jh_rp_ota_storage_begin(sizeof(s_container), kKey, sizeof(kKey)));
  }
}

void test_first_upload_without_state_is_accepted(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_rp_ota_storage_begin(sizeof(s_container), kKey, sizeof(kKey)));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_stable_upload_stages_image_and_marks_pending);
  RUN_TEST(test_upload_while_pending_keeps_staged_image);
  RUN_TEST(test_upload_during_trial_keeps_rollback_copy);
  RUN_TEST(test_upload_is_refused_in_rollback_and_recovery);
  RUN_TEST(test_first_upload_without_state_is_accepted);
  return UNITY_END();
}
