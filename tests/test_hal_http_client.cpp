#include "hal/hal_http_client.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <cstring>

void setUp(void) {
  hal_mock_net_reset();
  hal_mock_tcp_reset();
}

void tearDown(void) {}

void test_request_defaults_and_validation_are_bounded(void) {
  hal_http_client_request_t request = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_http_client_request_init(&request));
  TEST_ASSERT_EQUAL_INT(HAL_HTTP_CLIENT_TRANSPORT_PLAINTEXT, request.transport);
  TEST_ASSERT_EQUAL_UINT16(80u, request.port);
  TEST_ASSERT_EQUAL_STRING("GET", request.method);
  TEST_ASSERT_EQUAL_STRING("/", request.path);

  hal_http_client_response_t response = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_http_client_perform_ex(&request, nullptr, 0u, &response));
  request.host = "localhost";
  request.path = "bad";
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_http_client_perform_ex(&request, nullptr, 0u, &response));
  request.path = "/";
  request.transport = HAL_HTTP_CLIENT_TRANSPORT_TLS;
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, hal_http_client_perform_ex(
                                         &request, nullptr, 0u, &response));
}

void test_plain_http_get_parses_fragment_safe_content_length_response(void) {
  hal_http_client_request_t request = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_http_client_request_init(&request));
  request.host = "localhost";
  request.port = 18080u;
  request.path = "/probe";
  request.timeout_ms = 5000u;
  const hal_http_client_header_t headers[] = {{"X-Unit", "yes"}};
  request.headers = headers;
  request.header_count = 1u;

  uint8_t body[8] = {};
  hal_http_client_response_t response = {};
  static const char reply[] =
      "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nX-Test: value\r\n\r\nhello";
  hal_mock_tcp_set_next_rx(reinterpret_cast<const uint8_t *>(reply),
                           (uint16_t)(sizeof(reply) - 1u));
  const hal_status_t result =
      hal_http_client_perform_ex(&request, body, sizeof(body), &response);
  const hal_tcp_socket_t socket = hal_mock_tcp_get_last_opened_socket();

  TEST_ASSERT_EQUAL_INT(HAL_OK, result);
  TEST_ASSERT_NOT_NULL(socket);
  TEST_ASSERT_EQUAL_UINT16(200u, response.status_code);
  TEST_ASSERT_TRUE(response.content_length_known);
  TEST_ASSERT_EQUAL_UINT32(5u, response.content_length);
  TEST_ASSERT_EQUAL_UINT32(5u, response.body_length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY("hello", body, 5u);
  const char *sent =
      reinterpret_cast<const char *>(hal_mock_tcp_get_last_tx_payload(socket));
  TEST_ASSERT_NOT_NULL(strstr(sent, "GET /probe HTTP/1.1\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(sent, "X-Unit: yes\r\n"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_request_defaults_and_validation_are_bounded);
  RUN_TEST(test_plain_http_get_parses_fragment_safe_content_length_response);
  return UNITY_END();
}
