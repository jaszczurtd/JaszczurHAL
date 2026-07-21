#include "hal/hal_tcp.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <string.h>

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_net_reset();
  hal_mock_tcp_reset();
}

void tearDown(void) {}

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

static hal_net_endpoint_t make_ipv6_endpoint(uint16_t port, uint32_t scope_id) {
  static const uint8_t address[HAL_NET_IPV6_ADDR_LEN] = {
      0x20u, 0x01u, 0x0du, 0xb8u, 0u, 0u, 0u, 0u,
      0u,    0u,    0u,    0u,    0u, 0u, 0u, 0x42u};
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET6;
  endpoint.addr_len = HAL_NET_IPV6_ADDR_LEN;
  memcpy(endpoint.addr, address, sizeof(address));
  endpoint.port = port;
  endpoint.scope_id = scope_id;
  return endpoint;
}

void test_connect_validates_family_length_scope_and_keeps_ipv6(void) {
  const hal_net_capabilities_t dual =
      HAL_NET_CAP_IPV4 | HAL_NET_CAP_IPV6 | HAL_NET_CAP_DUAL_STACK;
  hal_tcp_socket_t socket = hal_tcp_socket_open();
  TEST_ASSERT_NOT_NULL(socket);
  hal_net_endpoint_t remote6 = make_ipv6_endpoint(443u, 9u);
  hal_net_endpoint_t malformed = make_endpoint(192u, 0u, 2u, 1u, 443u);
  malformed.addr_len = HAL_NET_IPV6_ADDR_LEN;

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_tcp_socket_connect_ex(socket, &malformed, 100u));
  malformed = make_endpoint(192u, 0u, 2u, 1u, 443u);
  malformed.scope_id = 1u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_tcp_socket_connect_ex(socket, &malformed, 100u));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_tcp_socket_connect_ex(socket, &remote6, 100u));

  TEST_ASSERT_TRUE(hal_mock_net_set_capabilities(dual));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_tcp_socket_connect_ex(socket, &remote6, 100u));
  hal_net_endpoint_t captured = {};
  TEST_ASSERT_TRUE(hal_mock_tcp_get_remote_endpoint(socket, &captured));
  TEST_ASSERT_EQUAL_INT(HAL_NET_AF_INET6, captured.family);
  TEST_ASSERT_EQUAL_UINT8(HAL_NET_IPV6_ADDR_LEN, captured.addr_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(remote6.addr, captured.addr,
                                HAL_NET_IPV6_ADDR_LEN);
  TEST_ASSERT_EQUAL_UINT16(443u, captured.port);
  TEST_ASSERT_EQUAL_UINT32(9u, captured.scope_id);
  hal_tcp_socket_close(socket);
}

void test_connect_success_records_remote_endpoint(void) {
  hal_tcp_socket_t socket = hal_tcp_socket_open();
  TEST_ASSERT_NOT_NULL(socket);

  hal_net_endpoint_t remote = make_endpoint(192u, 168u, 4u, 10u, 1883u);
  hal_net_endpoint_t captured = {};

  TEST_ASSERT_TRUE(hal_tcp_socket_connect(socket, &remote, 250u));
  TEST_ASSERT_TRUE(hal_tcp_socket_is_connected(socket));
  TEST_ASSERT_TRUE(hal_mock_tcp_get_remote_endpoint(socket, &captured));
  TEST_ASSERT_EQUAL_INT(HAL_NET_AF_INET, captured.family);
  TEST_ASSERT_EQUAL_UINT8(192u, captured.addr[0]);
  TEST_ASSERT_EQUAL_UINT8(10u, captured.addr[3]);
  TEST_ASSERT_EQUAL_UINT16(1883u, captured.port);

  hal_tcp_socket_close(socket);
}

void test_connect_failure_leaves_socket_disconnected(void) {
  hal_tcp_socket_t socket = hal_tcp_socket_open();
  TEST_ASSERT_NOT_NULL(socket);

  hal_net_endpoint_t remote = make_endpoint(203u, 0u, 113u, 5u, 443u);
  hal_mock_tcp_set_connect_result(false);

  TEST_ASSERT_FALSE(hal_tcp_socket_connect(socket, &remote, 10u));
  TEST_ASSERT_FALSE(hal_tcp_socket_is_connected(socket));

  hal_tcp_socket_close(socket);
}

