#ifndef JH_HTTP_SERVER_TEST_HELPERS_H
#define JH_HTTP_SERVER_TEST_HELPERS_H

#include "hal/impl/.mock/hal_mock.h"
#include "hal/network/http/hal_http_server.h"
#include "network_test_helpers.h"
#include "utils/unity.h"

#include <string.h>

static hal_tcp_socket_t accept_http_client(uint16_t port) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_http_server_start(port));
  hal_tcp_listener_t listener = hal_mock_tcp_listener_find_by_port(port);
  TEST_ASSERT_NOT_NULL(listener);

  hal_net_endpoint_t remote = make_endpoint(192u, 168u, 1u, 50u, 51000u);
  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote));
  hal_http_server_poll();

  hal_tcp_socket_t socket = hal_mock_tcp_get_last_accepted_socket();
  TEST_ASSERT_NOT_NULL(socket);
  return socket;
}

static void inject_http_request(hal_tcp_socket_t socket, const char *request) {
  hal_mock_tcp_inject_rx(socket, (const uint8_t *)request,
                         (uint16_t)strlen(request));
  hal_http_server_poll();
}

static hal_tcp_socket_t send_http_request(uint16_t port, const char *request) {
  hal_tcp_socket_t socket = accept_http_client(port);
  inject_http_request(socket, request);
  return socket;
}

static void assert_response_contains(hal_tcp_socket_t socket,
                                     const char *needle) {
  char text[768];
  const uint16_t len = hal_mock_tcp_get_last_tx_len(socket);
  TEST_ASSERT_LESS_THAN(sizeof(text), len);
  memcpy(text, hal_mock_tcp_get_last_tx_payload(socket), len);
  text[len] = '\0';
  TEST_ASSERT_NOT_NULL(strstr(text, needle));
}

#endif
