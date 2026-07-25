#include "utils/unity.h"

#include <hal/impl/shared/drivers/cyw43-driver/jh_cyw43_driver.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_stage_vocabulary_is_bounded(void) {
  TEST_ASSERT_EQUAL_STRING(
      "none", jh_cyw43_driver_stage_string(JH_CYW43_DRIVER_STAGE_NONE));
  TEST_ASSERT_EQUAL_STRING("low-level", jh_cyw43_driver_stage_string(
                                            JH_CYW43_DRIVER_STAGE_LOW_LEVEL));
  TEST_ASSERT_EQUAL_STRING(
      "bus", jh_cyw43_driver_stage_string(JH_CYW43_DRIVER_STAGE_BUS));
  TEST_ASSERT_EQUAL_STRING(
      "ready", jh_cyw43_driver_stage_string(JH_CYW43_DRIVER_STAGE_READY));
}

static void test_non_stm32_build_has_no_driver_instance(void) {
  jh_cyw43_driver_result_t result{};

  TEST_ASSERT_FALSE(jh_cyw43_driver_is_ready());
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        jh_cyw43_driver_start(nullptr, &result));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, jh_cyw43_driver_restart(&result));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, jh_cyw43_driver_stop());
  TEST_ASSERT_FALSE(jh_cyw43_driver_is_ready());
}

static void test_fallback_mac_uses_unique_uid_suffix(void) {
  const uint8_t old_uid[HAL_DEVICE_UID_BYTES] = {
      0xE6u, 0x64u, 0x28u, 0x15u, 0xE3u, 0x78u, 0x8Fu, 0x23u,
  };
  const uint8_t replacement_uid[HAL_DEVICE_UID_BYTES] = {
      0xE6u, 0x64u, 0x28u, 0x15u, 0xE3u, 0x52u, 0x87u, 0x23u,
  };
  const uint8_t expected_old[6] = {
      0x2Au, 0x15u, 0xE3u, 0x78u, 0x8Fu, 0x23u,
  };
  const uint8_t expected_replacement[6] = {
      0x2Au, 0x15u, 0xE3u, 0x52u, 0x87u, 0x23u,
  };
  uint8_t old_mac[6]{};
  uint8_t replacement_mac[6]{};

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_cyw43_make_laa_mac_from_uid(old_uid, old_mac));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_cyw43_make_laa_mac_from_uid(replacement_uid, replacement_mac));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_old, old_mac, sizeof(old_mac));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_replacement, replacement_mac,
                                sizeof(replacement_mac));
  TEST_ASSERT_EQUAL_UINT8(0u, old_mac[0] & 0x01u);
  TEST_ASSERT_EQUAL_UINT8(0x02u, old_mac[0] & 0x02u);
  TEST_ASSERT_FALSE(memcmp(old_mac, replacement_mac, sizeof(old_mac)) == 0);
}

static void test_fallback_mac_rejects_null_arguments(void) {
  uint8_t uid[HAL_DEVICE_UID_BYTES]{};
  uint8_t mac[6]{};

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_cyw43_make_laa_mac_from_uid(nullptr, mac));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_cyw43_make_laa_mac_from_uid(uid, nullptr));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_stage_vocabulary_is_bounded);
  RUN_TEST(test_non_stm32_build_has_no_driver_instance);
  RUN_TEST(test_fallback_mac_uses_unique_uid_suffix);
  RUN_TEST(test_fallback_mac_rejects_null_arguments);
  return UNITY_END();
}