void test_send_captures_payload(void) {
  const uint8_t payload[] = {'p', 'i', 'n', 'g'};
  hal_tcp_socket_t socket = hal_tcp_socket_open();
  TEST_ASSERT_NOT_NULL(socket);

  hal_net_endpoint_t remote = make_endpoint(10u, 0u, 0u, 15u, 9000u);
  TEST_ASSERT_TRUE(hal_tcp_socket_connect(socket, &remote, 100u));

  TEST_ASSERT_EQUAL_INT((int)sizeof(payload),
                        hal_tcp_socket_send(socket, payload, sizeof(payload)));
  TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(payload),
                           hal_mock_tcp_get_last_tx_len(socket));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      payload, hal_mock_tcp_get_last_tx_payload(socket), sizeof(payload));

  hal_tcp_socket_close(socket);
}

void test_recv_reads_in_chunks(void) {
  const uint8_t payload[] = {'a', 'b', 'c', 'd', 'e'};
  uint8_t first[2] = {0};
  uint8_t second[4] = {0};
  hal_tcp_socket_t socket = hal_tcp_socket_open();
  TEST_ASSERT_NOT_NULL(socket);

  hal_net_endpoint_t remote = make_endpoint(10u, 0u, 0u, 20u, 7000u);
  TEST_ASSERT_TRUE(hal_tcp_socket_connect(socket, &remote, 100u));

  hal_mock_tcp_inject_rx(socket, payload, (uint16_t)sizeof(payload));
  TEST_ASSERT_EQUAL_INT(2,
                        hal_tcp_socket_recv(socket, first, sizeof(first), 0u));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, first, sizeof(first));
  TEST_ASSERT_EQUAL_INT(
      3, hal_tcp_socket_recv(socket, second, sizeof(second), 0u));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload + 2u, second, 3u);

  hal_tcp_socket_close(socket);
}

void test_recv_timeout_returns_zero_when_no_data(void) {
  uint8_t out[4] = {0};
  hal_tcp_socket_t socket = hal_tcp_socket_open();
  TEST_ASSERT_NOT_NULL(socket);

  hal_net_endpoint_t remote = make_endpoint(10u, 1u, 2u, 3u, 7001u);
  TEST_ASSERT_TRUE(hal_tcp_socket_connect(socket, &remote, 100u));

  TEST_ASSERT_EQUAL_INT(0, hal_tcp_socket_recv(socket, out, sizeof(out), 5u));
  TEST_ASSERT_TRUE(hal_tcp_socket_is_connected(socket));

  hal_tcp_socket_close(socket);
}

void test_shutdown_disconnects_but_keeps_handle_allocated(void) {
  const uint8_t payload[] = {0x42u};
  hal_tcp_socket_t socket = hal_tcp_socket_open();
  TEST_ASSERT_NOT_NULL(socket);

  hal_net_endpoint_t remote = make_endpoint(10u, 2u, 3u, 4u, 7002u);
  TEST_ASSERT_TRUE(hal_tcp_socket_connect(socket, &remote, 100u));

  hal_tcp_socket_shutdown(socket);
  TEST_ASSERT_FALSE(hal_tcp_socket_is_connected(socket));
  TEST_ASSERT_EQUAL_INT(-1,
                        hal_tcp_socket_send(socket, payload, sizeof(payload)));

  TEST_ASSERT_TRUE(hal_tcp_socket_connect(socket, &remote, 100u));
  TEST_ASSERT_TRUE(hal_tcp_socket_is_connected(socket));

  hal_tcp_socket_close(socket);
}

void test_closed_socket_use_is_rejected(void) {
  const uint8_t payload[] = {0x11u};
  uint8_t out[1] = {0};
  hal_tcp_socket_t socket = hal_tcp_socket_open();
  TEST_ASSERT_NOT_NULL(socket);

  hal_net_endpoint_t remote = make_endpoint(198u, 51u, 100u, 9u, 80u);
  TEST_ASSERT_TRUE(hal_tcp_socket_connect(socket, &remote, 100u));

  hal_tcp_socket_close(socket);
  TEST_ASSERT_FALSE(hal_tcp_socket_connect(socket, &remote, 100u));
  TEST_ASSERT_FALSE(hal_tcp_socket_is_connected(socket));
  TEST_ASSERT_EQUAL_INT(-1,
                        hal_tcp_socket_send(socket, payload, sizeof(payload)));
  TEST_ASSERT_EQUAL_INT(-1, hal_tcp_socket_recv(socket, out, sizeof(out), 0u));
}

