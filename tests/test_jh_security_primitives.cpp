#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/jh_secure_random.h"
#include "utils/unity.h"

#include <string.h>

void setUp(void) { hal_mock_secure_random_reset(); }
void tearDown(void) {}

void test_secure_zeroize_erases_the_complete_buffer(void) {
  uint8_t buffer[31];
  memset(buffer, 0xA5, sizeof(buffer));

  jh_secure_zeroize(buffer, sizeof(buffer));

  const uint8_t expected[sizeof(buffer)] = {0u};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buffer, sizeof(buffer));
  jh_secure_zeroize(nullptr, 0u);
}

void test_constant_time_compare_reports_matches_and_mismatches(void) {
  const uint8_t reference[] = {0x10u, 0x20u, 0x30u, 0x40u};
  uint8_t candidate[sizeof(reference)];
  memcpy(candidate, reference, sizeof(candidate));

  TEST_ASSERT_TRUE(
      jh_constant_time_compare(reference, candidate, sizeof(reference)));
  candidate[0] ^= 0x01u;
  TEST_ASSERT_FALSE(
      jh_constant_time_compare(reference, candidate, sizeof(reference)));
  candidate[0] = reference[0];
  candidate[sizeof(candidate) - 1u] ^= 0x01u;
  TEST_ASSERT_FALSE(
      jh_constant_time_compare(reference, candidate, sizeof(reference)));
  TEST_ASSERT_FALSE(
      jh_constant_time_compare(nullptr, candidate, sizeof(candidate)));
  TEST_ASSERT_TRUE(jh_constant_time_compare(reference, candidate, 0u));
}

void test_mock_random_matches_the_fixed_seed_vector(void) {
  static const uint8_t expected[32] = {
      0x7Cu, 0xD5u, 0x88u, 0x5Eu, 0xBBu, 0xEDu, 0x2Bu, 0x95u,
      0xBDu, 0x23u, 0x47u, 0x6Fu, 0x28u, 0x9Fu, 0x97u, 0xDAu,
      0xC5u, 0x3Du, 0x2Cu, 0x73u, 0x62u, 0x39u, 0x5Eu, 0x37u,
      0x3Du, 0x4Eu, 0x62u, 0x8Fu, 0x3Eu, 0x65u, 0x96u, 0x76u,
  };
  uint8_t actual[sizeof(expected)] = {0u};
  hal_mock_secure_random_set_seed(0x0123456789ABCDEFull);
  hal_mock_secure_random_set_status(HAL_OK);

  TEST_ASSERT_EQUAL(HAL_OK, jh_secure_random_bytes(actual, sizeof(actual)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, sizeof(expected));
}

void test_random_failure_zeroes_output(void) {
  uint8_t output[16];
  memset(output, 0xA5, sizeof(output));
  hal_mock_secure_random_set_status(HAL_EUNSUPPORTED);

  TEST_ASSERT_EQUAL(HAL_EUNSUPPORTED,
                    jh_secure_random_bytes(output, sizeof(output)));
  const uint8_t expected[sizeof(output)] = {0u};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, output, sizeof(output));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_secure_zeroize_erases_the_complete_buffer);
  RUN_TEST(test_constant_time_compare_reports_matches_and_mismatches);
  RUN_TEST(test_mock_random_matches_the_fixed_seed_vector);
  RUN_TEST(test_random_failure_zeroes_output);
  return UNITY_END();
}
