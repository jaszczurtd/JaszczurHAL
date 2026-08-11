#include "hal/impl/.mock/hal_mock.h"
#include "hal/network/hal_udp.h"
#include "support/network_test_helpers.h"
#include "utils/unity.h"

#include <string.h>

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_net_reset();
  hal_mock_udp_reset();
}

void tearDown(void) {}

static hal_net_endpoint_t make_ipv6_endpoint(uint16_t port, uint32_t scope_id) {
  static const uint8_t address[HAL_NET_IPV6_ADDR_LEN] = {
      0xfeu, 0x80u, 0u, 0u, 0u, 0u, 0u,    0u,
      0u,    0u,    0u, 0u, 0u, 0u, 0x12u, 0x34u};
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET6;
  endpoint.addr_len = HAL_NET_IPV6_ADDR_LEN;
  memcpy(endpoint.addr, address, sizeof(address));
  endpoint.port = port;
  endpoint.scope_id = scope_id;
  return endpoint;
}

void test_socket_validates_shape_and_keeps_full_ipv6_endpoints(void) {
  const uint8_t payload[] = {'v', '6'};
  const hal_net_capabilities_t dual =
      HAL_NET_CAP_IPV4 | HAL_NET_CAP_IPV6 | HAL_NET_CAP_DUAL_STACK;
  hal_udp_socket_t socket = hal_udp_socket_open();
  TEST_ASSERT_NOT_NULL(socket);
  hal_net_endpoint_t local6 = make_ipv6_endpoint(5300u, 9u);
  memset(local6.addr, 0, sizeof(local6.addr));
  hal_net_endpoint_t remote6 = make_ipv6_endpoint(9000u, 9u);
  hal_net_endpoint_t malformed = make_endpoint(192u, 0u, 2u, 1u, 9000u);
  malformed.addr_len = HAL_NET_IPV6_ADDR_LEN;
  size_t sent = 0u;

  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_udp_socket_sendto_ex(socket, payload, sizeof(payload),
                                           &malformed, &sent));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_udp_socket_bind_ex(socket, &local6));
  TEST_ASSERT_TRUE(hal_mock_net_set_capabilities(dual));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_bind_ex(socket, &local6));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_sendto_ex(socket, payload,
                                                         sizeof(payload),
                                                         &remote6, &sent));
  TEST_ASSERT_EQUAL_UINT32(sizeof(payload), sent);
  hal_net_endpoint_t captured = {};
  TEST_ASSERT_TRUE(hal_mock_udp_get_last_tx_remote_for(socket, &captured));
  TEST_ASSERT_EQUAL_INT(HAL_NET_AF_INET6, captured.family);
  TEST_ASSERT_EQUAL_UINT8(HAL_NET_IPV6_ADDR_LEN, captured.addr_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(remote6.addr, captured.addr,
                                HAL_NET_IPV6_ADDR_LEN);
  TEST_ASSERT_EQUAL_UINT16(9000u, captured.port);
  TEST_ASSERT_EQUAL_UINT32(9u, captured.scope_id);

  hal_mock_udp_inject_packet_to(socket, "fe80::beef%12", 9001u, payload,
                                (uint16_t)sizeof(payload));
  uint8_t received[2] = {};
  hal_net_endpoint_t sender = {};
  size_t received_len = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_udp_socket_recvfrom_ex(socket, received, sizeof(received),
                                         &sender, 0u, &received_len));
  TEST_ASSERT_EQUAL_UINT32(sizeof(payload), received_len);
  TEST_ASSERT_EQUAL_INT(HAL_NET_AF_INET6, sender.family);
  TEST_ASSERT_EQUAL_UINT8(HAL_NET_IPV6_ADDR_LEN, sender.addr_len);
  TEST_ASSERT_EQUAL_UINT8(0xfeu, sender.addr[0]);
  TEST_ASSERT_EQUAL_UINT8(0x80u, sender.addr[1]);
  TEST_ASSERT_EQUAL_UINT8(0xbeu, sender.addr[14]);
  TEST_ASSERT_EQUAL_UINT8(0xefu, sender.addr[15]);
  TEST_ASSERT_EQUAL_UINT32(12u, sender.scope_id);
  hal_udp_socket_close(socket);
}

