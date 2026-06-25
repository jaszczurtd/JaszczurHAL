#include "hal/hal_config.h"
#include "hal/hal_net.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <stdint.h>

void setUp(void) { hal_mock_net_reset(); }

void tearDown(void) {}

void test_endpoint_shape_and_family_values(void) {
  hal_net_endpoint_t endpoint = {};

  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr[0] = 192u;
  endpoint.addr[1] = 168u;
  endpoint.addr[2] = 1u;
  endpoint.addr[3] = 25u;
  endpoint.port = 4242u;

  TEST_ASSERT_EQUAL_INT(0, HAL_NET_AF_UNSPEC);
  TEST_ASSERT_EQUAL_INT(2, HAL_NET_AF_INET);
  TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)sizeof(endpoint.addr));
  TEST_ASSERT_EQUAL_UINT8(192u, endpoint.addr[0]);
  TEST_ASSERT_EQUAL_UINT8(168u, endpoint.addr[1]);
  TEST_ASSERT_EQUAL_UINT8(1u, endpoint.addr[2]);
  TEST_ASSERT_EQUAL_UINT8(25u, endpoint.addr[3]);
  TEST_ASSERT_EQUAL_UINT16(4242u, endpoint.port);
}

void test_status_values_start_at_ok(void) {
  TEST_ASSERT_EQUAL_INT(0, HAL_NET_OK);
  TEST_ASSERT_TRUE(HAL_NET_ERR_INVALID > HAL_NET_OK);
  TEST_ASSERT_TRUE(HAL_NET_ERR_BACKEND > HAL_NET_ERR_INVALID);
}

void test_network_limits_defaults(void) {
  TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)HAL_UDP_SOCKET_MAX_INSTANCES);
  TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)HAL_TCP_SOCKET_MAX_INSTANCES);
  TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)HAL_TCP_LISTENER_MAX_INSTANCES);
  TEST_ASSERT_EQUAL_UINT32(5u, (uint32_t)HAL_TCP_LISTENER_BACKLOG_MAX);
  TEST_ASSERT_EQUAL_UINT32(8u, (uint32_t)HAL_BSD_SOCKET_MAX_FDS);
  TEST_ASSERT_EQUAL_INT(64, HAL_BSD_SOCKET_FD_BASE);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, (uint32_t)HAL_NET_TIMEOUT_FOREVER);
}

void test_resolve_ipv4_accepts_literal_localhost_and_mock_dns(void) {
  uint8_t addr[HAL_NET_IPV4_ADDR_LEN] = {};

  TEST_ASSERT_TRUE(hal_net_resolve_ipv4("192.0.2.55", addr));
  TEST_ASSERT_EQUAL_UINT8(192u, addr[0]);
  TEST_ASSERT_EQUAL_UINT8(55u, addr[3]);

  TEST_ASSERT_TRUE(hal_net_resolve_ipv4("localhost", addr));
  TEST_ASSERT_EQUAL_UINT8(127u, addr[0]);
  TEST_ASSERT_EQUAL_UINT8(1u, addr[3]);

  TEST_ASSERT_TRUE(hal_mock_net_set_dns_entry("broker.example", "203.0.113.8"));
  TEST_ASSERT_TRUE(hal_net_resolve_ipv4("broker.example", addr));
  TEST_ASSERT_EQUAL_UINT8(203u, addr[0]);
  TEST_ASSERT_EQUAL_UINT8(8u, addr[3]);
}

void test_resolve_ipv4_rejects_unknown_or_invalid_names(void) {
  uint8_t addr[HAL_NET_IPV4_ADDR_LEN] = {};

  TEST_ASSERT_FALSE(hal_net_resolve_ipv4(NULL, addr));
  TEST_ASSERT_FALSE(hal_net_resolve_ipv4("", addr));
  TEST_ASSERT_FALSE(hal_net_resolve_ipv4("192.0.2.999", addr));
  TEST_ASSERT_FALSE(hal_net_resolve_ipv4("missing.example", addr));
  TEST_ASSERT_FALSE(hal_net_resolve_ipv4("192.0.2.1", NULL));
  TEST_ASSERT_FALSE(hal_mock_net_set_dns_entry("", "192.0.2.1"));
  TEST_ASSERT_FALSE(hal_mock_net_set_dns_entry("bad.example", "999.0.0.1"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_endpoint_shape_and_family_values);
  RUN_TEST(test_status_values_start_at_ok);
  RUN_TEST(test_network_limits_defaults);
  RUN_TEST(test_resolve_ipv4_accepts_literal_localhost_and_mock_dns);
  RUN_TEST(test_resolve_ipv4_rejects_unknown_or_invalid_names);
  return UNITY_END();
}
