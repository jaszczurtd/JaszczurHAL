#include "fakes/network_contract_control.h"
#include "hal/network/jh_network_backend.h"
#include "hal/network/jh_network_handle_pool.h"
#include "utils/unity.h"

#include <string.h>

namespace {

const jh_network_backend_descriptor_t *backend(void) {
  return jh_network_backend_selected();
}

hal_net_endpoint_t ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                        uint16_t port) {
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

hal_net_endpoint_t ipv6_loopback(uint16_t port) {
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET6;
  endpoint.addr_len = HAL_NET_IPV6_ADDR_LEN;
  endpoint.addr[HAL_NET_IPV6_ADDR_LEN - 1u] = 1u;
  endpoint.port = port;
  return endpoint;
}

void assert_ipv4(const hal_net_endpoint_t &endpoint, uint8_t a, uint8_t b,
                 uint8_t c, uint8_t d) {
  TEST_ASSERT_EQUAL_INT(HAL_NET_AF_INET, endpoint.family);
  TEST_ASSERT_EQUAL_UINT(HAL_NET_IPV4_ADDR_LEN, endpoint.addr_len);
  TEST_ASSERT_EQUAL_UINT8(a, endpoint.addr[0]);
  TEST_ASSERT_EQUAL_UINT8(b, endpoint.addr[1]);
  TEST_ASSERT_EQUAL_UINT8(c, endpoint.addr[2]);
  TEST_ASSERT_EQUAL_UINT8(d, endpoint.addr[3]);
}

} // namespace

void setUp(void) { jh_contract_backend_reset(); }
void tearDown(void) {}

static void test_descriptor_reports_topology_and_optional_capabilities(void) {
  const jh_network_backend_descriptor_t *selected = backend();
  TEST_ASSERT_NOT_NULL(selected);
  const jh_network_capabilities_t required =
      JH_NET_CAP_WIFI_STA | JH_NET_CAP_DNS | JH_NET_CAP_TCP_CLIENT |
      JH_NET_CAP_TCP_LISTENER | JH_NET_CAP_UDP | JH_NET_CAP_IPV4;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_network_backend_validate(selected, required));
  TEST_ASSERT_EQUAL_INT(
      HAL_EUNSUPPORTED,
      jh_network_backend_validate(selected, JH_NET_CAP_TLS_OFFLOAD));

  if (jh_contract_backend_is_socket_offload()) {
    TEST_ASSERT_EQUAL_STRING("mock-socket-offload", selected->name);
    TEST_ASSERT_EQUAL_INT(JH_NETWORK_EXECUTION_OWNED_WORKER,
                          selected->execution_model);
    TEST_ASSERT_BITS_LOW(JH_NET_CAP_HOST_STACK_L3 |
                             JH_NET_CAP_VIRTUAL_NETIF_ROUTE |
                             JH_NET_CAP_STACK_CONTEXT,
                         selected->capabilities);
    TEST_ASSERT_EQUAL_INT(
        HAL_EUNSUPPORTED,
        jh_network_backend_validate(selected, JH_NET_CAP_HOST_STACK_L3));
  } else {
    TEST_ASSERT_EQUAL_STRING("mock-host-stack", selected->name);
    TEST_ASSERT_EQUAL_INT(JH_NETWORK_EXECUTION_POLL, selected->execution_model);
    TEST_ASSERT_BITS_HIGH(JH_NET_CAP_HOST_STACK_L3 |
                              JH_NET_CAP_VIRTUAL_NETIF_ROUTE |
                              JH_NET_CAP_STACK_CONTEXT,
                          selected->capabilities);
    TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_backend_validate(
                                      selected, JH_NET_CAP_HOST_STACK_L3 |
                                                    JH_NET_CAP_STACK_CONTEXT));
  }
}

