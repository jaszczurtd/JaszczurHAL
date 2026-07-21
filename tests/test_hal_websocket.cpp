#include "hal/hal_websocket.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <stdio.h>
#include <string.h>

static hal_websocket_client_t s_connected[HAL_WEBSOCKET_MAX_CLIENTS];
static size_t s_connected_count;
static hal_websocket_client_t s_last_message_client;
static hal_websocket_message_type_t s_last_message_type;
static uint8_t s_last_message[64];
static size_t s_last_message_len;
static hal_websocket_client_t s_last_disconnect_client;
static uint16_t s_last_disconnect_code;

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_tcp_reset();
  hal_websocket_server_stop();
  hal_websocket_server_set_callbacks(NULL, NULL);
  memset(s_connected, 0xff, sizeof(s_connected));
  s_connected_count = 0u;
  s_last_message_client = HAL_WEBSOCKET_INVALID_CLIENT;
  s_last_message_type = HAL_WEBSOCKET_MESSAGE_TEXT;
  memset(s_last_message, 0, sizeof(s_last_message));
  s_last_message_len = 0u;
  s_last_disconnect_client = HAL_WEBSOCKET_INVALID_CLIENT;
  s_last_disconnect_code = 0u;
}

void tearDown(void) {
  hal_websocket_server_stop();
  hal_websocket_server_set_callbacks(NULL, NULL);
}

static hal_net_endpoint_t make_endpoint(uint8_t a, uint8_t b, uint8_t c,
                                        uint8_t d, uint16_t port) {
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr_len = HAL_NET_IPV4_ADDR_LEN;
  endpoint.addr[0] = a;
  endpoint.addr[1] = b;
  endpoint.addr[2] = c;
  endpoint.addr[3] = d;
  endpoint.port = port;
  return endpoint;
}

static void on_connect(hal_websocket_client_t client, void *user) {
  (void)user;
  if (s_connected_count < HAL_WEBSOCKET_MAX_CLIENTS) {
    s_connected[s_connected_count++] = client;
  }
}

static void on_message(hal_websocket_client_t client,
                       hal_websocket_message_type_t type, const uint8_t *data,
                       size_t len, void *user) {
  (void)user;
  s_last_message_client = client;
  s_last_message_type = type;
  s_last_message_len = len;
  if (len > sizeof(s_last_message)) {
    len = sizeof(s_last_message);
  }
  if (len > 0u) {
    memcpy(s_last_message, data, len);
  }
}

static void on_disconnect(hal_websocket_client_t client, uint16_t close_code,
                          void *user) {
  (void)user;
  s_last_disconnect_client = client;
  s_last_disconnect_code = close_code;
}

static void install_callbacks(void) {
  hal_websocket_callbacks_t callbacks = {};
  callbacks.on_connect = on_connect;
  callbacks.on_message = on_message;
  callbacks.on_disconnect = on_disconnect;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_websocket_server_set_callbacks(&callbacks, NULL));
}

static hal_tcp_socket_t accept_client(uint16_t port, const char *path,
                                      const char *key) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_websocket_server_start(port, path));
  hal_tcp_listener_t listener = hal_mock_tcp_listener_find_by_port(port);
  TEST_ASSERT_NOT_NULL(listener);

  hal_net_endpoint_t remote = make_endpoint(192u, 168u, 1u, 60u, 52000u);
  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote));
  hal_websocket_server_poll();

  hal_tcp_socket_t socket = hal_mock_tcp_get_last_accepted_socket();
  TEST_ASSERT_NOT_NULL(socket);

  char request[256];
  int written = snprintf(request, sizeof(request),
                         "GET %s HTTP/1.1\r\n"
                         "Host: unit\r\n"
                         "Upgrade: websocket\r\n"
                         "Connection: Upgrade\r\n"
                         "Sec-WebSocket-Key: %s\r\n"
                         "Sec-WebSocket-Version: 13\r\n\r\n",
                         path, key);
  TEST_ASSERT_GREATER_THAN(0, written);
  TEST_ASSERT_LESS_THAN((int)sizeof(request), written);

  hal_mock_tcp_inject_rx(socket, (const uint8_t *)request,
                         (uint16_t)strlen(request));
  hal_websocket_server_poll();
  return socket;
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

static void assert_tx_contains(hal_tcp_socket_t socket, const char *needle) {
  char text[512];
  uint16_t len = hal_mock_tcp_get_last_tx_len(socket);
  TEST_ASSERT_LESS_THAN(sizeof(text), len);
  memcpy(text, hal_mock_tcp_get_last_tx_payload(socket), len);
  text[len] = '\0';
  TEST_ASSERT_NOT_NULL(strstr(text, needle));
}

void test_handshake_accepts_upgrade_and_calls_connect(void) {
  install_callbacks();
  hal_tcp_socket_t socket =
      accept_client(8090u, "/ws", "dGhlIHNhbXBsZSBub25jZQ==");

  TEST_ASSERT_EQUAL_UINT(1u, s_connected_count);
  TEST_ASSERT_TRUE(hal_websocket_client_is_connected(s_connected[0]));
  TEST_ASSERT_EQUAL_UINT(1u, hal_websocket_client_count());
  assert_tx_contains(socket, "HTTP/1.1 101 Switching Protocols\r\n");
  assert_tx_contains(socket, "Sec-WebSocket-Accept: "
                             "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n");
}

