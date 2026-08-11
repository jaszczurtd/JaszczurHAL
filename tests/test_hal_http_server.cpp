#include "hal/impl/.mock/hal_mock.h"
#include "hal/network/http/hal_http_server.h"
#include "support/http_server_test_helpers.h"
#include "utils/unity.h"

#include <string.h>

static bool s_seen_handler;
static char s_seen_path[32];
static char s_seen_query[32];
static char s_seen_body[64];
static char s_seen_content_type[64];
static char s_seen_custom_header[64];
static size_t s_seen_body_len;

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_tcp_reset();
  hal_mock_set_millis(0u);
  hal_http_server_stop();
  hal_http_server_clear_routes();
  s_seen_handler = false;
  s_seen_path[0] = '\0';
  s_seen_query[0] = '\0';
  s_seen_body[0] = '\0';
  s_seen_content_type[0] = '\0';
  s_seen_custom_header[0] = '\0';
  s_seen_body_len = 0u;
}

void tearDown(void) {
  hal_http_server_stop();
  hal_http_server_clear_routes();
}

static hal_status_t hello_handler(const hal_http_request_t *request,
                                  hal_http_response_t *response, void *user) {
  (void)request;
  (void)user;
  s_seen_handler = true;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_http_response_set_content_type(response, "text/plain"));
  return hal_http_response_write_str(response, "hello\n");
}

static hal_status_t post_handler(const hal_http_request_t *request,
                                 hal_http_response_t *response, void *user) {
  (void)user;
  s_seen_handler = true;
  strncpy(s_seen_path, request->path, sizeof(s_seen_path) - 1u);
  strncpy(s_seen_query, request->query, sizeof(s_seen_query) - 1u);
  s_seen_body_len = request->body_len;
  strncpy(s_seen_body, request->body, sizeof(s_seen_body) - 1u);
  const char *content_type =
      hal_http_request_get_header(request, "content-type");
  const char *custom = hal_http_request_get_header(request, "X-Unit");
  if (content_type) {
    strncpy(s_seen_content_type, content_type,
            sizeof(s_seen_content_type) - 1u);
  }
  if (custom) {
    strncpy(s_seen_custom_header, custom, sizeof(s_seen_custom_header) - 1u);
  }

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_http_response_set_status(response, 201u, "Created"));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_http_response_set_content_type(response, "application/json"));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_http_response_set_header(response, "X-Reply", "accepted"));
  return hal_http_response_write_str(response, "{\"ok\":true}");
}

static hal_status_t prefix_handler(const hal_http_request_t *request,
                                   hal_http_response_t *response, void *user) {
  (void)user;
  s_seen_handler = true;
  strncpy(s_seen_path, request->path, sizeof(s_seen_path) - 1u);
  return hal_http_response_write_str(response, "prefix");
}

static hal_status_t failing_handler(const hal_http_request_t *request,
                                    hal_http_response_t *response, void *user) {
  (void)request;
  (void)response;
  (void)user;
  return HAL_EIO;
}

void test_get_route_sends_ok_response(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_http_server_route(HAL_HTTP_METHOD_GET, "/hello",
                                              hello_handler, NULL));

  hal_tcp_socket_t socket =
      send_http_request(8080u, "GET /hello HTTP/1.1\r\nHost: unit\r\n\r\n");

  TEST_ASSERT_TRUE(s_seen_handler);
  assert_response_contains(socket, "HTTP/1.1 200 OK\r\n");
  assert_response_contains(socket, "Content-Type: text/plain\r\n");
  assert_response_contains(socket, "Content-Length: 6\r\n");
  assert_response_contains(socket, "\r\n\r\nhello\n");
}

void test_post_route_exposes_query_and_body(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_http_server_route(HAL_HTTP_METHOD_POST, "/api", post_handler, NULL));

  hal_tcp_socket_t socket = send_http_request(
      8081u, "POST /api?mode=test HTTP/1.1\r\nHost: unit\r\n"
             "Content-Type: application/json\r\nX-Unit: yes\r\n"
             "Content-Length: 7\r\n\r\npayload");

  TEST_ASSERT_TRUE(s_seen_handler);
  TEST_ASSERT_EQUAL_STRING("/api", s_seen_path);
  TEST_ASSERT_EQUAL_STRING("mode=test", s_seen_query);
  TEST_ASSERT_EQUAL_UINT(7u, s_seen_body_len);
  TEST_ASSERT_EQUAL_STRING("payload", s_seen_body);
  TEST_ASSERT_EQUAL_STRING("application/json", s_seen_content_type);
  TEST_ASSERT_EQUAL_STRING("yes", s_seen_custom_header);
  assert_response_contains(socket, "HTTP/1.1 201 Created\r\n");
  assert_response_contains(socket, "Content-Type: application/json\r\n");
  assert_response_contains(socket, "X-Reply: accepted\r\n");
  assert_response_contains(socket, "Content-Length: 11\r\n");
  assert_response_contains(socket, "\r\n\r\n{\"ok\":true}");
}

