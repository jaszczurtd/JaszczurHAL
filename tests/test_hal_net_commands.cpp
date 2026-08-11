#include "hal/impl/.mock/hal_mock.h"
#include "hal/network/net_commands/hal_net_commands.h"
#include "support/http_server_test_helpers.h"
#include "utils/unity.h"

#include <stdio.h>
#include <string.h>

static uint32_t s_handler_calls;
static hal_net_commands_source_t s_last_source;
static char s_last_args[64];

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_tcp_reset();
  hal_http_server_stop();
  hal_http_server_clear_routes();
  hal_websocket_server_stop();
  hal_websocket_server_set_callbacks(NULL, NULL);
  hal_net_commands_clear();
  s_handler_calls = 0u;
  s_last_source = HAL_NET_COMMANDS_SOURCE_DIRECT;
  s_last_args[0] = '\0';
}

void tearDown(void) {
  hal_http_server_stop();
  hal_http_server_clear_routes();
  hal_websocket_server_stop();
  hal_websocket_server_set_callbacks(NULL, NULL);
  hal_net_commands_clear();
}

static hal_status_t echo_handler(const hal_net_command_request_t *request,
                                 hal_net_command_response_t *response,
                                 void *user) {
  (void)user;
  s_handler_calls++;
  s_last_source = request->source;
  snprintf(s_last_args, sizeof(s_last_args), "%s", request->args_text);

  TEST_ASSERT_EQUAL_STRING("echo", request->command);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_net_command_response_write_str(response, "echo: "));
  return hal_net_command_response_write_str(response, request->args_text);
}

static hal_status_t status_handler(const hal_net_command_request_t *request,
                                   hal_net_command_response_t *response,
                                   void *user) {
  (void)user;
  s_handler_calls++;
  s_last_source = request->source;

  cJSON *root = cJSON_CreateObject();
  TEST_ASSERT_NOT_NULL(root);
  cJSON_AddStringToObject(root, "cmd", request->command);
  cJSON_AddNumberToObject(root, "calls", (double)s_handler_calls);
  if (request->json_args != NULL) {
    const cJSON *mode =
        cJSON_GetObjectItemCaseSensitive(request->json_args, "mode");
    if (cJSON_IsString(mode)) {
      cJSON_AddStringToObject(root, "mode", mode->valuestring);
    }
  }

  hal_status_t status = hal_net_command_response_write_json(response, root);
  cJSON_Delete(root);
  return status;
}

static hal_status_t deny_handler(const hal_net_command_request_t *request,
                                 hal_net_command_response_t *response,
                                 void *user) {
  (void)request;
  (void)user;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_net_command_response_set_status(
                                    response, HAL_EPERM, "denied"));
  return HAL_EPERM;
}

static size_t make_masked_frame(uint8_t opcode, const uint8_t *payload,
                                size_t len, uint8_t *out, size_t out_size) {
  const uint8_t mask[4] = {0x12u, 0x34u, 0x56u, 0x78u};
  TEST_ASSERT_LESS_OR_EQUAL_UINT(125u, len);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT(len + 6u, out_size);
  out[0] = (uint8_t)(0x80u | opcode);
  out[1] = (uint8_t)(0x80u | len);
  memcpy(out + 2u, mask, sizeof(mask));
  for (size_t i = 0u; i < len; ++i) {
    out[6u + i] = (uint8_t)(payload[i] ^ mask[i & 3u]);
  }
  return len + 6u;
}

static void ws_message(hal_websocket_client_t client,
                       hal_websocket_message_type_t type, const uint8_t *data,
                       size_t len, void *user) {
  (void)user;
  hal_net_commands_handle_websocket_message(client, type, data, len,
                                            HAL_NET_COMMANDS_FORMAT_JSON);
}

static hal_tcp_socket_t accept_websocket_client(uint16_t port) {
  hal_websocket_callbacks_t callbacks = {};
  callbacks.on_message = ws_message;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_websocket_server_set_callbacks(&callbacks, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_websocket_server_start(port, "/ws"));
  hal_tcp_listener_t listener = hal_mock_tcp_listener_find_by_port(port);
  TEST_ASSERT_NOT_NULL(listener);

  hal_net_endpoint_t remote = make_endpoint(192u, 168u, 1u, 60u, 52000u);
  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote));
  hal_websocket_server_poll();

  hal_tcp_socket_t socket = hal_mock_tcp_get_last_accepted_socket();
  TEST_ASSERT_NOT_NULL(socket);
  const char request[] = "GET /ws HTTP/1.1\r\n"
                         "Host: unit\r\n"
                         "Upgrade: websocket\r\n"
                         "Connection: Upgrade\r\n"
                         "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                         "Sec-WebSocket-Version: 13\r\n\r\n";
  hal_mock_tcp_inject_rx(socket, (const uint8_t *)request,
                         (uint16_t)strlen(request));
  hal_websocket_server_poll();
  return socket;
}

void test_text_command_dispatches_to_registered_handler(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_net_commands_register("echo", echo_handler, NULL));

  hal_net_command_response_t response;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_net_commands_execute_text("echo hello", &response));

  TEST_ASSERT_EQUAL_UINT(1u, s_handler_calls);
  TEST_ASSERT_EQUAL_INT(HAL_NET_COMMANDS_SOURCE_DIRECT, s_last_source);
  TEST_ASSERT_EQUAL_STRING("hello", s_last_args);
  TEST_ASSERT_EQUAL_STRING("echo: hello", response.body);
}