void test_socket_pool_limit_and_reuse_after_close(void) {
  hal_tcp_socket_t sockets[HAL_TCP_SOCKET_MAX_INSTANCES] = {};

  for (size_t i = 0u; i < HAL_TCP_SOCKET_MAX_INSTANCES; ++i) {
    sockets[i] = hal_tcp_socket_open();
    TEST_ASSERT_NOT_NULL(sockets[i]);
  }

  TEST_ASSERT_NULL(hal_tcp_socket_open());

  hal_tcp_socket_close(sockets[1]);
  sockets[1] = hal_tcp_socket_open();
  TEST_ASSERT_NOT_NULL(sockets[1]);

  for (size_t i = 0u; i < HAL_TCP_SOCKET_MAX_INSTANCES; ++i) {
    hal_tcp_socket_close(sockets[i]);
  }
}

void test_socket_api_rejects_invalid_inputs(void) {
  const uint8_t payload[] = {0x21u};
  uint8_t out[1] = {0};
  hal_tcp_socket_t socket = hal_tcp_socket_open();
  TEST_ASSERT_NOT_NULL(socket);

  hal_net_endpoint_t remote = make_endpoint(192u, 0u, 2u, 50u, 8000u);
  hal_net_endpoint_t bad_remote = remote;
  bad_remote.family = HAL_NET_AF_UNSPEC;

  TEST_ASSERT_FALSE(hal_tcp_socket_connect(NULL, &remote, 100u));
  TEST_ASSERT_FALSE(hal_tcp_socket_connect(socket, NULL, 100u));
  TEST_ASSERT_FALSE(hal_tcp_socket_connect(socket, &bad_remote, 100u));
  TEST_ASSERT_TRUE(hal_tcp_socket_connect(socket, &remote, 100u));
  TEST_ASSERT_EQUAL_INT(-1, hal_tcp_socket_send(socket, NULL, 1u));
  TEST_ASSERT_EQUAL_INT(-1, hal_tcp_socket_recv(socket, NULL, 1u, 0u));
  TEST_ASSERT_EQUAL_INT(0, hal_tcp_socket_send(socket, payload, 0u));
  TEST_ASSERT_EQUAL_INT(0, hal_tcp_socket_recv(socket, out, 0u, 0u));

  hal_tcp_socket_close(socket);
}

void test_multiple_listeners_keep_pending_clients_separate(void) {
  hal_tcp_listener_t listener_a = hal_tcp_listener_open();
  hal_tcp_listener_t listener_b = hal_tcp_listener_open();
  TEST_ASSERT_NOT_NULL(listener_a);
  TEST_ASSERT_NOT_NULL(listener_b);

  hal_net_endpoint_t local_a = make_endpoint(0u, 0u, 0u, 0u, 8101u);
  hal_net_endpoint_t local_b = make_endpoint(0u, 0u, 0u, 0u, 8102u);
  hal_net_endpoint_t remote_a = make_endpoint(192u, 168u, 10u, 1u, 5101u);
  hal_net_endpoint_t remote_b = make_endpoint(192u, 168u, 10u, 2u, 5102u);
  hal_net_endpoint_t accepted_remote = {};

  TEST_ASSERT_TRUE(hal_tcp_listener_bind(listener_a, &local_a));
  TEST_ASSERT_TRUE(hal_tcp_listener_bind(listener_b, &local_b));
  TEST_ASSERT_TRUE(hal_tcp_listener_listen(listener_a, 2u));
  TEST_ASSERT_TRUE(hal_tcp_listener_listen(listener_b, 2u));
  TEST_ASSERT_EQUAL_UINT16(8101u,
                           hal_mock_tcp_listener_get_local_port(listener_a));
  TEST_ASSERT_EQUAL_UINT16(8102u,
                           hal_mock_tcp_listener_get_local_port(listener_b));

  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener_a, &remote_a));
  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener_b, &remote_b));

  hal_tcp_socket_t socket_b =
      hal_tcp_listener_accept(listener_b, &accepted_remote, 0u);
  TEST_ASSERT_NOT_NULL(socket_b);
  TEST_ASSERT_TRUE(hal_tcp_socket_is_connected(socket_b));
  TEST_ASSERT_EQUAL_UINT8(2u, accepted_remote.addr[3]);
  TEST_ASSERT_EQUAL_UINT16(5102u, accepted_remote.port);

  hal_tcp_socket_t socket_a =
      hal_tcp_listener_accept(listener_a, &accepted_remote, 0u);
  TEST_ASSERT_NOT_NULL(socket_a);
  TEST_ASSERT_TRUE(hal_tcp_socket_is_connected(socket_a));
  TEST_ASSERT_EQUAL_UINT8(1u, accepted_remote.addr[3]);
  TEST_ASSERT_EQUAL_UINT16(5101u, accepted_remote.port);

  hal_tcp_socket_close(socket_a);
  hal_tcp_socket_close(socket_b);
  hal_tcp_listener_close(listener_a);
  hal_tcp_listener_close(listener_b);
}