void test_text_frame_is_unmasked_and_delivered(void) {
  install_callbacks();
  hal_tcp_socket_t socket =
      accept_client(8091u, "/ws", "dGhlIHNhbXBsZSBub25jZQ==");
  uint8_t frame[32];
  const uint8_t payload[] = {'h', 'e', 'l', 'l', 'o'};
  size_t frame_len =
      make_masked_frame(0x1u, payload, sizeof(payload), frame, sizeof(frame));

  hal_mock_tcp_inject_rx(socket, frame, (uint16_t)frame_len);
  hal_websocket_server_poll();

  TEST_ASSERT_EQUAL_UINT(s_connected[0], s_last_message_client);
  TEST_ASSERT_EQUAL_INT(HAL_WEBSOCKET_MESSAGE_TEXT, s_last_message_type);
  TEST_ASSERT_EQUAL_UINT(sizeof(payload), s_last_message_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, s_last_message, sizeof(payload));
}

void test_broadcast_text_sends_frame_to_all_open_clients(void) {
  install_callbacks();
  hal_tcp_socket_t first =
      accept_client(8092u, "/events", "dGhlIHNhbXBsZSBub25jZQ==");
  hal_tcp_socket_t second =
      accept_client(8092u, "/events", "bW9jay1rZXktMTIzNDU2Nzg5MA==");

  TEST_ASSERT_EQUAL_UINT(2u, s_connected_count);
  size_t sent_count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_websocket_broadcast_text("tick", &sent_count));
  TEST_ASSERT_EQUAL_UINT(2u, sent_count);

  const uint8_t expected[] = {0x81u, 0x04u, 't', 'i', 'c', 'k'};
  TEST_ASSERT_EQUAL_UINT(sizeof(expected), hal_mock_tcp_get_last_tx_len(first));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      expected, hal_mock_tcp_get_last_tx_payload(first), sizeof(expected));
  TEST_ASSERT_EQUAL_UINT(sizeof(expected),
                         hal_mock_tcp_get_last_tx_len(second));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      expected, hal_mock_tcp_get_last_tx_payload(second), sizeof(expected));
}

void test_ping_gets_pong_with_same_payload(void) {
  hal_tcp_socket_t socket =
      accept_client(8093u, "/ws", "dGhlIHNhbXBsZSBub25jZQ==");
  uint8_t frame[32];
  const uint8_t payload[] = {'o', 'k'};
  size_t frame_len =
      make_masked_frame(0x9u, payload, sizeof(payload), frame, sizeof(frame));

  hal_mock_tcp_inject_rx(socket, frame, (uint16_t)frame_len);
  hal_websocket_server_poll();

  const uint8_t expected[] = {0x8au, 0x02u, 'o', 'k'};
  TEST_ASSERT_EQUAL_UINT(sizeof(expected),
                         hal_mock_tcp_get_last_tx_len(socket));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      expected, hal_mock_tcp_get_last_tx_payload(socket), sizeof(expected));
}

void test_close_frame_disconnects_and_reports_code(void) {
  install_callbacks();
  hal_tcp_socket_t socket =
      accept_client(8094u, "/ws", "dGhlIHNhbXBsZSBub25jZQ==");
  uint8_t frame[32];
  const uint8_t payload[] = {0x03u, 0xe8u};
  size_t frame_len =
      make_masked_frame(0x8u, payload, sizeof(payload), frame, sizeof(frame));

  hal_mock_tcp_inject_rx(socket, frame, (uint16_t)frame_len);
  hal_websocket_server_poll();

  TEST_ASSERT_EQUAL_UINT(s_connected[0], s_last_disconnect_client);
  TEST_ASSERT_EQUAL_UINT16(1000u, s_last_disconnect_code);
  TEST_ASSERT_FALSE(hal_websocket_client_is_connected(s_connected[0]));
  TEST_ASSERT_EQUAL_UINT(0u, hal_websocket_client_count());
}

void test_invalid_handshake_returns_bad_request(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_websocket_server_start(8095u, "/ws"));
  hal_tcp_listener_t listener = hal_mock_tcp_listener_find_by_port(8095u);
  TEST_ASSERT_NOT_NULL(listener);

  hal_net_endpoint_t remote = make_endpoint(192u, 168u, 1u, 61u, 52001u);
  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote));
  hal_websocket_server_poll();

  hal_tcp_socket_t socket = hal_mock_tcp_get_last_accepted_socket();
  TEST_ASSERT_NOT_NULL(socket);
  const char request[] = "GET /ws HTTP/1.1\r\nHost: unit\r\n\r\n";
  hal_mock_tcp_inject_rx(socket, (const uint8_t *)request,
                         (uint16_t)strlen(request));
  hal_websocket_server_poll();

  assert_tx_contains(socket, "HTTP/1.1 400 Bad Request\r\n");
}

void test_api_rejects_invalid_configuration(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_websocket_server_start(0u, "/ws"));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_websocket_server_start(8096u, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_websocket_server_start(8096u, "ws"));
  TEST_ASSERT_EQUAL_INT(
      HAL_ENOENT, hal_websocket_send_text(HAL_WEBSOCKET_INVALID_CLIENT, "x"));
  size_t sent_count = 123u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_websocket_broadcast_text(NULL, &sent_count));
  TEST_ASSERT_EQUAL_UINT(0u, sent_count);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_handshake_accepts_upgrade_and_calls_connect);
  RUN_TEST(test_text_frame_is_unmasked_and_delivered);
  RUN_TEST(test_broadcast_text_sends_frame_to_all_open_clients);
  RUN_TEST(test_ping_gets_pong_with_same_payload);
  RUN_TEST(test_close_frame_disconnects_and_reports_code);
  RUN_TEST(test_invalid_handshake_returns_bad_request);
  RUN_TEST(test_api_rejects_invalid_configuration);
  return UNITY_END();
}
