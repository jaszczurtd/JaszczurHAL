#include "hal/hal_net_console.h"
#include "hal/hal_serial.h"
#include "hal/hal_tcp.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <stdio.h>
#include <string.h>

static hal_net_console_client_t s_last_event_client;
static hal_net_console_event_t s_last_event;
static unsigned s_connect_events;
static unsigned s_auth_events;
static unsigned s_disconnect_events;
static hal_net_console_client_t s_last_line_client;
static char s_last_line[64];

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_tcp_reset();
  hal_net_console_stop();
  hal_net_console_set_callbacks(NULL, NULL, NULL);
  s_last_event_client = HAL_NET_CONSOLE_INVALID_CLIENT;
  s_last_event = HAL_NET_CONSOLE_EVENT_DISCONNECT;
  s_connect_events = 0u;
  s_auth_events = 0u;
  s_disconnect_events = 0u;
  s_last_line_client = HAL_NET_CONSOLE_INVALID_CLIENT;
  s_last_line[0] = '\0';
}

void tearDown(void) {
  hal_net_console_stop();
  hal_net_console_set_callbacks(NULL, NULL, NULL);
}

static hal_net_endpoint_t make_endpoint(uint8_t d, uint16_t port) {
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr[0] = 192u;
  endpoint.addr[1] = 168u;
  endpoint.addr[2] = 1u;
  endpoint.addr[3] = d;
  endpoint.port = port;
  return endpoint;
}

static void on_event(hal_net_console_client_t client,
                     hal_net_console_event_t event, void *user) {
  (void)user;
  s_last_event_client = client;
  s_last_event = event;
  if (event == HAL_NET_CONSOLE_EVENT_CONNECT) {
    ++s_connect_events;
  } else if (event == HAL_NET_CONSOLE_EVENT_AUTHENTICATED) {
    ++s_auth_events;
  } else if (event == HAL_NET_CONSOLE_EVENT_DISCONNECT) {
    ++s_disconnect_events;
  }
}

static hal_status_t on_line(hal_net_console_client_t client, const char *line,
                            void *user) {
  (void)user;
  s_last_line_client = client;
  snprintf(s_last_line, sizeof(s_last_line), "%s", line ? line : "");
  return hal_net_console_write_text_to(client, "reply:ok\r\n");
}

static void install_callbacks(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_net_console_set_callbacks(on_event, on_line, NULL));
}

static void assert_tx_contains(hal_tcp_socket_t socket, const char *needle) {
  char text[512];
  uint16_t len = hal_mock_tcp_get_last_tx_len(socket);
  TEST_ASSERT_LESS_THAN(sizeof(text), len);
  memcpy(text, hal_mock_tcp_get_last_tx_payload(socket), len);
  text[len] = '\0';
  TEST_ASSERT_NOT_NULL(strstr(text, needle));
}

static hal_tcp_socket_t connect_client(uint16_t port, uint8_t host_octet,
                                       uint16_t remote_port,
                                       const char *password) {
  hal_tcp_listener_t listener = hal_mock_tcp_listener_find_by_port(port);
  TEST_ASSERT_NOT_NULL(listener);

  hal_net_endpoint_t remote = make_endpoint(host_octet, remote_port);
  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote));
  hal_net_console_poll();

  hal_tcp_socket_t socket = hal_mock_tcp_get_last_accepted_socket();
  TEST_ASSERT_NOT_NULL(socket);
  TEST_ASSERT_TRUE(hal_tcp_socket_is_connected(socket));
  assert_tx_contains(socket, "Password:");

  char auth[96];
  int written = snprintf(auth, sizeof(auth), "%s\n", password);
  TEST_ASSERT_GREATER_THAN(0, written);
  TEST_ASSERT_LESS_THAN((int)sizeof(auth), written);
  hal_mock_tcp_inject_rx(socket, (const uint8_t *)auth, (uint16_t)strlen(auth));
  hal_net_console_poll();
  TEST_ASSERT_TRUE(hal_tcp_socket_is_connected(socket));
  assert_tx_contains(socket, "OK");
  return socket;
}

void test_start_requires_nonempty_password(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_net_console_start(0u, "secret"));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_net_console_start(2323u, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_net_console_start(2323u, ""));

  char too_long[HAL_NET_CONSOLE_PASSWORD_MAX + 1u];
  memset(too_long, 'x', sizeof(too_long));
  too_long[sizeof(too_long) - 1u] = '\0';
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, hal_net_console_start(2323u, too_long));
}