static void test_service_and_wifi_contract(void) {
  const jh_network_backend_descriptor_t *selected = backend();
  TEST_ASSERT_NOT_NULL(selected->service);
  TEST_ASSERT_NOT_NULL(selected->wifi);
  TEST_ASSERT_EQUAL_INT(HAL_OK, selected->service->initialize());
  TEST_ASSERT_EQUAL_INT(HAL_OK, selected->service->service());
  TEST_ASSERT_EQUAL_INT(HAL_OK, selected->wifi->set_mode(HAL_WIFI_MODE_STA));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      selected->wifi->join("contract-ssid", "contract-password", false, 1000u));

  hal_wifi_state_t state = HAL_WIFI_STATE_OFF;
  TEST_ASSERT_EQUAL_INT(HAL_OK, selected->wifi->get_state(&state));
  TEST_ASSERT_EQUAL_INT(HAL_WIFI_STATE_CONNECTED, state);

  hal_net_endpoint_t local = {};
  hal_net_endpoint_t dns = {};
  uint8_t mac[HAL_WIFI_BSSID_LEN] = {};
  int32_t rssi = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, selected->wifi->get_local_address(&local));
  TEST_ASSERT_EQUAL_INT(HAL_OK, selected->wifi->get_dns_address(&dns));
  TEST_ASSERT_EQUAL_INT(HAL_OK, selected->wifi->get_mac(mac));
  TEST_ASSERT_EQUAL_INT(HAL_OK, selected->wifi->get_rssi(&rssi));
  assert_ipv4(local, 192u, 0u, 2u,
              jh_contract_backend_is_socket_offload() ? 30u : 20u);
  assert_ipv4(dns, 192u, 0u, 2u, 53u);
  TEST_ASSERT_NOT_EQUAL(0, mac[HAL_WIFI_BSSID_LEN - 1u]);
  TEST_ASSERT_TRUE(rssi < 0);

  int scan_count = -1;
  if (jh_contract_backend_is_socket_offload()) {
    TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                          selected->wifi->scan(100u, &scan_count));
  } else {
    TEST_ASSERT_EQUAL_INT(HAL_OK, selected->wifi->scan(100u, &scan_count));
    TEST_ASSERT_TRUE(scan_count >= 0);
  }
  TEST_ASSERT_EQUAL_INT(HAL_OK, selected->service->deinitialize());
}

static void test_dns_contract_and_unsupported_address_family(void) {
  const jh_network_backend_descriptor_t *selected = backend();
  TEST_ASSERT_NOT_NULL(selected->resolver);
  hal_net_endpoint_t results[2] = {};
  size_t count = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, selected->resolver->resolve("contract.test", HAL_NET_AF_UNSPEC,
                                          results, 2u, &count));
  TEST_ASSERT_EQUAL_UINT(1u, count);
  assert_ipv4(results[0], 203u, 0u, 113u,
              jh_contract_backend_is_socket_offload() ? 30u : 20u);

  count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, selected->resolver->resolve(
                                           "contract.test", HAL_NET_AF_UNSPEC,
                                           nullptr, 0u, &count));
  TEST_ASSERT_EQUAL_UINT(1u, count);

  count = 0u;
  const hal_status_t ipv6_status =
      selected->resolver->resolve("::1", HAL_NET_AF_INET6, results, 2u, &count);
  if (jh_contract_backend_is_socket_offload()) {
    TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, ipv6_status);
    TEST_ASSERT_EQUAL_UINT(0u, count);
  } else {
    TEST_ASSERT_EQUAL_INT(HAL_OK, ipv6_status);
    TEST_ASSERT_EQUAL_UINT(1u, count);
    TEST_ASSERT_EQUAL_INT(HAL_NET_AF_INET6, results[0].family);
    TEST_ASSERT_EQUAL_UINT(HAL_NET_IPV6_ADDR_LEN, results[0].addr_len);
  }
}