void test_socket_handles_bind_and_receive_independently(void) {
  const uint8_t payload_a[] = {'a', '1'};
  const uint8_t payload_b[] = {'b', '2', '3'};
  uint8_t out_a[4] = {0};
  uint8_t out_b[4] = {0};
  hal_net_endpoint_t remote_a = {};
  hal_net_endpoint_t remote_b = {};

  hal_udp_socket_t socket_a = hal_udp_socket_open();
  hal_udp_socket_t socket_b = hal_udp_socket_open();
  TEST_ASSERT_NOT_NULL(socket_a);
  TEST_ASSERT_NOT_NULL(socket_b);

  hal_net_endpoint_t local_a = make_endpoint(0u, 0u, 0u, 0u, 4100u);
  hal_net_endpoint_t local_b = make_endpoint(0u, 0u, 0u, 0u, 4200u);
  TEST_ASSERT_TRUE(hal_udp_socket_bind(socket_a, &local_a));
  TEST_ASSERT_TRUE(hal_udp_socket_bind(socket_b, &local_b));
  TEST_ASSERT_EQUAL_UINT16(4100u, hal_mock_udp_get_local_port_for(socket_a));
  TEST_ASSERT_EQUAL_UINT16(4200u, hal_mock_udp_get_local_port_for(socket_b));

  hal_mock_udp_inject_packet_to(socket_a, "10.0.0.1", 6001u, payload_a,
                                (uint16_t)sizeof(payload_a));
  hal_mock_udp_inject_packet_to(socket_b, "10.0.0.2", 6002u, payload_b,
                                (uint16_t)sizeof(payload_b));

  TEST_ASSERT_EQUAL_INT(
      (int)sizeof(payload_b),
      hal_udp_socket_recvfrom(socket_b, out_b, sizeof(out_b), &remote_b, 0u));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_b, out_b, sizeof(payload_b));
  TEST_ASSERT_EQUAL_UINT8(10u, remote_b.addr[0]);
  TEST_ASSERT_EQUAL_UINT8(2u, remote_b.addr[3]);
  TEST_ASSERT_EQUAL_UINT16(6002u, remote_b.port);

  TEST_ASSERT_EQUAL_INT(
      (int)sizeof(payload_a),
      hal_udp_socket_recvfrom(socket_a, out_a, sizeof(out_a), &remote_a, 0u));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_a, out_a, sizeof(payload_a));
  TEST_ASSERT_EQUAL_UINT8(10u, remote_a.addr[0]);
  TEST_ASSERT_EQUAL_UINT8(1u, remote_a.addr[3]);
  TEST_ASSERT_EQUAL_UINT16(6001u, remote_a.port);

  TEST_ASSERT_EQUAL_INT(
      0, hal_udp_socket_recvfrom(socket_a, out_a, sizeof(out_a), NULL, 0u));

  hal_udp_socket_close(socket_a);
  hal_udp_socket_close(socket_b);
}

