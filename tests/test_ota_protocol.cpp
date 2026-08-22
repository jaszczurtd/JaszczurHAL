#include "hal/network/ota/jh_ota_protocol.h"
#include "hal/security/hal_crypto.h"
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
      " 0 3232 4096 0123456789abcdef0123456789abcdef\n",
      "0  3232 4096 0123456789abcdef0123456789abcdef\n",
      "0\t3232 4096 0123456789abcdef0123456789abcdef\n",
      "0 03232 4096 0123456789abcdef0123456789abcdef\n",
      "0 3232 04096 0123456789abcdef0123456789abcdef\n",
      "0 3232 4096 0123456789abcdef0123456789abcdef \n",
      "0 3232 4096 0123456789abcdef0123456789abcdef\n\n",
      "0 3232 4096 0123456789abcdef0123456789abcdef\r",
  };
  for (const char *message : invalid) {
    jh_ota_invitation_t invitation{};
    TEST_ASSERT_EQUAL_INT(
        HAL_EINVAL,
        jh_ota_parse_invitation(reinterpret_cast<const uint8_t *>(message),
                                std::strlen(message), &invitation));
  }

  const uint8_t embedded_nul[] = {
      '0', ' ', '3', '2', '3', '2', ' ', '1', ' ', '0', '1', '2', '3', '4',
      '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f', '0', '1', '2',
      '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f', '\0'};
  jh_ota_invitation_t invitation{};
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      jh_ota_parse_invitation(embedded_nul, sizeof(embedded_nul), &invitation));
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

  const char *noncanonical[] = {
      " 201 0123456789abcdef0123456789abcdef "
      "fedcba9876543210fedcba98765432100123456789abcdef0123456789abcdef\n",
      "201  0123456789abcdef0123456789abcdef "
      "fedcba9876543210fedcba98765432100123456789abcdef0123456789abcdef\n",
      "0201 0123456789abcdef0123456789abcdef "
      "fedcba9876543210fedcba98765432100123456789abcdef0123456789abcdef\n",
      "201 0123456789abcdef0123456789abcdef\t"
      "fedcba9876543210fedcba98765432100123456789abcdef0123456789abcdef\n",
      "201 0123456789abcdef0123456789abcdef "
      "fedcba9876543210fedcba98765432100123456789abcdef0123456789abcdef "
      "\n",
  };
  for (const char *message : noncanonical) {
    TEST_ASSERT_EQUAL_INT(
        HAL_EINVAL,
        jh_ota_parse_auth_response(reinterpret_cast<const uint8_t *>(message),
                                   std::strlen(message), &response));
  }
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

void test_auth2_hmac_matches_host_vector(void) {
  jh_ota_invitation_t invitation{};
  invitation.command = 0u;
  invitation.tcp_port = 3232u;
  invitation.image_size = 4096u;
  std::memcpy(invitation.image_md5, "0123456789abcdef0123456789abcdef",
              JH_OTA_MD5_HEX_BUFFER_SIZE);
  char transcript[JH_OTA_AUTH_TRANSCRIPT_BUFFER_SIZE]{};
  size_t transcript_length = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ota_format_auth_transcript(
                            &invitation, "00112233445566778899aabbccddeeff",
                            "ffeeddccbbaa99887766554433221100", transcript,
                            sizeof(transcript), &transcript_length));

  static const char password[] = "correct horse battery staple";
  char password_md5[HAL_MD5_HEX_BUF_SIZE]{};
  char tag[HAL_SHA256_HEX_BUF_SIZE]{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ota_derive_password_key(password, password_md5));
  TEST_ASSERT_EQUAL_STRING("9cc2ae8a1ba7a93da39b46fc1019c481", password_md5);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ota_derive_password_key("", password_md5));
  TEST_ASSERT_EQUAL_STRING("d41d8cd98f00b204e9800998ecf8427e", password_md5);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ota_derive_password_key(password, password_md5));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_ota_derive_password_key(nullptr, password_md5));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_ota_derive_password_key(password, nullptr));
  TEST_ASSERT_TRUE(hal_hmac_sha256_hex(
      reinterpret_cast<const uint8_t *>(password_md5),
      std::strlen(password_md5), reinterpret_cast<const uint8_t *>(transcript),
      transcript_length, tag, sizeof(tag)));
  TEST_ASSERT_EQUAL_STRING(
      "c704cfc163213195568901d2399b8434e8e199d221fbfa82e83e2bfd8446bdf1", tag);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_invitation_parser_accepts_sketch_and_filesystem_commands);
  RUN_TEST(test_invitation_parser_rejects_bad_shape_and_ranges);
  RUN_TEST(test_auth_parser_and_constant_shape_comparison);
  RUN_TEST(test_auth_transcript_binds_complete_invitation);
  RUN_TEST(test_auth_endpoint_comparison_binds_address_and_source_port);
  RUN_TEST(test_auth2_hmac_matches_host_vector);
  return UNITY_END();
}