void test_unknown_route_returns_404(void) {
  hal_tcp_socket_t socket =
      send_http_request(8082u, "GET /missing HTTP/1.1\r\nHost: unit\r\n\r\n");

  TEST_ASSERT_FALSE(s_seen_handler);
  assert_response_contains(socket, "HTTP/1.1 404 Not Found\r\n");
  assert_response_contains(socket, "\r\n\r\nNot Found\n");
}

void test_head_sends_headers_without_body(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_http_server_route(HAL_HTTP_METHOD_HEAD, "/hello",
                                              hello_handler, NULL));

  hal_tcp_socket_t socket =
      send_http_request(8083u, "HEAD /hello HTTP/1.1\r\nHost: unit\r\n\r\n");

  TEST_ASSERT_TRUE(s_seen_handler);
  assert_response_contains(socket, "HTTP/1.1 200 OK\r\n");
  assert_response_contains(socket, "Content-Length: 6\r\n");
  char payload[768];
  uint16_t len = hal_mock_tcp_get_last_tx_len(socket);
  TEST_ASSERT_LESS_THAN(sizeof(payload), len);
  memcpy(payload, hal_mock_tcp_get_last_tx_payload(socket), len);
  payload[len] = '\0';
  TEST_ASSERT_NULL(strstr(payload, "\r\n\r\nhello\n"));
}

void test_handler_failure_returns_500(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_http_server_route(HAL_HTTP_METHOD_GET, "/fail",
                                              failing_handler, NULL));

  hal_tcp_socket_t socket =
      send_http_request(8084u, "GET /fail HTTP/1.1\r\nHost: unit\r\n\r\n");

  assert_response_contains(socket, "HTTP/1.1 500 Internal Server Error\r\n");
  assert_response_contains(socket, "\r\n\r\nInternal Server Error\n");
}

void test_api_rejects_invalid_configuration(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_http_server_start(0u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_http_server_route(HAL_HTTP_METHOD_UNKNOWN, "/x",
                                              hello_handler, NULL));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      hal_http_server_route(HAL_HTTP_METHOD_GET, "bad", hello_handler, NULL));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_http_server_route(HAL_HTTP_METHOD_GET, "/x", NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_http_server_route_prefix(HAL_HTTP_METHOD_GET, "bad",
                                                     hello_handler, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_http_response_write_str(NULL, "invalid"));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_http_response_set_header(NULL, "X-Test", "yes"));
  TEST_ASSERT_NULL(hal_http_request_get_header(NULL, "Content-Type"));
  TEST_ASSERT_EQUAL_STRING("GET",
                           hal_http_method_to_string(HAL_HTTP_METHOD_GET));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN",
                           hal_http_method_to_string(HAL_HTTP_METHOD_UNKNOWN));
}

void test_prefix_route_matches_nested_path(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_http_server_route_prefix(HAL_HTTP_METHOD_GET, "/assets",
                                           prefix_handler, NULL));

  hal_tcp_socket_t socket = send_http_request(
      8085u, "GET /assets/app.js HTTP/1.1\r\nHost: unit\r\n\r\n");

  TEST_ASSERT_TRUE(s_seen_handler);
  TEST_ASSERT_EQUAL_STRING("/assets/app.js", s_seen_path);
  assert_response_contains(socket, "HTTP/1.1 200 OK\r\n");
  assert_response_contains(socket, "\r\n\r\nprefix");
}

void test_content_length_overflow_is_rejected_before_body_indexing(void) {
  hal_tcp_socket_t socket = send_http_request(
      8086u, "POST /api HTTP/1.1\r\nHost: unit\r\nContent-Length: "
             "184467440737095516160\r\n\r\n");

  TEST_ASSERT_FALSE(s_seen_handler);
  assert_response_contains(socket, "HTTP/1.1 413 Payload Too Large\r\n");
}

