#include "hal/network/ota/jh_ota_protocol.h"
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
  const char auth[] = "201 0123456789abcdef0123456789ABCDEF "
                      "fedcba9876543210fedcba9876543210"
                      "0123456789abcdef0123456789abcdef\n";
  jh_ota_auth_response_t response{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ota_parse_auth_response(
                                    reinterpret_cast<const uint8_t *>(auth),
                                    std::strlen(auth), &response));
  TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef",
                           response.client_nonce);
  TEST_ASSERT_EQUAL_STRING("fedcba9876543210fedcba9876543210"
                           "0123456789abcdef0123456789abcdef",
                           response.response);
  TEST_ASSERT_TRUE(jh_ota_auth_tag_equal(response.response,
                                         "fedcba9876543210fedcba9876543210"
                                         "0123456789abcdef0123456789abcdef"));
  TEST_ASSERT_FALSE(jh_ota_auth_tag_equal(response.response,
                                          "fedcba9876543210fedcba9876543210"
                                          "0123456789abcdef0123456789abcdee"));
  TEST_ASSERT_FALSE(jh_ota_auth_tag_equal(response.response, "short"));

  const char legacy[] = "200 0123456789abcdef0123456789abcdef "
                        "fedcba9876543210fedcba9876543210\n";
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      jh_ota_parse_auth_response(reinterpret_cast<const uint8_t *>(legacy),
                                 std::strlen(legacy), &response));

  const char truncated[] = "201 0123456789abcdef0123456789abcdef "
                           "fedcba9876543210fedcba9876543210\n";
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      jh_ota_parse_auth_response(reinterpret_cast<const uint8_t *>(truncated),
                                 std::strlen(truncated), &response));
}

void test_auth_transcript_binds_complete_invitation(void) {
  const char invitation_text[] =
      "0 3232 4096 0123456789abcdef0123456789abcdef\n";
  jh_ota_invitation_t invitation{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ota_parse_invitation(
                            reinterpret_cast<const uint8_t *>(invitation_text),
                            std::strlen(invitation_text), &invitation));
  std::memcpy(invitation.image_md5, "0123456789ABCDEF0123456789ABCDEF",
              JH_OTA_MD5_HEX_BUFFER_SIZE);
  char transcript[JH_OTA_AUTH_TRANSCRIPT_BUFFER_SIZE]{};
  size_t length = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ota_format_auth_transcript(
                            &invitation, "00112233445566778899AABBCCDDEEFF",
                            "FFEEDDCCBBAA99887766554433221100", transcript,
                            sizeof(transcript), &length));
  TEST_ASSERT_EQUAL_STRING("JHOTA-AUTH-2:0:3232:4096:"
                           "0123456789abcdef0123456789abcdef:"
                           "00112233445566778899aabbccddeeff:"
                           "ffeeddccbbaa99887766554433221100",
                           transcript);
  TEST_ASSERT_EQUAL_UINT32(std::strlen(transcript), length);

  char short_output[8] = {'x'};
  length = 99u;
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        jh_ota_format_auth_transcript(
                            &invitation, "00112233445566778899aabbccddeeff",
                            "ffeeddccbbaa99887766554433221100", short_output,
                            sizeof(short_output), &length));
  TEST_ASSERT_EQUAL_CHAR('\0', short_output[0]);
  TEST_ASSERT_EQUAL_UINT32(0u, length);

  invitation.command = 1u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_ota_format_auth_transcript(
                            &invitation, "00112233445566778899aabbccddeeff",
                            "ffeeddccbbaa99887766554433221100", transcript,
                            sizeof(transcript), &length));
  invitation.command = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_ota_format_auth_transcript(
                            &invitation, "00112233445566778899aabbccddeefg",
                            "ffeeddccbbaa99887766554433221100", transcript,
                            sizeof(transcript), &length));
}

void test_auth_endpoint_comparison_binds_address_and_source_port(void) {
  hal_net_endpoint_t invitation_source{};
  invitation_source.family = HAL_NET_AF_INET;
  invitation_source.addr_len = HAL_NET_IPV4_ADDR_LEN;
  invitation_source.addr[0] = 192u;
  invitation_source.addr[1] = 0u;
  invitation_source.addr[2] = 2u;
  invitation_source.addr[3] = 42u;
  invitation_source.port = 49152u;

  hal_net_endpoint_t auth_source = invitation_source;
  TEST_ASSERT_TRUE(jh_ota_endpoint_equal(&invitation_source, &auth_source));
  auth_source.port = 49153u;
  TEST_ASSERT_FALSE(jh_ota_endpoint_equal(&invitation_source, &auth_source));
  auth_source = invitation_source;
  auth_source.addr[3] = 43u;
  TEST_ASSERT_FALSE(jh_ota_endpoint_equal(&invitation_source, &auth_source));
  TEST_ASSERT_FALSE(jh_ota_endpoint_equal(nullptr, &auth_source));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_invitation_parser_accepts_sketch_and_filesystem_commands);
  RUN_TEST(test_invitation_parser_rejects_bad_shape_and_ranges);
  RUN_TEST(test_auth_parser_and_constant_shape_comparison);
  RUN_TEST(test_auth_transcript_binds_complete_invitation);
  RUN_TEST(test_auth_endpoint_comparison_binds_address_and_source_port);
  return UNITY_END();
}
