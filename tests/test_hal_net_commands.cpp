#include "hal/impl/.mock/hal_mock.h"
#include "hal/network/net_commands/hal_net_commands.h"
#include "support/http_server_test_helpers.h"
#include "utils/unity.h"

#include <stdio.h>
#include <string.h>

static uint32_t s_handler_calls;
static hal_net_commands_source_t s_last_source;
static char s_last_args[64];
static uint32_t s_generic_handler_calls;
static hal_command_source_t s_generic_last_source;
static hal_command_encoding_t s_generic_last_encoding;
static uint8_t s_generic_last_arguments[128];
static size_t s_generic_last_arguments_length;

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
  s_generic_handler_calls = 0u;
  s_generic_last_source = HAL_COMMAND_SOURCE_DIRECT;
  s_generic_last_encoding = HAL_COMMAND_ENCODING_BINARY;
  s_generic_last_arguments_length = 0u;
  memset(s_generic_last_arguments, 0, sizeof(s_generic_last_arguments));
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

static hal_status_t generic_handler(const hal_command_request_t *request,
                                    hal_command_response_t *response,
                                    void *user) {
  (void)user;
  s_generic_handler_calls++;
  s_generic_last_source = request->source;
  s_generic_last_encoding = request->encoding;
  TEST_ASSERT_LESS_OR_EQUAL_UINT(sizeof(s_generic_last_arguments),
                                 request->arguments_length);
  s_generic_last_arguments_length = request->arguments_length;
  if (request->arguments_length > 0u) {
    memcpy(s_generic_last_arguments, request->arguments,
           request->arguments_length);
  }

  TEST_ASSERT_EQUAL_STRING("generic", request->command);
  if (request->encoding == HAL_COMMAND_ENCODING_JSON) {
    return hal_command_response_write_str(response, "{\"generic\":true}");
  }
  hal_status_t status = hal_command_response_write_str(response, "generic: ");
  if (status == HAL_OK) {
    status = hal_command_response_write(response, request->arguments,
                                        request->arguments_length);
  }
  return status;
}

static hal_command_router_t register_generic_command(void) {
  hal_command_router_t router = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_default(&router));
  TEST_ASSERT_NOT_NULL(router);

  hal_command_definition_t definition = {};
  definition.name = "generic";
  definition.allowed_sources =
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_DIRECT) |
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_HTTP) |
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_WEBSOCKET);
  definition.handler = generic_handler;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_register(router, &definition));
  return router;
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

void test_default_router_handler_dispatches_through_text_and_json(void) {
  hal_command_router_t router = register_generic_command();
  size_t router_count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_count(router, &router_count));
  TEST_ASSERT_EQUAL_UINT(1u, router_count);
  TEST_ASSERT_EQUAL_UINT(1u, hal_net_commands_count());

  hal_net_command_response_t response;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_net_commands_execute_text("generic hello", &response));
  TEST_ASSERT_EQUAL_UINT(1u, s_generic_handler_calls);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_SOURCE_DIRECT, s_generic_last_source);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_ENCODING_TEXT, s_generic_last_encoding);
  TEST_ASSERT_EQUAL_UINT(5u, s_generic_last_arguments_length);
  TEST_ASSERT_EQUAL_MEMORY("hello", s_generic_last_arguments, 5u);
  TEST_ASSERT_EQUAL_STRING("generic: hello", response.body);

  const char json[] =
      "{\"cmd\":\"generic\",\"args\":{\"mode\":\"unit\",\"value\":2}}";
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_net_commands_execute_json(json, strlen(json), &response));
  TEST_ASSERT_EQUAL_UINT(2u, s_generic_handler_calls);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_ENCODING_JSON, s_generic_last_encoding);
  TEST_ASSERT_EQUAL_UINT(strlen("{\"mode\":\"unit\",\"value\":2}"),
                         s_generic_last_arguments_length);
  TEST_ASSERT_EQUAL_MEMORY("{\"mode\":\"unit\",\"value\":2}",
                           s_generic_last_arguments,
                           s_generic_last_arguments_length);
  TEST_ASSERT_EQUAL_STRING("application/json", response.content_type);
  TEST_ASSERT_EQUAL_STRING("{\"generic\":true}", response.body);
}

