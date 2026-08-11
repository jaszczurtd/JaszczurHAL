#include "hal/security/hal_sc_auth.h"
#include "utils/unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_sc_auth_matches_the_protocol_vectors(void) {
  static const uint8_t uid[HAL_DEVICE_UID_BYTES] = {
      0xE6u, 0x61u, 0xA4u, 0xD1u, 0x23u, 0x45u, 0x67u, 0xABu,
  };
  static const uint8_t challenge[HAL_SC_AUTH_CHALLENGE_BYTES] = {
      0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u,
      0x08u, 0x09u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x0Eu, 0x0Fu,
  };
  static const uint8_t expected_key[HAL_SC_AUTH_KEY_BYTES] = {
      0xD2u, 0xFDu, 0x28u, 0x70u, 0xBEu, 0x45u, 0x6Au, 0x23u,
      0xCDu, 0xA6u, 0x7Eu, 0x28u, 0x81u, 0x4Du, 0xAAu, 0xD7u,
      0xD5u, 0x39u, 0xAFu, 0x6Eu, 0x39u, 0x54u, 0x3Fu, 0xDAu,
      0xF9u, 0x15u, 0x0Bu, 0x86u, 0xF5u, 0xBCu, 0x6Eu, 0xAAu,
  };
  static const uint8_t expected_response[HAL_SC_AUTH_RESPONSE_BYTES] = {
      0x92u, 0x64u, 0x61u, 0xB6u, 0x42u, 0xBDu, 0xD3u, 0xE6u,
      0x36u, 0x76u, 0x28u, 0x7Eu, 0xF3u, 0xCFu, 0xDFu, 0xB3u,
      0xB8u, 0xD3u, 0xD5u, 0x73u, 0x16u, 0xDEu, 0x6Eu, 0x84u,
      0xB5u, 0xA4u, 0xE2u, 0x41u, 0x7Au, 0x9Du, 0xACu, 0x8Au,
  };
  uint8_t key[HAL_SC_AUTH_KEY_BYTES] = {0u};
  uint8_t response[HAL_SC_AUTH_RESPONSE_BYTES] = {0u};

  TEST_ASSERT_TRUE(hal_sc_auth_derive_device_key(uid, sizeof(uid), key));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_key, key, sizeof(key));
  TEST_ASSERT_TRUE(hal_sc_auth_compute_response(
      key, challenge, sizeof(challenge), 0x12345678u, response));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_response, response, sizeof(response));
  TEST_ASSERT_TRUE(
      hal_sc_auth_macs_equal(expected_response, response, sizeof(response)));
}

void test_sc_auth_invalid_input_fails_with_cleared_output(void) {
  uint8_t output[HAL_SC_AUTH_RESPONSE_BYTES];
  memset(output, 0xA5, sizeof(output));

  TEST_ASSERT_FALSE(
      hal_sc_auth_compute_response(nullptr, nullptr, 0u, 0u, output));
  const uint8_t cleared[sizeof(output)] = {0u};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(cleared, output, sizeof(output));
  TEST_ASSERT_FALSE(hal_sc_auth_macs_equal(nullptr, cleared, sizeof(cleared)));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_sc_auth_matches_the_protocol_vectors);
  RUN_TEST(test_sc_auth_invalid_input_fails_with_cleared_output);
  return UNITY_END();
}