void test_socket_sendto_keeps_independent_tx_state(void) {
  const uint8_t payload_a[] = {'A', 'A'};
  const uint8_t payload_b[] = {'B', 'B', 'B'};
  hal_net_endpoint_t tx_remote_a = {};
  hal_net_endpoint_t tx_remote_b = {};

  hal_udp_socket_t socket_a = hal_udp_socket_open();
  hal_udp_socket_t socket_b = hal_udp_socket_open();
  TEST_ASSERT_NOT_NULL(socket_a);
  TEST_ASSERT_NOT_NULL(socket_b);

  hal_net_endpoint_t local_a = make_endpoint(0u, 0u, 0u, 0u, 5100u);
  hal_net_endpoint_t local_b = make_endpoint(0u, 0u, 0u, 0u, 5200u);
  hal_net_endpoint_t remote_a = make_endpoint(192u, 168u, 10u, 10u, 7001u);
  hal_net_endpoint_t remote_b = make_endpoint(192u, 168u, 10u, 20u, 7002u);

  TEST_ASSERT_TRUE(hal_udp_socket_bind(socket_a, &local_a));
  TEST_ASSERT_TRUE(hal_udp_socket_bind(socket_b, &local_b));

  TEST_ASSERT_EQUAL_INT(
      (int)sizeof(payload_a),
      hal_udp_socket_sendto(socket_a, payload_a, sizeof(payload_a), &remote_a));
  TEST_ASSERT_EQUAL_INT(
      (int)sizeof(payload_b),
      hal_udp_socket_sendto(socket_b, payload_b, sizeof(payload_b), &remote_b));

  TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(payload_a),
                           hal_mock_udp_get_last_tx_len_for(socket_a));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_a,
                                hal_mock_udp_get_last_tx_payload_for(socket_a),
                                sizeof(payload_a));
  TEST_ASSERT_TRUE(hal_mock_udp_get_last_tx_remote_for(socket_a, &tx_remote_a));
  TEST_ASSERT_EQUAL_UINT8(10u, tx_remote_a.addr[3]);
  TEST_ASSERT_EQUAL_UINT16(7001u, tx_remote_a.port);

  TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(payload_b),
                           hal_mock_udp_get_last_tx_len_for(socket_b));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_b,
                                hal_mock_udp_get_last_tx_payload_for(socket_b),
                                sizeof(payload_b));
  TEST_ASSERT_TRUE(hal_mock_udp_get_last_tx_remote_for(socket_b, &tx_remote_b));
  TEST_ASSERT_EQUAL_UINT8(20u, tx_remote_b.addr[3]);
  TEST_ASSERT_EQUAL_UINT16(7002u, tx_remote_b.port);

  hal_udp_socket_close(socket_a);
  hal_udp_socket_close(socket_b);
}

void test_socket_pool_limit_and_reuse_after_close(void) {
  hal_udp_socket_t sockets[HAL_UDP_SOCKET_MAX_INSTANCES] = {};

  for (size_t i = 0u; i < HAL_UDP_SOCKET_MAX_INSTANCES; ++i) {
    sockets[i] = hal_udp_socket_open();
    TEST_ASSERT_NOT_NULL(sockets[i]);
  }

  TEST_ASSERT_NULL(hal_udp_socket_open());

  hal_udp_socket_close(sockets[1]);
  sockets[1] = hal_udp_socket_open();
  TEST_ASSERT_NOT_NULL(sockets[1]);

  for (size_t i = 0u; i < HAL_UDP_SOCKET_MAX_INSTANCES; ++i) {
    hal_udp_socket_close(sockets[i]);
  }
}

void test_socket_api_rejects_invalid_inputs(void) {
  const uint8_t payload[] = {0x42};
  uint8_t out[1] = {0};
  hal_udp_socket_t socket = hal_udp_socket_open();
  TEST_ASSERT_NOT_NULL(socket);

  hal_net_endpoint_t local = make_endpoint(0u, 0u, 0u, 0u, 5300u);
  hal_net_endpoint_t remote = make_endpoint(203u, 0u, 113u, 9u, 9000u);
  hal_net_endpoint_t bad_remote = remote;
  bad_remote.family = HAL_NET_AF_UNSPEC;

  TEST_ASSERT_FALSE(hal_udp_socket_bind(NULL, &local));
  TEST_ASSERT_FALSE(hal_udp_socket_bind(socket, NULL));
  TEST_ASSERT_TRUE(hal_udp_socket_bind(socket, &local));
  TEST_ASSERT_EQUAL_INT(-1, hal_udp_socket_sendto(socket, NULL, 1u, &remote));
  TEST_ASSERT_EQUAL_INT(
      -1, hal_udp_socket_sendto(socket, payload, sizeof(payload), &bad_remote));
  TEST_ASSERT_EQUAL_INT(
      -1, hal_udp_socket_recvfrom(socket, NULL, sizeof(out), NULL, 0u));

  hal_udp_socket_close(socket);
  TEST_ASSERT_EQUAL_INT(
      -1, hal_udp_socket_sendto(socket, payload, sizeof(payload), &remote));
}