void test_default_router_handler_dispatches_through_http_and_websocket(void) {
  (void)register_generic_command();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_net_commands_register_http_route(
                            "/api/command", HAL_NET_COMMANDS_FORMAT_JSON));

  const char request[] =
      "POST /api/command HTTP/1.1\r\nHost: unit\r\nContent-Length: 40\r\n\r\n"
      "{\"cmd\":\"generic\",\"args\":{\"mode\":\"http\"}}";
  hal_tcp_socket_t http_socket = send_http_request(8090u, request);
  TEST_ASSERT_EQUAL_UINT(1u, s_generic_handler_calls);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_SOURCE_HTTP, s_generic_last_source);
  TEST_ASSERT_EQUAL_UINT(15u, s_generic_last_arguments_length);
  TEST_ASSERT_EQUAL_MEMORY("{\"mode\":\"http\"}", s_generic_last_arguments,
                           s_generic_last_arguments_length);
  assert_response_contains(http_socket, "HTTP/1.1 200 OK\r\n");
  assert_response_contains(http_socket, "{\"generic\":true}");

  hal_tcp_socket_t websocket_socket = accept_websocket_client(8099u);
  uint8_t frame[96];
  const char payload[] =
      "{\"cmd\":\"generic\",\"args\":{\"mode\":\"websocket\"}}";
  size_t frame_len = make_masked_frame(0x1u, (const uint8_t *)payload,
                                       strlen(payload), frame, sizeof(frame));
  hal_mock_tcp_inject_rx(websocket_socket, frame, (uint16_t)frame_len);
  hal_websocket_server_poll();

  TEST_ASSERT_EQUAL_UINT(2u, s_generic_handler_calls);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_SOURCE_WEBSOCKET, s_generic_last_source);
  TEST_ASSERT_EQUAL_UINT(20u, s_generic_last_arguments_length);
  TEST_ASSERT_EQUAL_MEMORY("{\"mode\":\"websocket\"}", s_generic_last_arguments,
                           s_generic_last_arguments_length);
  const uint8_t *tx = hal_mock_tcp_get_last_tx_payload(websocket_socket);
  const uint16_t tx_len = hal_mock_tcp_get_last_tx_len(websocket_socket);
  TEST_ASSERT_GREATER_THAN_UINT(2u, tx_len);
  char body[96];
  const size_t body_len = (size_t)tx_len - 2u;
  TEST_ASSERT_LESS_THAN(sizeof(body), body_len);
  memcpy(body, tx + 2u, body_len);
  body[body_len] = '\0';
  TEST_ASSERT_NOT_NULL(strstr(body, "{\"generic\":true}"));
}

void test_legacy_registry_wrappers_operate_on_default_router(void) {
  hal_command_router_t router = register_generic_command();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_net_commands_unregister("generic"));

  size_t count = 1u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_count(router, &count));
  TEST_ASSERT_EQUAL_UINT(0u, count);

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_net_commands_register("echo", echo_handler, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_count(router, &count));
  TEST_ASSERT_EQUAL_UINT(1u, count);
  hal_net_commands_clear();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_count(router, &count));
  TEST_ASSERT_EQUAL_UINT(0u, count);
}

void test_response_keeps_legacy_aggregate_field_order(void) {
  hal_net_command_response_t response = {HAL_OK,           "OK", "text/plain",
                                         {'o', 'k', '\0'}, 2u,   false};
  TEST_ASSERT_EQUAL_INT(HAL_OK, response.status);
  TEST_ASSERT_EQUAL_STRING("OK", response.message);
  TEST_ASSERT_EQUAL_STRING("text/plain", response.content_type);
  TEST_ASSERT_EQUAL_STRING("ok", response.body);
  TEST_ASSERT_EQUAL_UINT(2u, response.body_len);
  TEST_ASSERT_FALSE(response.overflow);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_ENCODING_BINARY, response.encoding);
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
  RUN_TEST(test_default_router_handler_dispatches_through_text_and_json);
  RUN_TEST(test_default_router_handler_dispatches_through_http_and_websocket);
  RUN_TEST(test_legacy_registry_wrappers_operate_on_default_router);
  RUN_TEST(test_response_keeps_legacy_aggregate_field_order);
  RUN_TEST(test_api_rejects_invalid_configuration);
  return UNITY_END();
}
