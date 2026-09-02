#include "hal/bluetooth/jh_gamepad_bond_codec.h"

#include <cstring>

#include "utils/unity.h"

namespace {

jh_gamepad_bond_identity_t make_identity(uint8_t addr_seed, uint8_t key_seed) {
  jh_gamepad_bond_identity_t identity{};
  for (size_t i = 0; i < sizeof(identity.bd_addr); ++i) {
    identity.bd_addr[i] = (uint8_t)(addr_seed + i);
  }
  for (size_t i = 0; i < sizeof(identity.link_key); ++i) {
    identity.link_key[i] = (uint8_t)(key_seed + i);
  }
  identity.link_key_type = 4u;
  return identity;
}

void test_round_trip_preserves_identity_and_sequence() {
  const jh_gamepad_bond_identity_t identity = make_identity(0x10u, 0x80u);
  hal_gamepad_bond_blob_t blob{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_gamepad_bond_encode(&identity, 42u, &blob));

  jh_gamepad_bond_identity_t decoded{};
  uint32_t sequence = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_gamepad_bond_decode(&blob, &decoded, &sequence));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(identity.bd_addr, decoded.bd_addr,
                                sizeof(identity.bd_addr));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(identity.link_key, decoded.link_key,
                                sizeof(identity.link_key));
  TEST_ASSERT_EQUAL_UINT8(identity.link_key_type, decoded.link_key_type);
  TEST_ASSERT_EQUAL_UINT32(42u, sequence);
}

void test_decode_accepts_null_sequence_out_param() {
  const jh_gamepad_bond_identity_t identity = make_identity(1u, 1u);
  hal_gamepad_bond_blob_t blob{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_gamepad_bond_encode(&identity, 7u, &blob));

  jh_gamepad_bond_identity_t decoded{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_gamepad_bond_decode(&blob, &decoded, nullptr));
}

void test_decode_rejects_null_arguments() {
  hal_gamepad_bond_blob_t blob{};
  jh_gamepad_bond_identity_t decoded{};
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_gamepad_bond_decode(nullptr, &decoded, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_gamepad_bond_decode(&blob, nullptr, nullptr));
  const jh_gamepad_bond_identity_t identity = make_identity(2u, 2u);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, jh_gamepad_bond_encode(nullptr, 0u, &blob));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_gamepad_bond_encode(&identity, 0u, nullptr));
}

void test_decode_rejects_corrupted_bytes() {
  const jh_gamepad_bond_identity_t identity = make_identity(3u, 3u);
  hal_gamepad_bond_blob_t blob{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_gamepad_bond_encode(&identity, 1u, &blob));

  hal_gamepad_bond_blob_t corrupted = blob;
  corrupted.bytes[0] ^= 0xFFu; /* corrupt magic */
  jh_gamepad_bond_identity_t decoded{};
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_gamepad_bond_decode(&corrupted, &decoded, nullptr));

  corrupted = blob;
  corrupted.bytes[20] ^= 0x01u; /* corrupt a link-key byte -> CRC mismatch */
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_gamepad_bond_decode(&corrupted, &decoded, nullptr));
}

void test_decode_rejects_zeroed_blob() {
  hal_gamepad_bond_blob_t blob{};
  std::memset(&blob, 0, sizeof(blob));
  jh_gamepad_bond_identity_t decoded{};
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_gamepad_bond_decode(&blob, &decoded, nullptr));
}

} // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_round_trip_preserves_identity_and_sequence);
  RUN_TEST(test_decode_accepts_null_sequence_out_param);
  RUN_TEST(test_decode_rejects_null_arguments);
  RUN_TEST(test_decode_rejects_corrupted_bytes);
  RUN_TEST(test_decode_rejects_zeroed_blob);
  return UNITY_END();
}