void test_begin_receive_and_remote_endpoint(void) {
  const uint8_t payload[] = {0x11, 0x22, 0x33, 0x44};
  uint8_t out[8] = {0};
  char remote_ip[HAL_UDP_IP_STR_LEN] = {0};

  TEST_ASSERT_TRUE(hal_udp_begin(12345u));
  TEST_ASSERT_EQUAL_UINT16(12345u, hal_mock_udp_get_local_port());

  hal_mock_udp_inject_packet("192.168.1.50", 4444u, payload,
                             (uint16_t)sizeof(payload));
  TEST_ASSERT_EQUAL_INT((int)sizeof(payload), hal_udp_parse_packet());

  TEST_ASSERT_TRUE(hal_udp_remote_ip(remote_ip, sizeof(remote_ip)));
  TEST_ASSERT_EQUAL_STRING("192.168.1.50", remote_ip);
  TEST_ASSERT_EQUAL_UINT16(4444u, hal_udp_remote_port());

  TEST_ASSERT_EQUAL_INT((int)sizeof(payload), hal_udp_read(out, sizeof(out)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out, sizeof(payload));
  TEST_ASSERT_EQUAL_INT(0, hal_udp_parse_packet());
}

void test_send_to_explicit_host_collects_payload(void) {
  const uint8_t prefix[] = {0xAA, 0xBB};
  const uint8_t expected[] = {0xAA, 0xBB, 'O', 'K'};

  TEST_ASSERT_TRUE(hal_udp_begin(15000u));
  TEST_ASSERT_TRUE(hal_udp_begin_packet("10.0.0.12", 7777u));

  TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(prefix),
                           hal_udp_write(prefix, (uint16_t)sizeof(prefix)));
  TEST_ASSERT_EQUAL_UINT16(2u, hal_udp_write_str("OK"));

  TEST_ASSERT_TRUE(hal_udp_end_packet());
  TEST_ASSERT_TRUE(hal_mock_udp_was_end_packet_called());

  TEST_ASSERT_EQUAL_STRING("10.0.0.12",
                           hal_mock_udp_get_last_begin_packet_host());
  TEST_ASSERT_EQUAL_UINT16(7777u, hal_mock_udp_get_last_begin_packet_port());
  TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(expected),
                           hal_mock_udp_get_last_tx_len());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, hal_mock_udp_get_last_tx_payload(),
                                sizeof(expected));
}

void test_send_to_last_remote_sender(void) {
  const uint8_t ping[] = {'p', 'i', 'n', 'g'};
  uint8_t discard[8] = {0};

  TEST_ASSERT_TRUE(hal_udp_begin(9000u));

  hal_mock_udp_inject_packet("172.16.0.9", 5050u, ping, (uint16_t)sizeof(ping));
  TEST_ASSERT_EQUAL_INT((int)sizeof(ping), hal_udp_parse_packet());
  TEST_ASSERT_EQUAL_INT((int)sizeof(ping),
                        hal_udp_read(discard, sizeof(discard)));

  TEST_ASSERT_TRUE(hal_udp_begin_packet_remote());
  TEST_ASSERT_EQUAL_UINT16(4u, hal_udp_write_str("pong"));
  TEST_ASSERT_TRUE(hal_udp_end_packet());

  TEST_ASSERT_EQUAL_STRING("172.16.0.9",
                           hal_mock_udp_get_last_begin_packet_host());
  TEST_ASSERT_EQUAL_UINT16(5050u, hal_mock_udp_get_last_begin_packet_port());
}