static void test_tcp_partial_io_listener_and_stale_handles(void) {
  const jh_network_backend_descriptor_t *selected = backend();
  TEST_ASSERT_NOT_NULL(selected->tcp);
  const jh_network_tcp_ops_t *tcp = selected->tcp;
  const hal_net_endpoint_t remote = ipv4(203u, 0u, 113u, 5u, 80u);

  void *backend_socket = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, tcp->socket_open(&backend_socket));
  TEST_ASSERT_NOT_NULL(backend_socket);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        tcp->socket_connect(backend_socket, &remote, 1000u));
  TEST_ASSERT_TRUE(tcp->socket_can_send(backend_socket));
  TEST_ASSERT_TRUE(tcp->socket_is_connected(backend_socket));

  uint8_t payload[600] = {};
  memset(payload, 0x5au, sizeof(payload));
  size_t sent = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, tcp->socket_send(backend_socket, payload,
                                                 sizeof(payload), &sent));
  TEST_ASSERT_GREATER_THAN_UINT(0u, sent);
  TEST_ASSERT_LESS_THAN_UINT(sizeof(payload), sent);

  const uint8_t inbound[] = {1u, 2u, 3u, 4u, 5u};
  jh_contract_backend_tcp_inject(backend_socket, inbound, sizeof(inbound));
  TEST_ASSERT_TRUE(tcp->socket_can_recv(backend_socket));
  uint8_t received[2] = {};
  size_t received_length = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, tcp->socket_recv(backend_socket, received,
                                                 sizeof(received), 100u,
                                                 &received_length));
  TEST_ASSERT_EQUAL_UINT(sizeof(received), received_length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(inbound, received, sizeof(received));

  jh_network_handle_slot_t slots[1] = {};
  jh_network_handle_pool_t pool = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_network_handle_pool_init(&pool, slots, 1u, 7u));
  void *first_ticket = nullptr;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_network_handle_allocate(&pool, backend_socket, &first_ticket));
  void *released_socket = nullptr;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_network_handle_release(&pool, first_ticket, &released_socket));
  TEST_ASSERT_EQUAL_PTR(backend_socket, released_socket);
  tcp->socket_close(released_socket);

  void *reused_socket = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, tcp->socket_open(&reused_socket));
  void *second_ticket = nullptr;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_network_handle_allocate(&pool, reused_socket, &second_ticket));
  TEST_ASSERT_NOT_EQUAL(first_ticket, second_ticket);
  void *resolved = nullptr;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      jh_network_handle_resolve(&pool, first_ticket, &resolved, nullptr));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_network_handle_release(&pool, second_ticket, &resolved));
  tcp->socket_close(resolved);

  void *listener = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, tcp->listener_open(&listener));
  const hal_net_endpoint_t local = ipv4(0u, 0u, 0u, 0u, 8080u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, tcp->listener_bind(listener, &local));
  TEST_ASSERT_EQUAL_INT(HAL_OK, tcp->listener_listen(listener, 2u));
  void *accepted = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        tcp->listener_accept(listener, nullptr, 0u, &accepted));
  TEST_ASSERT_NULL(accepted);
  tcp->listener_close(listener);
}

static void test_udp_partial_io_and_endpoint_preservation(void) {
  const jh_network_backend_descriptor_t *selected = backend();
  TEST_ASSERT_NOT_NULL(selected->udp);
  const jh_network_udp_ops_t *udp = selected->udp;
  const hal_net_endpoint_t local = ipv4(0u, 0u, 0u, 0u, 9000u);
  const hal_net_endpoint_t remote = ipv4(198u, 51u, 100u, 7u, 9001u);

  void *socket = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, udp->socket_open(&socket));
  TEST_ASSERT_EQUAL_INT(HAL_OK, udp->socket_bind(socket, &local));
  TEST_ASSERT_TRUE(udp->socket_can_send(socket));

  uint8_t payload[600] = {};
  memset(payload, 0xa5, sizeof(payload));
  size_t sent = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      udp->socket_sendto(socket, payload, sizeof(payload), &remote, &sent));
  TEST_ASSERT_GREATER_THAN_UINT(0u, sent);
  TEST_ASSERT_LESS_THAN_UINT(sizeof(payload), sent);

  const uint8_t inbound[] = {9u, 8u, 7u, 6u, 5u};
  jh_contract_backend_udp_inject(socket, &remote, inbound, sizeof(inbound));
  TEST_ASSERT_TRUE(udp->socket_can_recv(socket));
  uint8_t received[2] = {};
  hal_net_endpoint_t actual_remote = {};
  size_t received_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, udp->socket_recvfrom(socket, received, sizeof(received),
                                   &actual_remote, 100u, &received_length));
  TEST_ASSERT_EQUAL_UINT(sizeof(received), received_length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(inbound, received, sizeof(received));
  TEST_ASSERT_EQUAL_MEMORY(&remote, &actual_remote, sizeof(remote));
  udp->socket_close(socket);
}

static void test_socket_offload_rejects_ipv6_without_fallback(void) {
  if (!jh_contract_backend_is_socket_offload()) {
    TEST_IGNORE_MESSAGE("host-stack backend supports IPv6");
  }
  void *socket = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, backend()->tcp->socket_open(&socket));
  const hal_net_endpoint_t remote = ipv6_loopback(80u);
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        backend()->tcp->socket_connect(socket, &remote, 100u));
  backend()->tcp->socket_close(socket);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_descriptor_reports_topology_and_optional_capabilities);
  RUN_TEST(test_service_and_wifi_contract);
  RUN_TEST(test_dns_contract_and_unsupported_address_family);
  RUN_TEST(test_tcp_partial_io_listener_and_stale_handles);
  RUN_TEST(test_udp_partial_io_and_endpoint_preservation);
  RUN_TEST(test_socket_offload_rejects_ipv6_without_fallback);
  return UNITY_END();
}