void test_listener_backlog_limits_pending_clients(void) {
  hal_tcp_listener_t listener = hal_tcp_listener_open();
  TEST_ASSERT_NOT_NULL(listener);

  hal_net_endpoint_t local = make_endpoint(0u, 0u, 0u, 0u, 8200u);
  hal_net_endpoint_t remote_a = make_endpoint(10u, 0u, 0u, 1u, 5201u);
  hal_net_endpoint_t remote_b = make_endpoint(10u, 0u, 0u, 2u, 5202u);
  hal_net_endpoint_t accepted_remote = {};

  TEST_ASSERT_TRUE(hal_tcp_listener_bind(listener, &local));
  TEST_ASSERT_TRUE(hal_tcp_listener_listen(listener, 1u));
  TEST_ASSERT_EQUAL_UINT8(1u, hal_mock_tcp_listener_get_backlog(listener));

  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote_a));
  TEST_ASSERT_FALSE(hal_mock_tcp_listener_inject_client(listener, &remote_b));
  TEST_ASSERT_EQUAL_UINT8(1u,
                          hal_mock_tcp_listener_get_pending_count(listener));

  hal_tcp_socket_t socket =
      hal_tcp_listener_accept(listener, &accepted_remote, 0u);
  TEST_ASSERT_NOT_NULL(socket);
  TEST_ASSERT_EQUAL_UINT16(5201u, accepted_remote.port);
  TEST_ASSERT_EQUAL_UINT8(0u,
                          hal_mock_tcp_listener_get_pending_count(listener));

  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote_b));

  hal_tcp_socket_close(socket);
  socket = hal_tcp_listener_accept(listener, &accepted_remote, 0u);
  TEST_ASSERT_NOT_NULL(socket);
  TEST_ASSERT_EQUAL_UINT16(5202u, accepted_remote.port);

  hal_tcp_socket_close(socket);
  hal_tcp_listener_close(listener);
}

void test_listener_accept_without_client_returns_null(void) {
  hal_tcp_listener_t listener = hal_tcp_listener_open();
  TEST_ASSERT_NOT_NULL(listener);

  hal_net_endpoint_t local = make_endpoint(0u, 0u, 0u, 0u, 8300u);
  TEST_ASSERT_TRUE(hal_tcp_listener_bind(listener, &local));
  TEST_ASSERT_TRUE(hal_tcp_listener_listen(listener, 2u));

  TEST_ASSERT_NULL(hal_tcp_listener_accept(listener, NULL, 0u));
  TEST_ASSERT_NULL(hal_tcp_listener_accept(listener, NULL, 5u));

  hal_tcp_listener_close(listener);
}

