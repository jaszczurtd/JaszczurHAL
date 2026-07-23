#include "hal/impl/shared/network/ota/jh_ota_protocol.h"
#include "utils/unity.h"

#include <cstring>

void setUp(void) {}
void tearDown(void) {}

void test_invitation_parser_accepts_sketch_and_filesystem_commands(void) {
  const char sketch[] = "0 3232 4096 0123456789abcdef0123456789ABCDEF\n";
  jh_ota_invitation_t invitation{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_ota_parse_invitation(reinterpret_cast<const uint8_t *>(sketch),
                                      std::strlen(sketch), &invitation));
  TEST_ASSERT_EQUAL_UINT16(0u, invitation.command);
  TEST_ASSERT_EQUAL_UINT16(3232u, invitation.tcp_port);
  TEST_ASSERT_EQUAL_UINT32(4096u, invitation.image_size);
  TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef",
                           invitation.image_md5);

  const char filesystem[] = "100 65535 1 fedcba9876543210fedcba9876543210\r\n";
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      jh_ota_parse_invitation(reinterpret_cast<const uint8_t *>(filesystem),
                              std::strlen(filesystem), &invitation));
  TEST_ASSERT_EQUAL_UINT16(100u, invitation.command);
  TEST_ASSERT_EQUAL_UINT16(65535u, invitation.tcp_port);
  TEST_ASSERT_EQUAL_UINT32(1u, invitation.image_size);
}

void test_invitation_parser_rejects_bad_shape_and_ranges(void) {
  const char *invalid[] = {
      "200 3232 4096 0123456789abcdef0123456789abcdef\n",
      "0 0 4096 0123456789abcdef0123456789abcdef\n",
      "0 65536 4096 0123456789abcdef0123456789abcdef\n",
      "0 3232 0 0123456789abcdef0123456789abcdef\n",
      "0 3232 4096 short\n",
      "0 3232 4096 0123456789abcdef0123456789abcdeg\n",
      "0 3232 4096 0123456789abcdef0123456789abcdef junk\n",
  };
  for (const char *message : invalid) {
    jh_ota_invitation_t invitation{};
    TEST_ASSERT_EQUAL_INT(
        HAL_EINVAL,
        jh_ota_parse_invitation(reinterpret_cast<const uint8_t *>(message),
                                std::strlen(message), &invitation));
  }
}

void test_auth_parser_and_constant_shape_comparison(void) {
  const char auth[] = "200 0123456789abcdef0123456789ABCDEF "
                      "fedcba9876543210fedcba9876543210\n";
  jh_ota_auth_response_t response{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ota_parse_auth_response(
                                    reinterpret_cast<const uint8_t *>(auth),
                                    std::strlen(auth), &response));
  TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef",
                           response.client_nonce);
  TEST_ASSERT_EQUAL_STRING("fedcba9876543210fedcba9876543210",
                           response.response);
  TEST_ASSERT_TRUE(
      jh_ota_hex_equal(response.response, "fedcba9876543210fedcba9876543210"));
  TEST_ASSERT_FALSE(
      jh_ota_hex_equal(response.response, "fedcba9876543210fedcba9876543211"));
  TEST_ASSERT_FALSE(jh_ota_hex_equal(response.response, "short"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_invitation_parser_accepts_sketch_and_filesystem_commands);
  RUN_TEST(test_invitation_parser_rejects_bad_shape_and_ranges);
  RUN_TEST(test_auth_parser_and_constant_shape_comparison);
  return UNITY_END();
}