void test_read_in_chunks_consumes_packet(void) {
  const uint8_t payload[] = {'A', 'B', 'C', 'D', 'E'};
  uint8_t chunk_a[2] = {0};
  uint8_t chunk_b[8] = {0};

  TEST_ASSERT_TRUE(hal_udp_begin(9100u));
  hal_mock_udp_inject_packet("10.1.2.3", 6000u, payload,
                             (uint16_t)sizeof(payload));

  TEST_ASSERT_EQUAL_INT((int)sizeof(payload), hal_udp_parse_packet());

  TEST_ASSERT_EQUAL_INT(2, hal_udp_read(chunk_a, (uint16_t)sizeof(chunk_a)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, chunk_a, sizeof(chunk_a));

  TEST_ASSERT_EQUAL_INT(3, hal_udp_read(chunk_b, (uint16_t)sizeof(chunk_b)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload + sizeof(chunk_a), chunk_b, 3);

  TEST_ASSERT_EQUAL_INT(0, hal_udp_read(chunk_b, (uint16_t)sizeof(chunk_b)));
  TEST_ASSERT_EQUAL_INT(0, hal_udp_parse_packet());
}

void test_stop_clears_remote_and_packet_state(void) {
  const uint8_t payload[] = {0x01, 0x02, 0x03};
  char out_ip[HAL_UDP_IP_STR_LEN] = {0};

  TEST_ASSERT_TRUE(hal_udp_begin(9200u));

  hal_mock_udp_inject_packet("203.0.113.10", 6500u, payload,
                             (uint16_t)sizeof(payload));
  TEST_ASSERT_EQUAL_INT((int)sizeof(payload), hal_udp_parse_packet());
  TEST_ASSERT_TRUE(hal_udp_remote_ip(out_ip, sizeof(out_ip)));
  TEST_ASSERT_EQUAL_UINT16(6500u, hal_udp_remote_port());

  TEST_ASSERT_TRUE(hal_udp_begin_packet("example.local", 7001u));
  TEST_ASSERT_EQUAL_UINT16(1u, hal_udp_write(payload, 1u));

  hal_udp_stop();

  memset(out_ip, 0, sizeof(out_ip));
  TEST_ASSERT_FALSE(hal_udp_remote_ip(out_ip, sizeof(out_ip)));
  TEST_ASSERT_EQUAL_STRING("0.0.0.0", out_ip);
  TEST_ASSERT_EQUAL_UINT16(0u, hal_udp_remote_port());
  TEST_ASSERT_FALSE(hal_udp_begin_packet_remote());
  TEST_ASSERT_FALSE(hal_udp_end_packet());
}

void test_invalid_inputs_are_rejected(void) {
  char out_ip[HAL_UDP_IP_STR_LEN] = {0};

  TEST_ASSERT_FALSE(hal_udp_begin(0u));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  TEST_ASSERT_TRUE(hal_udp_begin(12000u));

  hal_mock_serial_reset();
  TEST_ASSERT_EQUAL_INT(-1, hal_udp_read(NULL, 1u));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_udp_remote_ip(NULL, sizeof(out_ip)));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  TEST_ASSERT_FALSE(hal_udp_remote_ip(out_ip, sizeof(out_ip)));
  TEST_ASSERT_EQUAL_STRING("0.0.0.0", out_ip);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_udp_begin_packet(NULL, 7000u));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_udp_begin_packet("host", 0u));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_udp_begin_packet_remote());
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_EQUAL_UINT16(0u, hal_udp_write(NULL, 1u));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_EQUAL_UINT16(0u, hal_udp_write_str(NULL));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  TEST_ASSERT_FALSE(hal_udp_end_packet());
}

void test_end_packet_failure_is_propagated(void) {
  const uint8_t data[] = {0xEF};

  TEST_ASSERT_TRUE(hal_udp_begin(13000u));
  TEST_ASSERT_TRUE(hal_udp_begin_packet("192.168.10.2", 6060u));
  TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(data),
                           hal_udp_write(data, (uint16_t)sizeof(data)));

  hal_mock_udp_set_end_packet_result(false);
  TEST_ASSERT_FALSE(hal_udp_end_packet());
  TEST_ASSERT_TRUE(hal_mock_udp_was_end_packet_called());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_socket_validates_shape_and_keeps_full_ipv6_endpoints);
  RUN_TEST(test_socket_handles_bind_and_receive_independently);
  RUN_TEST(test_socket_sendto_keeps_independent_tx_state);
  RUN_TEST(test_socket_pool_limit_and_reuse_after_close);
  RUN_TEST(test_socket_api_rejects_invalid_inputs);
  RUN_TEST(test_begin_receive_and_remote_endpoint);
  RUN_TEST(test_send_to_explicit_host_collects_payload);
  RUN_TEST(test_send_to_last_remote_sender);
  RUN_TEST(test_read_in_chunks_consumes_packet);
  RUN_TEST(test_stop_clears_remote_and_packet_state);
  RUN_TEST(test_invalid_inputs_are_rejected);
  RUN_TEST(test_end_packet_failure_is_propagated);
  return UNITY_END();
}
