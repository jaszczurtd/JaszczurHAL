#include "hal/bluetooth/jh_bluetooth_classic_bond_kv_provider.h"
#include "hal/bluetooth/jh_gamepad_bond_kv_provider.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/storage/hal_eeprom.h"
#include "hal/storage/hal_kv.h"
#include "utils/unity.h"

#include <cstring>

namespace {

constexpr uint16_t kBondKey = 900u;
jh_gamepad_bond_kv_context_t s_context{};

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
  s_context = {};
}

void tearDown(void) {
  hal_mock_kv_full_reset();
  hal_mock_eeprom_reset();
}

void test_load_reports_no_bond_before_any_store(void) {
  const hal_gamepad_bond_provider_t provider =
      jh_gamepad_bond_kv_provider(&s_context, kBondKey);
  hal_gamepad_bond_blob_t blob{};
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, provider.load(provider.context, &blob));
}

void test_store_then_load_round_trips_the_blob(void) {
  const hal_gamepad_bond_provider_t provider =
      jh_gamepad_bond_kv_provider(&s_context, kBondKey);
  const hal_gamepad_bond_blob_t stored = make_blob(0x5Au);
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.store(provider.context, &stored));

  hal_gamepad_bond_blob_t loaded{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.load(provider.context, &loaded));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(stored.bytes, loaded.bytes,
                                sizeof(stored.bytes));
}

void test_store_replaces_previous_blob(void) {
  const hal_gamepad_bond_provider_t provider =
      jh_gamepad_bond_kv_provider(&s_context, kBondKey);
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
      jh_gamepad_bond_kv_provider(&s_context, kBondKey);
  const hal_gamepad_bond_blob_t stored = make_blob(0x33u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.store(provider.context, &stored));

  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.erase(provider.context));
  hal_gamepad_bond_blob_t blob{};
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, provider.load(provider.context, &blob));

  /* Erasing an already-absent bond is not an error (factory reset). */
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.erase(provider.context));
}

void test_different_keys_do_not_collide(void) {
  jh_gamepad_bond_kv_context_t context_a{};
  jh_gamepad_bond_kv_context_t context_b{};
  const hal_gamepad_bond_provider_t provider_a =
      jh_gamepad_bond_kv_provider(&context_a, kBondKey);
  const hal_gamepad_bond_provider_t provider_b =
      jh_gamepad_bond_kv_provider(&context_b, (uint16_t)(kBondKey + 1u));
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

void test_null_context_returns_an_empty_provider(void) {
  const hal_gamepad_bond_provider_t provider =
      jh_gamepad_bond_kv_provider(nullptr, kBondKey);
  TEST_ASSERT_NULL(provider.context);
  TEST_ASSERT_NULL(provider.load);
  TEST_ASSERT_NULL(provider.store);
  TEST_ASSERT_NULL(provider.erase);
}

void test_classic_provider_keeps_indexed_peer_slots_independent(void) {
  jh_bluetooth_classic_bond_kv_context_t context{};
  const hal_bluetooth_classic_bond_provider_t provider =
      jh_bluetooth_classic_bond_kv_provider(&context, kBondKey, 2u);
  TEST_ASSERT_EQUAL_UINT(2u, provider.capacity);

  const hal_bluetooth_classic_bond_blob_t first = make_blob(0x41u);
  const hal_bluetooth_classic_bond_blob_t second = make_blob(0x82u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.store(provider.context, 0u, &first));
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.store(provider.context, 1u, &second));

  hal_bluetooth_classic_bond_blob_t loaded{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.load(provider.context, 0u, &loaded));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(first.bytes, loaded.bytes, sizeof(first.bytes));
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider.load(provider.context, 1u, &loaded));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(second.bytes, loaded.bytes,
                                sizeof(second.bytes));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        provider.load(provider.context, 2u, &loaded));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_load_reports_no_bond_before_any_store);
  RUN_TEST(test_store_then_load_round_trips_the_blob);
  RUN_TEST(test_store_replaces_previous_blob);
  RUN_TEST(test_erase_removes_the_blob_and_is_idempotent);
  RUN_TEST(test_different_keys_do_not_collide);
  RUN_TEST(test_null_context_returns_an_empty_provider);
  RUN_TEST(test_classic_provider_keeps_indexed_peer_slots_independent);
  return UNITY_END();
}
