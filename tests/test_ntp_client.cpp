#include "hal/impl/shared/network/jh_ntp_client.h"
#include "utils/unity.h"

#include <cstring>

namespace {

constexpr uint64_t request_token = UINT64_C(0x4a484e5450000001);

void make_valid_fixture(uint8_t request[JH_NTP_PACKET_SIZE],
                        hal_net_endpoint_t *server,
                        uint8_t response[JH_NTP_PACKET_SIZE],
                        hal_net_endpoint_t *source) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ntp_prepare_request(request, request_token));
  server->family = HAL_NET_AF_INET;
  server->addr[0] = 192u;
  server->addr[1] = 0u;
  server->addr[2] = 2u;
  server->addr[3] = 1u;
  server->port = JH_NTP_PORT;
  *source = *server;
  std::memset(response, 0, JH_NTP_PACKET_SIZE);
  response[0] = 0x24u; /* Leap=0, version=4, server mode. */
  response[1] = 2u;
  std::memcpy(&response[24], &request[40], 8u);
  response[40] = 0xe9u;
  response[41] = 0x7du;
  response[42] = 0x8bu;
  response[43] = 0x80u;
  response[44] = 0x80u;
}

} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_ntp_accepts_matching_server_response_and_extracts_time(void) {
  uint8_t request[JH_NTP_PACKET_SIZE];
  uint8_t response[JH_NTP_PACKET_SIZE];
  hal_net_endpoint_t server = {};
  hal_net_endpoint_t source = {};
  make_valid_fixture(request, &server, response, &source);

  uint32_t seconds = 0u;
  uint32_t fraction = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ntp_validate_response(request, &server, response,
                                                 sizeof(response), &source,
                                                 &seconds, &fraction));
  TEST_ASSERT_EQUAL_HEX32(UINT32_C(0xe97d8b80), seconds);
  TEST_ASSERT_EQUAL_HEX32(UINT32_C(0x80000000), fraction);
}

void test_ntp_rejects_wrong_source_and_originate_timestamp(void) {
  uint8_t request[JH_NTP_PACKET_SIZE];
  uint8_t response[JH_NTP_PACKET_SIZE];
  hal_net_endpoint_t server = {};
  hal_net_endpoint_t source = {};
  make_valid_fixture(request, &server, response, &source);
  uint32_t seconds = 0u;
  uint32_t fraction = 0u;

  source.port = 124u;
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_ntp_validate_response(request, &server, response,
                                                 sizeof(response), &source,
                                                 &seconds, &fraction));
  source = server;
  source.addr[3] = 2u;
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_ntp_validate_response(request, &server, response,
                                                 sizeof(response), &source,
                                                 &seconds, &fraction));
  source = server;
  response[24] ^= 1u;
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_ntp_validate_response(request, &server, response,
                                                 sizeof(response), &source,
                                                 &seconds, &fraction));
}

void test_ntp_rejects_alarm_mode_stratum_truncation_and_zero_time(void) {
  uint8_t request[JH_NTP_PACKET_SIZE];
  uint8_t response[JH_NTP_PACKET_SIZE];
  hal_net_endpoint_t server = {};
  hal_net_endpoint_t source = {};
  make_valid_fixture(request, &server, response, &source);
  uint32_t seconds = 0u;
  uint32_t fraction = 0u;

  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_ntp_validate_response(request, &server, response,
                                                 sizeof(response) - 1u, &source,
                                                 &seconds, &fraction));
  response[0] = 0xe4u;
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_ntp_validate_response(request, &server, response,
                                                 sizeof(response), &source,
                                                 &seconds, &fraction));
  response[0] = 0x24u;
  response[1] = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_ntp_validate_response(request, &server, response,
                                                 sizeof(response), &source,
                                                 &seconds, &fraction));
  response[1] = 2u;
  std::memset(&response[40], 0, 8u);
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_ntp_validate_response(request, &server, response,
                                                 sizeof(response), &source,
                                                 &seconds, &fraction));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ntp_accepts_matching_server_response_and_extracts_time);
  RUN_TEST(test_ntp_rejects_wrong_source_and_originate_timestamp);
  RUN_TEST(test_ntp_rejects_alarm_mode_stratum_truncation_and_zero_time);
  return UNITY_END();
}
