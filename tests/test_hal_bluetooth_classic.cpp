#include "hal/bluetooth/hal_bluetooth_classic.h"
#include "hal/bluetooth/jh_bluetooth_classic_address.h"
#include "hal/bluetooth/jh_bluetooth_classic_bond_codec.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <string.h>

namespace {

hal_bluetooth_classic_t s_classic = nullptr;
hal_bluetooth_classic_bond_blob_t s_records[2]{};
bool s_record_used[2]{};
uint32_t s_store_calls = 0u;
uint32_t s_erase_calls = 0u;

hal_bluetooth_classic_address_t address(uint8_t tail) {
  hal_bluetooth_classic_address_t value{
      {0x28u, 0xcdu, 0xc1u, 0x14u, 0x90u, tail}};
  return value;
}

hal_status_t load_record(void *, size_t index,
                         hal_bluetooth_classic_bond_blob_t *out_blob) {
  if (index >= 2u || out_blob == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_record_used[index]) {
    return HAL_ENOENT;
  }
  *out_blob = s_records[index];
  return HAL_OK;
}

hal_status_t store_record(void *, size_t index,
                          const hal_bluetooth_classic_bond_blob_t *blob) {
  if (index >= 2u || blob == nullptr) {
    return HAL_EINVAL;
  }
  s_records[index] = *blob;
  s_record_used[index] = true;
  ++s_store_calls;
  return HAL_OK;
}

hal_status_t erase_record(void *, size_t index) {
  if (index >= 2u) {
    return HAL_EINVAL;
  }
  memset(&s_records[index], 0, sizeof(s_records[index]));
  s_record_used[index] = false;
  ++s_erase_calls;
  return HAL_OK;
}

hal_bluetooth_classic_bond_provider_t provider() {
  hal_bluetooth_classic_bond_provider_t value{};
  value.capacity = 2u;
  value.load = load_record;
  value.store = store_record;
  value.erase = erase_record;
  return value;
}

void open_ready(
    const hal_bluetooth_classic_bond_provider_t *bond_provider = nullptr) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_bluetooth_classic_open_ex(&s_classic, bond_provider));
  TEST_ASSERT_NOT_NULL(s_classic);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_classic_inject_ready());
}

void authorize(const hal_bluetooth_classic_address_t &peer) {
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_bluetooth_classic_inject_pairing_request(
                            &peer, HAL_BLUETOOTH_CLASSIC_PAIRING_JUST_WORKS));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_pairing_authorize(s_classic));
}

} // namespace

void setUp(void) {
  if (s_classic != nullptr) {
    (void)hal_bluetooth_classic_close(s_classic);
  }
  s_classic = nullptr;
  hal_mock_bluetooth_classic_reset();
  hal_mock_bluetooth_classic_runtime_full_reset();
  memset(s_records, 0, sizeof(s_records));
  memset(s_record_used, 0, sizeof(s_record_used));
  s_store_calls = 0u;
  s_erase_calls = 0u;
}

void tearDown(void) {
  if (s_classic != nullptr) {
    (void)hal_bluetooth_classic_close(s_classic);
  }
  s_classic = nullptr;
  hal_mock_bluetooth_classic_reset();
  hal_mock_bluetooth_classic_runtime_full_reset();
}

void test_lifecycle_is_independent_from_gamepad(void) {
  hal_bluetooth_classic_t second = nullptr;
  hal_bluetooth_classic_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_bluetooth_classic_open(nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_classic_open(&s_classic));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_bluetooth_classic_open(&second));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_get_info(s_classic, &info));
  TEST_ASSERT_EQUAL_INT(HAL_BLUETOOTH_CLASSIC_STATE_STARTING, info.state);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_classic_inject_ready());
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_get_info(s_classic, &info));
  TEST_ASSERT_EQUAL_INT(HAL_BLUETOOTH_CLASSIC_STATE_READY, info.state);

  const hal_bluetooth_classic_t stale = s_classic;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_classic_close(s_classic));
  s_classic = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_bluetooth_classic_poll(stale));
}

void test_scan_results_are_bounded_and_copied(void) {
  open_ready();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_scan_start(s_classic, 5000u));
  for (size_t index = 0u; index < HAL_BLUETOOTH_CLASSIC_SCAN_QUEUE_DEPTH + 1u;
       ++index) {
    hal_bluetooth_classic_scan_result_t result{};
    result.address = address((uint8_t)index);
    result.class_of_device = 0x0508u;
    result.rssi = (int8_t)(-40 - (int)index);
    result.rssi_valid = true;
    memcpy(result.name, "Classic peer", 12u);
    result.name_length = 12u;
    TEST_ASSERT_EQUAL_INT(
        HAL_OK, hal_mock_bluetooth_classic_inject_scan_result(&result));
    memset(result.name, 'X', sizeof(result.name));
  }
  hal_bluetooth_classic_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_get_info(s_classic, &info));
  TEST_ASSERT_EQUAL_UINT32(1u, info.dropped_scan_results);
  TEST_ASSERT_EQUAL_UINT(HAL_BLUETOOTH_CLASSIC_SCAN_QUEUE_DEPTH,
                         info.pending_scan_results);

  hal_bluetooth_classic_scan_result_t result{};
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, hal_bluetooth_classic_scan_result_next(
                                           s_classic, &result));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_bluetooth_classic_scan_result_next(s_classic, &result));
  TEST_ASSERT_EQUAL_STRING("Classic peer", result.name);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_classic_scan_stop(s_classic));
}