void test_listener_close_keeps_accepted_socket_open(void) {
  const uint8_t tx_payload[] = {'o', 'k'};
  const uint8_t rx_payload[] = {'h', 'i'};
  uint8_t out[2] = {0};
  hal_tcp_listener_t listener = hal_tcp_listener_open();
  TEST_ASSERT_NOT_NULL(listener);

  hal_net_endpoint_t local = make_endpoint(0u, 0u, 0u, 0u, 8400u);
  hal_net_endpoint_t remote = make_endpoint(203u, 0u, 113u, 55u, 5400u);
  hal_net_endpoint_t accepted_remote = {};

  TEST_ASSERT_TRUE(hal_tcp_listener_bind(listener, &local));
  TEST_ASSERT_TRUE(hal_tcp_listener_listen(listener, 2u));
  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote));

  hal_tcp_socket_t socket =
      hal_tcp_listener_accept(listener, &accepted_remote, 0u);
  TEST_ASSERT_NOT_NULL(socket);
  TEST_ASSERT_EQUAL_UINT16(5400u, accepted_remote.port);

  hal_tcp_listener_close(listener);
  TEST_ASSERT_TRUE(hal_tcp_socket_is_connected(socket));
  TEST_ASSERT_FALSE(hal_mock_tcp_listener_inject_client(listener, &remote));
  TEST_ASSERT_EQUAL_INT(
      (int)sizeof(tx_payload),
      hal_tcp_socket_send(socket, tx_payload, sizeof(tx_payload)));
  hal_mock_tcp_inject_rx(socket, rx_payload, (uint16_t)sizeof(rx_payload));
  TEST_ASSERT_EQUAL_INT((int)sizeof(rx_payload),
                        hal_tcp_socket_recv(socket, out, sizeof(out), 0u));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rx_payload, out, sizeof(rx_payload));

  hal_tcp_socket_close(socket);
}

void test_listener_pool_limit_and_reuse_after_close(void) {
  hal_tcp_listener_t listeners[HAL_TCP_LISTENER_MAX_INSTANCES] = {};

  for (size_t i = 0u; i < HAL_TCP_LISTENER_MAX_INSTANCES; ++i) {
    listeners[i] = hal_tcp_listener_open();
    TEST_ASSERT_NOT_NULL(listeners[i]);
  }

  TEST_ASSERT_NULL(hal_tcp_listener_open());

  hal_tcp_listener_close(listeners[0]);
  listeners[0] = hal_tcp_listener_open();
  TEST_ASSERT_NOT_NULL(listeners[0]);

  for (size_t i = 0u; i < HAL_TCP_LISTENER_MAX_INSTANCES; ++i) {
    hal_tcp_listener_close(listeners[i]);
  }
}

void test_listener_api_rejects_invalid_inputs(void) {
  hal_tcp_listener_t listener = hal_tcp_listener_open();
  TEST_ASSERT_NOT_NULL(listener);

  hal_net_endpoint_t local = make_endpoint(0u, 0u, 0u, 0u, 8500u);
  hal_net_endpoint_t bad_local = local;
  bad_local.family = HAL_NET_AF_UNSPEC;

  TEST_ASSERT_FALSE(hal_tcp_listener_bind(NULL, &local));
  TEST_ASSERT_FALSE(hal_tcp_listener_bind(listener, NULL));
  TEST_ASSERT_FALSE(hal_tcp_listener_bind(listener, &bad_local));
  TEST_ASSERT_FALSE(hal_tcp_listener_listen(listener, 1u));
  TEST_ASSERT_TRUE(hal_tcp_listener_bind(listener, &local));
  TEST_ASSERT_FALSE(hal_tcp_listener_listen(listener, 0u));
  TEST_ASSERT_TRUE(hal_tcp_listener_listen(listener, 1u));

  hal_tcp_listener_close(listener);
  TEST_ASSERT_NULL(hal_tcp_listener_accept(listener, NULL, 0u));
  TEST_ASSERT_FALSE(hal_tcp_listener_bind(listener, &local));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_connect_validates_family_length_scope_and_keeps_ipv6);
  RUN_TEST(test_connect_success_records_remote_endpoint);
  RUN_TEST(test_connect_failure_leaves_socket_disconnected);
  RUN_TEST(test_send_captures_payload);
  RUN_TEST(test_recv_reads_in_chunks);
  RUN_TEST(test_recv_timeout_returns_zero_when_no_data);
  RUN_TEST(test_shutdown_disconnects_but_keeps_handle_allocated);
  RUN_TEST(test_closed_socket_use_is_rejected);
  RUN_TEST(test_socket_pool_limit_and_reuse_after_close);
  RUN_TEST(test_socket_api_rejects_invalid_inputs);
  RUN_TEST(test_multiple_listeners_keep_pending_clients_separate);
  RUN_TEST(test_listener_backlog_limits_pending_clients);
  RUN_TEST(test_listener_accept_without_client_returns_null);
  RUN_TEST(test_listener_close_keeps_accepted_socket_open);
  RUN_TEST(test_listener_pool_limit_and_reuse_after_close);
  RUN_TEST(test_listener_api_rejects_invalid_inputs);
  return UNITY_END();
}
