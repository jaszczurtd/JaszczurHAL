#include "hal/bluetooth/jh_gamepad_bond_kv_provider.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/storage/hal_eeprom.h"
#include "hal/storage/hal_kv.h"
#include "utils/unity.h"

#include <cstring>

namespace {

constexpr uint16_t kBondKey = 900u;

hal_gamepad_bond_blob_t make_blob(uint8_t fill) {
  hal_gamepad_bond_blob_t blob{};
  std::memset(blob.bytes, fill, sizeof(blob.bytes));
  return blob;
}

} // namespace

void setUp(void) {
  hal_mock_eeprom_reset();
  hal_mock_kv_full_reset();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_eeprom_init(HAL_EEPROM_FLASH, 8192u, 0x50u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(0u, 8192u));
}

void tearDown(void) {
  hal_mock_kv_full_reset();
  hal_mock_eeprom_reset();
}

void test_load_reports_no_bond_before_any_store(void) {
  const hal_gamepad_bond_provider_t provider =
      jh_gamepad_bond_kv_provider(kBondKey);
  hal_gamepad_bond_blob_t blob{};
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, provider.load(provider.context, &blob));
}

void test_store_then_load_round_trips_the_blob(void) {
  const hal_gamepad_bond_provider_t provider =
      jh_gamepad_bond_kv_provider(kBondKey);
  const hal_gamepad_bond_blob_t stored = make_blob(0x5Au);
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.store(provider.context, &stored));

  hal_gamepad_bond_blob_t loaded{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.load(provider.context, &loaded));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(stored.bytes, loaded.bytes,
                                sizeof(stored.bytes));
}

void test_store_replaces_previous_blob(void) {
  const hal_gamepad_bond_provider_t provider =
      jh_gamepad_bond_kv_provider(kBondKey);
  const hal_gamepad_bond_blob_t first = make_blob(0x11u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.store(provider.context, &first));
  const hal_gamepad_bond_blob_t second = make_blob(0x22u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.store(provider.context, &second));

  hal_gamepad_bond_blob_t loaded{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.load(provider.context, &loaded));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(second.bytes, loaded.bytes,
                                sizeof(second.bytes));
}

void test_erase_removes_the_blob_and_is_idempotent(void) {
  const hal_gamepad_bond_provider_t provider =
      jh_gamepad_bond_kv_provider(kBondKey);
  const hal_gamepad_bond_blob_t stored = make_blob(0x33u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.store(provider.context, &stored));

  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.erase(provider.context));
  hal_gamepad_bond_blob_t blob{};
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, provider.load(provider.context, &blob));

  /* Erasing an already-absent bond is not an error (factory reset). */
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.erase(provider.context));
}

void test_different_keys_do_not_collide(void) {
  const hal_gamepad_bond_provider_t provider_a =
      jh_gamepad_bond_kv_provider(kBondKey);
  const hal_gamepad_bond_provider_t provider_b =
      jh_gamepad_bond_kv_provider((uint16_t)(kBondKey + 1u));
  const hal_gamepad_bond_blob_t stored_a = make_blob(0xAAu);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        provider_a.store(provider_a.context, &stored_a));

  hal_gamepad_bond_blob_t loaded_b{};
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        provider_b.load(provider_b.context, &loaded_b));

  hal_gamepad_bond_blob_t loaded_a{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider_a.load(provider_a.context, &loaded_a));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(stored_a.bytes, loaded_a.bytes,
                                sizeof(stored_a.bytes));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_load_reports_no_bond_before_any_store);
  RUN_TEST(test_store_then_load_round_trips_the_blob);
  RUN_TEST(test_store_replaces_previous_blob);
  RUN_TEST(test_erase_removes_the_blob_and_is_idempotent);
  RUN_TEST(test_different_keys_do_not_collide);
  return UNITY_END();
}