void test_authentication_gate_keeps_logs_from_unauthenticated_clients(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_net_console_start(2324u, "secret"));
  hal_tcp_listener_t listener = hal_mock_tcp_listener_find_by_port(2324u);
  TEST_ASSERT_NOT_NULL(listener);
  hal_net_endpoint_t remote = make_endpoint(44u, 53044u);
  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote));
  hal_net_console_poll();

  hal_tcp_socket_t socket = hal_mock_tcp_get_last_accepted_socket();
  TEST_ASSERT_NOT_NULL(socket);
  uint16_t before_len = hal_mock_tcp_get_last_tx_len(socket);
  hal_serial_println("hidden");
  hal_net_console_poll();
  TEST_ASSERT_EQUAL_UINT16(before_len, hal_mock_tcp_get_last_tx_len(socket));

  hal_mock_tcp_inject_rx(socket, (const uint8_t *)"secret\n", 7u);
  hal_net_console_poll();
  TEST_ASSERT_EQUAL_UINT(1u, hal_net_console_authenticated_count());

  hal_serial_println("visible");
  hal_net_console_poll();
  TEST_ASSERT_EQUAL_STRING("visible", hal_mock_serial_last_line());
  assert_tx_contains(socket, "visible\n");
}

void test_multiple_authenticated_clients_receive_serial_mirror(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_net_console_start(2325u, "pw"));
  hal_tcp_socket_t first = connect_client(2325u, 50u, 53050u, "pw");
  hal_tcp_socket_t second = connect_client(2325u, 51u, 53051u, "pw");
  TEST_ASSERT_EQUAL_UINT(2u, hal_net_console_authenticated_count());

  hal_serial_print("tick");
  hal_net_console_poll();

  assert_tx_contains(first, "tick");
  assert_tx_contains(second, "tick");
}

void test_bidirectional_input_is_available_and_delivered_as_lines(void) {
  install_callbacks();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_net_console_start(2326u, "pw"));
  hal_tcp_socket_t socket = connect_client(2326u, 60u, 53060u, "pw");

  const char command[] = "status\n";
  hal_mock_tcp_inject_rx(socket, (const uint8_t *)command,
                         (uint16_t)strlen(command));
  hal_net_console_poll();

  TEST_ASSERT_EQUAL_STRING("status", s_last_line);
  TEST_ASSERT_EQUAL_UINT8(0u, s_last_line_client);
  assert_tx_contains(socket, "reply:ok\r\n");
  TEST_ASSERT_EQUAL_INT((int)strlen(command), hal_net_console_available());

  char out[16] = {};
  TEST_ASSERT_EQUAL_INT((int)strlen(command),
                        hal_net_console_read(out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING(command, out);
}

void test_wrong_password_closes_session(void) {
  install_callbacks();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_net_console_start(2327u, "secret"));
  hal_tcp_listener_t listener = hal_mock_tcp_listener_find_by_port(2327u);
  TEST_ASSERT_NOT_NULL(listener);

  hal_net_endpoint_t remote = make_endpoint(70u, 53070u);
  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote));
  hal_net_console_poll();
  hal_tcp_socket_t socket = hal_mock_tcp_get_last_accepted_socket();
  TEST_ASSERT_NOT_NULL(socket);

  hal_mock_tcp_inject_rx(socket, (const uint8_t *)"bad\n", 4u);
  hal_net_console_poll();
  TEST_ASSERT_FALSE(hal_tcp_socket_is_connected(socket));
  TEST_ASSERT_EQUAL_UINT(1u, s_connect_events);
  TEST_ASSERT_EQUAL_UINT(0u, s_auth_events);
  TEST_ASSERT_EQUAL_UINT(1u, s_disconnect_events);
}

void test_explicit_write_requires_authenticated_client(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_net_console_start(2328u, "pw"));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_net_console_write_text("nobody"));

  hal_tcp_socket_t socket = connect_client(2328u, 80u, 53080u, "pw");
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_net_console_write_text("hello\r\n"));
  hal_net_console_poll();
  assert_tx_contains(socket, "hello\r\n");
}

void test_event_callbacks_report_connect_and_auth(void) {
  install_callbacks();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_net_console_start(2329u, "pw"));
  (void)connect_client(2329u, 90u, 53090u, "pw");

  TEST_ASSERT_EQUAL_UINT(1u, s_connect_events);
  TEST_ASSERT_EQUAL_UINT(1u, s_auth_events);
  TEST_ASSERT_EQUAL_UINT(0u, s_disconnect_events);
  TEST_ASSERT_EQUAL_UINT8(0u, s_last_event_client);
  TEST_ASSERT_EQUAL_INT(HAL_NET_CONSOLE_EVENT_AUTHENTICATED, s_last_event);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_start_requires_nonempty_password);
  RUN_TEST(test_authentication_gate_keeps_logs_from_unauthenticated_clients);
  RUN_TEST(test_multiple_authenticated_clients_receive_serial_mirror);
  RUN_TEST(test_bidirectional_input_is_available_and_delivered_as_lines);
  RUN_TEST(test_wrong_password_closes_session);
  RUN_TEST(test_explicit_write_requires_authenticated_client);
  RUN_TEST(test_event_callbacks_report_connect_and_auth);
  return UNITY_END();
}
