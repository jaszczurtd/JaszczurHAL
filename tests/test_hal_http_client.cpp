#include "hal/impl/.mock/hal_mock.h"
#include "hal/network/http/hal_http_client.h"
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

void test_content_length_requires_a_complete_decimal_value(void) {
  hal_http_client_request_t request = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_http_client_request_init(&request));
  request.host = "localhost";
  request.port = 18080u;
  request.timeout_ms = 5000u;

  uint8_t body[8] = {};
  hal_http_client_response_t response = {};
  static const char reply[] =
      "HTTP/1.1 200 OK\r\nContent-Length: 5x\r\n\r\nhello";
  hal_mock_tcp_set_next_rx(reinterpret_cast<const uint8_t *>(reply),
                           (uint16_t)(sizeof(reply) - 1u));

  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO,
      hal_http_client_perform_ex(&request, body, sizeof(body), &response));
  TEST_ASSERT_EQUAL_UINT16(200u, response.status_code);
  TEST_ASSERT_FALSE(response.content_length_known);
}

/* One request against a scripted reply. The mock keeps the connection open
 * after the reply, so a response that only completes at EOF never returns
 * HAL_OK here. */
static hal_status_t perform_reply(const char *method, const char *reply,
                                  uint8_t *body, size_t body_capacity,
                                  hal_http_client_response_t *response) {
  hal_mock_tcp_reset();
  hal_http_client_request_t request = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_http_client_request_init(&request));
  request.host = "localhost";
  request.port = 18080u;
  request.method = method;
  request.path = "/framing";
  request.timeout_ms = 5000u;
  hal_mock_tcp_set_next_rx(reinterpret_cast<const uint8_t *>(reply),
                           (uint16_t)strlen(reply));
  memset(response, 0, sizeof(*response));
  return hal_http_client_perform_ex(&request, body, body_capacity, response);
}

/* Audit F09: a HEAD response has no body whatever Content-Length says
 * (RFC 9112 §6.3); the header still reports the GET body length. */
void test_head_response_has_no_body_despite_content_length(void) {
  uint8_t body[8];
  memset(body, 0xAA, sizeof(body));
  hal_http_client_response_t response = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      perform_reply("HEAD", "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n",
                    body, sizeof(body), &response));
  TEST_ASSERT_EQUAL_UINT16(200u, response.status_code);
  TEST_ASSERT_TRUE(response.content_length_known);
  TEST_ASSERT_EQUAL_UINT32(5u, response.content_length);
  TEST_ASSERT_EQUAL_UINT32(0u, response.body_length);
  TEST_ASSERT_EQUAL_HEX8(0xAAu, body[0]);
  const char *sent = reinterpret_cast<const char *>(
      hal_mock_tcp_get_last_tx_payload(hal_mock_tcp_get_last_opened_socket()));
  TEST_ASSERT_NOT_NULL(strstr(sent, "HEAD /framing HTTP/1.1\r\n"));

  /* The encoding a GET would use does not make the HEAD unsupported. */
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      perform_reply("HEAD",
                    "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n",
                    body, sizeof(body), &response));
  TEST_ASSERT_EQUAL_UINT32(0u, response.body_length);
  TEST_ASSERT_EQUAL_INT(
      HAL_EUNSUPPORTED,
      perform_reply("GET",
                    "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n",
                    body, sizeof(body), &response));
}

void test_not_modified_and_no_content_responses_have_no_body(void) {
  uint8_t body[8] = {};
  hal_http_client_response_t response = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      perform_reply("GET",
                    "HTTP/1.1 304 Not Modified\r\nContent-Length: 5\r\n\r\n",
                    body, sizeof(body), &response));
  TEST_ASSERT_EQUAL_UINT16(304u, response.status_code);
  TEST_ASSERT_EQUAL_UINT32(0u, response.body_length);
  TEST_ASSERT_EQUAL_INT(HAL_OK, perform_reply("DELETE",
                                              "HTTP/1.1 204 No Content\r\n\r\n",
                                              body, sizeof(body), &response));
  TEST_ASSERT_EQUAL_UINT16(204u, response.status_code);
  TEST_ASSERT_FALSE(response.content_length_known);
  TEST_ASSERT_EQUAL_UINT32(0u, response.body_length);
}

void test_zero_content_length_completes_at_the_end_of_the_headers(void) {
  uint8_t body[8] = {};
  hal_http_client_response_t response = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      perform_reply("GET", "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", body,
                    sizeof(body), &response));
  TEST_ASSERT_EQUAL_UINT16(200u, response.status_code);
  TEST_ASSERT_TRUE(response.content_length_known);
  TEST_ASSERT_EQUAL_UINT32(0u, response.content_length);
  TEST_ASSERT_EQUAL_UINT32(0u, response.body_length);
}

void test_interim_response_is_skipped_and_switching_protocols_rejected(void) {
  uint8_t body[8] = {};
  hal_http_client_response_t response = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, perform_reply("POST",
                            "HTTP/1.1 100 Continue\r\n\r\n"
                            "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello",
                            body, sizeof(body), &response));
  TEST_ASSERT_EQUAL_UINT16(200u, response.status_code);
  TEST_ASSERT_EQUAL_UINT32(5u, response.body_length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY("hello", body, 5u);
  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO,
      perform_reply("GET", "HTTP/1.1 101 Switching Protocols\r\n\r\n", body,
                    sizeof(body), &response));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_request_defaults_and_validation_are_bounded);
  RUN_TEST(test_plain_http_get_parses_fragment_safe_content_length_response);
  RUN_TEST(test_content_length_requires_a_complete_decimal_value);
  RUN_TEST(test_head_response_has_no_body_despite_content_length);
  RUN_TEST(test_not_modified_and_no_content_responses_have_no_body);
  RUN_TEST(test_zero_content_length_completes_at_the_end_of_the_headers);
  RUN_TEST(test_interim_response_is_skipped_and_switching_protocols_rejected);
  return UNITY_END();
}