void test_json_command_exposes_args_and_writes_json_response(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_net_commands_register("status", status_handler, NULL));

  hal_net_command_response_t response;
  const char json[] = "{\"cmd\":\"status\",\"args\":{\"mode\":\"unit\"}}";
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_net_commands_execute_json(json, strlen(json), &response));

  TEST_ASSERT_EQUAL_UINT(1u, s_handler_calls);
  TEST_ASSERT_EQUAL_STRING("application/json", response.content_type);
  TEST_ASSERT_NOT_NULL(strstr(response.body, "\"cmd\":\"status\""));
  TEST_ASSERT_NOT_NULL(strstr(response.body, "\"mode\":\"unit\""));
}

void test_unknown_json_command_returns_structured_error(void) {
  hal_net_command_response_t response;
  const char json[] = "{\"cmd\":\"missing\"}";
  TEST_ASSERT_EQUAL_INT(
      HAL_ENOENT, hal_net_commands_execute_json(json, strlen(json), &response));

  TEST_ASSERT_EQUAL_STRING("application/json", response.content_type);
  TEST_ASSERT_NOT_NULL(strstr(response.body, "\"ok\":false"));
  TEST_ASSERT_NOT_NULL(strstr(response.body, "\"status\":\"HAL_ENOENT\""));
}

void test_http_route_dispatches_json_command(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_net_commands_register("status", status_handler, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_net_commands_register_http_route(
                            "/api/command", HAL_NET_COMMANDS_FORMAT_JSON));

  const char request[] =
      "POST /api/command HTTP/1.1\r\nHost: unit\r\nContent-Length: 16\r\n\r\n"
      "{\"cmd\":\"status\"}";
  hal_tcp_socket_t socket = send_http_request(8088u, request);

  TEST_ASSERT_EQUAL_UINT(1u, s_handler_calls);
  TEST_ASSERT_EQUAL_INT(HAL_NET_COMMANDS_SOURCE_HTTP, s_last_source);
  assert_response_contains(socket, "HTTP/1.1 200 OK\r\n");
  assert_response_contains(socket, "Content-Type: application/json\r\n");
  assert_response_contains(socket, "\"cmd\":\"status\"");
}

void test_http_route_maps_command_permission_error(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_net_commands_register("deny", deny_handler, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_net_commands_register_http_route(
                            "/api/command", HAL_NET_COMMANDS_FORMAT_JSON));

  const char request[] =
      "POST /api/command HTTP/1.1\r\nHost: unit\r\nContent-Length: 14\r\n\r\n"
      "{\"cmd\":\"deny\"}";
  hal_tcp_socket_t socket = send_http_request(8089u, request);

  assert_response_contains(socket, "HTTP/1.1 403 Forbidden\r\n");
  assert_response_contains(socket, "\"status\":\"HAL_EPERM\"");
}

void test_websocket_message_dispatches_and_replies(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_net_commands_register("status", status_handler, NULL));
  hal_tcp_socket_t socket = accept_websocket_client(8098u);

  uint8_t frame[64];
  const char payload[] = "{\"cmd\":\"status\"}";
  size_t frame_len = make_masked_frame(0x1u, (const uint8_t *)payload,
                                       strlen(payload), frame, sizeof(frame));
  hal_mock_tcp_inject_rx(socket, frame, (uint16_t)frame_len);
  hal_websocket_server_poll();

  TEST_ASSERT_EQUAL_UINT(1u, s_handler_calls);
  TEST_ASSERT_EQUAL_INT(HAL_NET_COMMANDS_SOURCE_WEBSOCKET, s_last_source);

  const uint8_t *tx = hal_mock_tcp_get_last_tx_payload(socket);
  uint16_t tx_len = hal_mock_tcp_get_last_tx_len(socket);
  TEST_ASSERT_GREATER_THAN_UINT(2u, tx_len);
  TEST_ASSERT_EQUAL_UINT8(0x81u, tx[0]);
  char body[256];
  size_t body_len = (size_t)tx_len - 2u;
  if (body_len >= sizeof(body)) {
    body_len = sizeof(body) - 1u;
  }
  memcpy(body, tx + 2u, body_len);
  body[body_len] = '\0';
  TEST_ASSERT_NOT_NULL(strstr(body, "\"cmd\":\"status\""));
}

void test_api_rejects_invalid_configuration(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_net_commands_register(NULL, echo_handler, NULL));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_net_commands_register("bad name", echo_handler, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_net_commands_register("x", NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_net_commands_execute_text(NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_net_commands_register_http_route(
                                        "api", HAL_NET_COMMANDS_FORMAT_JSON));
  TEST_ASSERT_EQUAL_STRING(
      "AUTO", hal_net_commands_format_to_string(HAL_NET_COMMANDS_FORMAT_AUTO));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", hal_net_commands_format_to_string(
                                          (hal_net_commands_format_t)99));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_text_command_dispatches_to_registered_handler);
  RUN_TEST(test_json_command_exposes_args_and_writes_json_response);
  RUN_TEST(test_unknown_json_command_returns_structured_error);
  RUN_TEST(test_http_route_dispatches_json_command);
  RUN_TEST(test_http_route_maps_command_permission_error);
  RUN_TEST(test_websocket_message_dispatches_and_replies);
  RUN_TEST(test_api_rejects_invalid_configuration);
  return UNITY_END();
}
