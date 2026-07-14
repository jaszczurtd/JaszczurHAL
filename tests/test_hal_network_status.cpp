#include "hal/hal_mqtt.h"
#include "hal/hal_net.h"
#include "hal/hal_tcp.h"
#include "hal/hal_udp.h"
#include "hal/hal_wifi.h"
#include "hal/hal_wireguard.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_wifi_reset();
  hal_mock_net_reset();
  hal_mock_tcp_reset();
  hal_mock_udp_reset();
  hal_mock_mqtt_reset();
  hal_mock_wireguard_reset();
}
void tearDown(void) {}

static hal_net_endpoint_t endpoint(uint16_t port) {
  hal_net_endpoint_t value = {};
  value.family = HAL_NET_AF_INET;
  value.addr[0] = 192u;
  value.addr[1] = 0u;
  value.addr[2] = 2u;
  value.addr[3] = 1u;
  value.port = port;
  return value;
}

void test_wifi_and_resolver_statuses(void) {
  int result = -1;
  int scan_count = -1;
  uint8_t ip[4] = {};
  hal_wifi_scan_result_t scan = {};
  char text[4] = {};

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_wifi_set_mode_ex((hal_wifi_mode_t)9));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_wifi_begin_station_ex(nullptr, "pass", false));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_wifi_begin_station_ex("ssid", "pass", false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_wifi_set_timeout_ms_ex(321u));
  TEST_ASSERT_EQUAL_UINT32(321u, hal_mock_wifi_get_timeout_ms());
  hal_mock_wifi_set_local_ip("192.0.2.123");
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        hal_wifi_get_local_ip_ex(text, sizeof(text)));
  hal_mock_wifi_set_ping_result(17);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_wifi_ping_status_ex("192.0.2.1", 100u, &result));
  TEST_ASSERT_EQUAL_INT(17, result);
  hal_mock_wifi_set_ping_result(-1);
  TEST_ASSERT_EQUAL_INT(HAL_EIO,
                        hal_wifi_ping_status_ex("192.0.2.1", 100u, &result));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_wifi_scan_networks_ex(&scan_count));
  TEST_ASSERT_EQUAL_INT(0, scan_count);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_wifi_get_scan_result_ex(0u, &scan));
  TEST_ASSERT_TRUE(hal_mock_net_set_dns_entry("host.test", "192.0.2.9"));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_net_resolve_ipv4_ex("host.test", ip));
  TEST_ASSERT_EQUAL_UINT8(9u, ip[3]);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        hal_net_resolve_ipv4_ex("missing.test", ip));
}

void test_tcp_and_udp_statuses(void) {
  const uint8_t data[] = {1u, 2u, 3u};
  size_t count = 0u;
  hal_net_endpoint_t remote = endpoint(1234u);
  hal_tcp_socket_t tcp = nullptr;
  hal_udp_socket_t udp = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_socket_open_ex(&tcp));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_open_ex(&udp));

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_tcp_socket_connect_ex(nullptr, &remote, 10u));
  TEST_ASSERT_EQUAL_INT(
      HAL_ESTATE, hal_tcp_socket_send_ex(tcp, data, sizeof(data), &count));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_socket_connect_ex(tcp, &remote, 10u));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_tcp_socket_send_ex(tcp, data, sizeof(data), &count));
  TEST_ASSERT_EQUAL_size_t(sizeof(data), count);

  hal_net_endpoint_t local = endpoint(4321u);
  TEST_ASSERT_EQUAL_INT(
      HAL_ESTATE,
      hal_udp_socket_sendto_ex(udp, data, sizeof(data), &remote, &count));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_bind_ex(udp, &local));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_sendto_ex(
                                    udp, data, sizeof(data), &remote, &count));
  TEST_ASSERT_EQUAL_size_t(sizeof(data), count);
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_udp_socket_sendto_ex(udp, nullptr, 1u, &remote, &count));
  hal_tcp_socket_close(tcp);
  hal_udp_socket_close(udp);
}

void test_network_pool_and_single_udp_statuses(void) {
  hal_tcp_socket_t tcp[HAL_TCP_SOCKET_MAX_INSTANCES] = {};
  hal_tcp_listener_t listeners[HAL_TCP_LISTENER_MAX_INSTANCES] = {};
  hal_udp_socket_t udp[HAL_UDP_SOCKET_MAX_INSTANCES] = {};
  hal_tcp_socket_t extra_tcp = (hal_tcp_socket_t)1;
  hal_tcp_listener_t extra_listener = (hal_tcp_listener_t)1;
  hal_udp_socket_t extra_udp = (hal_udp_socket_t)1;

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_tcp_socket_open_ex(nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_tcp_listener_open_ex(nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_udp_socket_open_ex(nullptr));
  for (size_t i = 0u; i < HAL_TCP_SOCKET_MAX_INSTANCES; ++i) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_socket_open_ex(&tcp[i]));
  }
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_tcp_socket_open_ex(&extra_tcp));
  TEST_ASSERT_NULL(extra_tcp);
  for (size_t i = 0u; i < HAL_TCP_LISTENER_MAX_INSTANCES; ++i) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_listener_open_ex(&listeners[i]));
  }
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_tcp_listener_open_ex(&extra_listener));
  TEST_ASSERT_NULL(extra_listener);
  for (size_t i = 0u; i < HAL_UDP_SOCKET_MAX_INSTANCES; ++i) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_open_ex(&udp[i]));
  }
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_udp_socket_open_ex(&extra_udp));
  TEST_ASSERT_NULL(extra_udp);

  hal_mock_tcp_reset();
  hal_mock_udp_reset();
  int packet_size = -1;
  uint16_t port = 99u;
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_udp_parse_packet_ex(&packet_size));
  TEST_ASSERT_EQUAL_INT(0, packet_size);
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_udp_remote_port_ex(&port));
  TEST_ASSERT_EQUAL_UINT16(0u, port);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_begin_ex(1234u));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_udp_remote_port_ex(&port));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_udp_end_packet_ex());
}

void test_mqtt_and_wireguard_statuses(void) {
  const uint8_t local_ip[4] = {10u, 0u, 0u, 2u};
  const uint8_t probe_ip[4] = {1u, 1u, 1u, 1u};
  bool peer_up = false;

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_mqtt_set_server_ex(nullptr, 1883u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mqtt_set_server_ex("broker.test", 1883u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mqtt_connect_ex("client"));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_mqtt_publish_ex("topic", nullptr, 1u, false));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mqtt_publish_str_ex("topic", "value", false));

  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT,
                        hal_wireguard_kick_handshake_ex(probe_ip, 53u, 100u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_wireguard_begin_ex(local_ip, "private", "peer.test",
                                               "public", 51820u));
  hal_mock_wireguard_set_peer_up_result(false);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_wireguard_peer_up_ex(nullptr, 0u, nullptr, &peer_up));
  TEST_ASSERT_FALSE(peer_up);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_wifi_and_resolver_statuses);
  RUN_TEST(test_tcp_and_udp_statuses);
  RUN_TEST(test_network_pool_and_single_udp_statuses);
  RUN_TEST(test_mqtt_and_wireguard_statuses);
  return UNITY_END();
}