void test_pairing_can_be_authorized_or_rejected_explicitly(void) {
  open_ready();
  const auto peer = address(0x42u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_bluetooth_classic_inject_pairing_request(
                            &peer, HAL_BLUETOOTH_CLASSIC_PAIRING_JUST_WORKS));
  hal_bluetooth_classic_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_get_info(s_classic, &info));
  TEST_ASSERT_TRUE(info.pairing_pending);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_pairing_authorize(s_classic));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        hal_bluetooth_classic_pairing_authorize(s_classic));

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_bluetooth_classic_inject_pairing_request(
                            &peer, HAL_BLUETOOTH_CLASSIC_PAIRING_PIN));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_pairing_reject(s_classic));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_get_info(s_classic, &info));
  TEST_ASSERT_FALSE(info.pairing_pending);
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH, info.last_status);
}

void test_indexed_bond_provider_owns_one_link_key_copy_per_peer(void) {
  const auto bond_provider = provider();
  open_ready(&bond_provider);
  const auto first = address(0x11u);
  const auto second = address(0x22u);
  const uint8_t first_key[16] = {1u};
  const uint8_t second_key[16] = {2u};

  authorize(first);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_classic_inject_link_key(
                                    &first, first_key, 4u));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_bluetooth_classic_peer_save(s_classic, &first, 0x1001u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_classic_poll(s_classic));
  authorize(second);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_classic_inject_link_key(
                                    &second, second_key, 5u));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_bluetooth_classic_peer_save(s_classic, &second, 0x1002u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_bluetooth_classic_poll(s_classic));
  TEST_ASSERT_EQUAL_UINT32(2u, s_store_calls);

  size_t count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_peer_count(s_classic, &count));
  TEST_ASSERT_EQUAL_UINT(2u, count);
  jh_bluetooth_classic_bond_identity_t decoded{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_classic_bond_decode(&s_records[0], &decoded));
  TEST_ASSERT_EQUAL_UINT16(0x1001u, decoded.profile_id);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(first_key, decoded.link_key, sizeof(first_key));

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_bluetooth_classic_peer_forget(s_classic, &first));
  TEST_ASSERT_EQUAL_UINT32(1u, s_erase_calls);
  TEST_ASSERT_FALSE(s_record_used[0]);
}

void test_address_format_is_stable(void) {
  char text[HAL_BLUETOOTH_CLASSIC_ADDRESS_TEXT_SIZE]{};
  const auto peer = address(0xf8u);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_bluetooth_classic_format_address(&peer, text, sizeof(text)));
  TEST_ASSERT_EQUAL_STRING("28:CD:C1:14:90:F8", text);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_bluetooth_classic_format_address(
                                        &peer, text, sizeof(text) - 1u));
}

void test_address_helpers_compare_values_and_reject_null(void) {
  const auto first = address(0x11u);
  const auto same = address(0x11u);
  const auto different = address(0x22u);
  const hal_bluetooth_classic_address_t zero{};

  TEST_ASSERT_TRUE(jh_bluetooth_classic_address_equal(&first, &same));
  TEST_ASSERT_FALSE(jh_bluetooth_classic_address_equal(&first, &different));
  TEST_ASSERT_FALSE(jh_bluetooth_classic_address_equal(&first, nullptr));
  TEST_ASSERT_FALSE(jh_bluetooth_classic_address_equal(nullptr, &same));
  TEST_ASSERT_TRUE(jh_bluetooth_classic_address_is_zero(&zero));
  TEST_ASSERT_FALSE(jh_bluetooth_classic_address_is_zero(&first));
  TEST_ASSERT_FALSE(jh_bluetooth_classic_address_is_zero(nullptr));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_lifecycle_is_independent_from_gamepad);
  RUN_TEST(test_scan_results_are_bounded_and_copied);
  RUN_TEST(test_pairing_can_be_authorized_or_rejected_explicitly);
  RUN_TEST(test_indexed_bond_provider_owns_one_link_key_copy_per_peer);
  RUN_TEST(test_address_format_is_stable);
  RUN_TEST(test_address_helpers_compare_values_and_reject_null);
  return UNITY_END();
}