void test_ambiguous_request_framing_is_rejected(void) {
  hal_tcp_socket_t invalid =
      send_http_request(8087u, "POST /api HTTP/1.1\r\nHost: unit\r\n"
                               "Content-Length: 1x\r\n\r\nx");
  assert_response_contains(invalid, "HTTP/1.1 400 Bad Request\r\n");
  hal_http_server_stop();

  hal_tcp_socket_t duplicate = send_http_request(
      8088u, "POST /api HTTP/1.1\r\nHost: unit\r\n"
             "Content-Length: 1\r\nContent-Length: 1\r\n\r\nx");
  assert_response_contains(duplicate, "HTTP/1.1 400 Bad Request\r\n");
  hal_http_server_stop();

  hal_tcp_socket_t transfer =
      send_http_request(8089u, "POST /api HTTP/1.1\r\nHost: unit\r\n"
                               "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
  assert_response_contains(transfer, "HTTP/1.1 400 Bad Request\r\n");
}

void test_route_owns_path_and_rejects_overlong_path(void) {
  char path[] = "/owned";
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_http_server_route(HAL_HTTP_METHOD_GET, path,
                                                      hello_handler, NULL));
  path[1] = 'x';

  hal_tcp_socket_t socket =
      send_http_request(8090u, "GET /owned HTTP/1.1\r\nHost: unit\r\n\r\n");
  TEST_ASSERT_TRUE(s_seen_handler);
  assert_response_contains(socket, "HTTP/1.1 200 OK\r\n");

  char overlong[HAL_HTTP_SERVER_ROUTE_PATH_MAX + 1u];
  memset(overlong, 'a', sizeof(overlong));
  overlong[0] = '/';
  overlong[sizeof(overlong) - 1u] = '\0';
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        hal_http_server_route(HAL_HTTP_METHOD_GET, overlong,
                                              hello_handler, NULL));
}

void test_incomplete_clients_time_out_and_release_slots(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_http_server_route(HAL_HTTP_METHOD_GET, "/hello",
                                              hello_handler, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_http_server_start(8091u));
  hal_tcp_listener_t listener = hal_mock_tcp_listener_find_by_port(8091u);
  TEST_ASSERT_NOT_NULL(listener);
  hal_net_endpoint_t remote = make_endpoint(192u, 168u, 1u, 70u, 53000u);
  for (size_t i = 0u; i < HAL_HTTP_SERVER_MAX_CLIENTS; ++i) {
    TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote));
    hal_http_server_poll();
    hal_tcp_socket_t socket = hal_mock_tcp_get_last_accepted_socket();
    const uint8_t partial_request = 'G';
    hal_mock_tcp_inject_rx(socket, &partial_request, 1u);
    hal_http_server_poll();
  }

  hal_mock_advance_millis(HAL_HTTP_SERVER_IDLE_TIMEOUT_MS);
  hal_http_server_poll();

  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote));
  hal_http_server_poll();
  hal_tcp_socket_t slow_socket = hal_mock_tcp_get_last_accepted_socket();
  for (size_t i = 0u; i < 3u; ++i) {
    hal_mock_advance_millis(HAL_HTTP_SERVER_IDLE_TIMEOUT_MS - 1u);
    const uint8_t next_byte = (uint8_t)('G' + i);
    hal_mock_tcp_inject_rx(slow_socket, &next_byte, 1u);
    hal_http_server_poll();
  }
  hal_mock_advance_millis(HAL_HTTP_SERVER_REQUEST_TIMEOUT_MS -
                          (3u * (HAL_HTTP_SERVER_IDLE_TIMEOUT_MS - 1u)));
  hal_http_server_poll();

  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote));
  hal_http_server_poll();
  hal_tcp_socket_t socket = hal_mock_tcp_get_last_accepted_socket();
  const char request[] = "GET /hello HTTP/1.1\r\nHost: unit\r\n\r\n";
  hal_mock_tcp_inject_rx(socket, (const uint8_t *)request,
                         (uint16_t)strlen(request));
  hal_http_server_poll();
  TEST_ASSERT_TRUE(s_seen_handler);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_get_route_sends_ok_response);
  RUN_TEST(test_post_route_exposes_query_and_body);
  RUN_TEST(test_unknown_route_returns_404);
  RUN_TEST(test_head_sends_headers_without_body);
  RUN_TEST(test_handler_failure_returns_500);
  RUN_TEST(test_api_rejects_invalid_configuration);
  RUN_TEST(test_prefix_route_matches_nested_path);
  RUN_TEST(test_content_length_overflow_is_rejected_before_body_indexing);
  RUN_TEST(test_ambiguous_request_framing_is_rejected);
  RUN_TEST(test_route_owns_path_and_rejects_overlong_path);
  RUN_TEST(test_incomplete_clients_time_out_and_release_slots);
  return UNITY_END();
}
