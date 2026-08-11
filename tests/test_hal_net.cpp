#include "hal/core/hal_config.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/network/hal_net.h"
#include "utils/unity.h"

#include <stdint.h>
#include <string.h>

void setUp(void) { hal_mock_net_reset(); }

void tearDown(void) {}

void test_endpoint_shape_and_family_values(void) {
  hal_net_endpoint_t endpoint = {};

  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr_len = HAL_NET_IPV4_ADDR_LEN;
  endpoint.addr[0] = 192u;
  endpoint.addr[1] = 168u;
  endpoint.addr[2] = 1u;
  endpoint.addr[3] = 25u;
  endpoint.port = 4242u;

  TEST_ASSERT_EQUAL_INT(0, HAL_NET_AF_UNSPEC);
  TEST_ASSERT_EQUAL_INT(2, HAL_NET_AF_INET);
  TEST_ASSERT_EQUAL_INT(10, HAL_NET_AF_INET6);
  TEST_ASSERT_EQUAL_UINT32(16u, (uint32_t)sizeof(endpoint.addr));
  TEST_ASSERT_EQUAL_UINT8(HAL_NET_IPV4_ADDR_LEN, endpoint.addr_len);
  TEST_ASSERT_EQUAL_UINT8(192u, endpoint.addr[0]);
  TEST_ASSERT_EQUAL_UINT8(168u, endpoint.addr[1]);
  TEST_ASSERT_EQUAL_UINT8(1u, endpoint.addr[2]);
  TEST_ASSERT_EQUAL_UINT8(25u, endpoint.addr[3]);
  TEST_ASSERT_EQUAL_UINT16(4242u, endpoint.port);
  TEST_ASSERT_EQUAL_UINT32(0u, endpoint.scope_id);
}

void test_capabilities_are_explicit_and_validated(void) {
  hal_net_capabilities_t capabilities = 0u;

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_net_get_capabilities_ex(&capabilities));
  TEST_ASSERT_EQUAL_UINT32(HAL_NET_CAP_IPV4, capabilities);
  TEST_ASSERT_EQUAL_UINT32(HAL_NET_CAP_IPV4, hal_net_get_capabilities());
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_net_get_capabilities_ex(NULL));

  const hal_net_capabilities_t dual =
      HAL_NET_CAP_IPV4 | HAL_NET_CAP_IPV6 | HAL_NET_CAP_DUAL_STACK;
  TEST_ASSERT_TRUE(hal_mock_net_set_capabilities(dual));
  TEST_ASSERT_EQUAL_UINT32(dual, hal_net_get_capabilities());
  TEST_ASSERT_FALSE(
      hal_mock_net_set_capabilities(HAL_NET_CAP_IPV4 | HAL_NET_CAP_IPV6));
  TEST_ASSERT_FALSE(hal_mock_net_set_capabilities(HAL_NET_CAP_DUAL_STACK));
  TEST_ASSERT_FALSE(hal_mock_net_set_capabilities(1u << 31u));
  TEST_ASSERT_EQUAL_UINT32(dual, hal_net_get_capabilities());
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

  TEST_ASSERT_TRUE(hal_mock_net_set_dns_entry("multi-a.example", "192.0.2.1"));
  TEST_ASSERT_TRUE(hal_mock_net_add_dns_entry("multi-a.example", "192.0.2.2"));
  TEST_ASSERT_TRUE(hal_net_resolve_ipv4("multi-a.example", addr));
  TEST_ASSERT_EQUAL_UINT8(192u, addr[0]);
  TEST_ASSERT_EQUAL_UINT8(1u, addr[3]);
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

void test_family_neutral_resolver_reports_capacity_and_preserves_results(void) {
  const hal_net_capabilities_t dual =
      HAL_NET_CAP_IPV4 | HAL_NET_CAP_IPV6 | HAL_NET_CAP_DUAL_STACK;
  hal_net_endpoint_t results[3];
  hal_net_endpoint_t sentinels[3];
  size_t count = 99u;

  TEST_ASSERT_TRUE(hal_mock_net_set_capabilities(dual));
  TEST_ASSERT_TRUE(hal_mock_net_add_dns_entry("multi.example", "192.0.2.10"));
  TEST_ASSERT_TRUE(hal_mock_net_add_dns_entry("multi.example", "2001:db8::10"));
  TEST_ASSERT_TRUE(hal_mock_net_add_dns_entry("multi.example", "fe80::1234%7"));

  TEST_ASSERT_EQUAL_INT(
      HAL_EOVERFLOW,
      hal_net_resolve_ex("multi.example", HAL_NET_AF_UNSPEC, NULL, 0u, &count));
  TEST_ASSERT_EQUAL_UINT32(3u, count);

  memset(results, 0xa5, sizeof(results));
  memcpy(sentinels, results, sizeof(results));
  count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        hal_net_resolve_ex("multi.example", HAL_NET_AF_UNSPEC,
                                           results, 2u, &count));
  TEST_ASSERT_EQUAL_UINT32(3u, count);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(sentinels, results, sizeof(results));

  memset(results, 0, sizeof(results));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_net_resolve_ex("multi.example", HAL_NET_AF_UNSPEC,
                                           results, 3u, &count));
  TEST_ASSERT_EQUAL_UINT32(3u, count);
  TEST_ASSERT_EQUAL_INT(HAL_NET_AF_INET, results[0].family);
  TEST_ASSERT_EQUAL_UINT8(HAL_NET_IPV4_ADDR_LEN, results[0].addr_len);
  TEST_ASSERT_EQUAL_UINT8(192u, results[0].addr[0]);
  TEST_ASSERT_EQUAL_UINT8(10u, results[0].addr[3]);
  for (size_t i = HAL_NET_IPV4_ADDR_LEN; i < HAL_NET_MAX_ADDR_LEN; ++i) {
    TEST_ASSERT_EQUAL_UINT8(0u, results[0].addr[i]);
  }
  TEST_ASSERT_EQUAL_INT(HAL_NET_AF_INET6, results[1].family);
  TEST_ASSERT_EQUAL_UINT8(HAL_NET_IPV6_ADDR_LEN, results[1].addr_len);
  TEST_ASSERT_EQUAL_UINT8(0x20u, results[1].addr[0]);
  TEST_ASSERT_EQUAL_UINT8(0x01u, results[1].addr[1]);
  TEST_ASSERT_EQUAL_UINT8(0x0du, results[1].addr[2]);
  TEST_ASSERT_EQUAL_UINT8(0xb8u, results[1].addr[3]);
  TEST_ASSERT_EQUAL_UINT8(0x10u, results[1].addr[15]);
  TEST_ASSERT_EQUAL_UINT32(0u, results[1].scope_id);
  TEST_ASSERT_EQUAL_INT(HAL_NET_AF_INET6, results[2].family);
  TEST_ASSERT_EQUAL_UINT8(HAL_NET_IPV6_ADDR_LEN, results[2].addr_len);
  TEST_ASSERT_EQUAL_UINT8(0xfeu, results[2].addr[0]);
  TEST_ASSERT_EQUAL_UINT8(0x80u, results[2].addr[1]);
  TEST_ASSERT_EQUAL_UINT8(0x12u, results[2].addr[14]);
  TEST_ASSERT_EQUAL_UINT8(0x34u, results[2].addr[15]);
  TEST_ASSERT_EQUAL_UINT32(7u, results[2].scope_id);
}

void test_family_neutral_resolver_filters_and_rejects_unsupported_ipv6(void) {
  const hal_net_capabilities_t dual =
      HAL_NET_CAP_IPV4 | HAL_NET_CAP_IPV6 | HAL_NET_CAP_DUAL_STACK;
  hal_net_endpoint_t results[2] = {};
  size_t count = 0u;

  TEST_ASSERT_TRUE(hal_mock_net_set_capabilities(dual));
  TEST_ASSERT_TRUE(hal_mock_net_add_dns_entry("mixed.example", "198.51.100.5"));
  TEST_ASSERT_TRUE(hal_mock_net_add_dns_entry("mixed.example", "2001:db8::5"));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_net_resolve_ex("mixed.example", HAL_NET_AF_INET6,
                                           results, 2u, &count));
  TEST_ASSERT_EQUAL_UINT32(1u, count);
  TEST_ASSERT_EQUAL_INT(HAL_NET_AF_INET6, results[0].family);

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_net_resolve_ex("mixed.example",
                                                       (hal_net_family_t)999,
                                                       results, 2u, &count));
  TEST_ASSERT_EQUAL_UINT32(0u, count);

  TEST_ASSERT_TRUE(hal_mock_net_set_capabilities(HAL_NET_CAP_IPV4));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_net_resolve_ex("mixed.example", HAL_NET_AF_INET6,
                                           results, 2u, &count));
  TEST_ASSERT_EQUAL_UINT32(0u, count);
  TEST_ASSERT_EQUAL_INT(
      HAL_EUNSUPPORTED,
      hal_net_resolve_ex("2001:db8::5", HAL_NET_AF_INET6, results, 2u, &count));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_net_resolve_ex("mixed.example", HAL_NET_AF_UNSPEC,
                                           results, 2u, &count));
  TEST_ASSERT_EQUAL_UINT32(1u, count);
  TEST_ASSERT_EQUAL_INT(HAL_NET_AF_INET, results[0].family);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_endpoint_shape_and_family_values);
  RUN_TEST(test_capabilities_are_explicit_and_validated);
  RUN_TEST(test_status_values_start_at_ok);
  RUN_TEST(test_network_limits_defaults);
  RUN_TEST(test_resolve_ipv4_accepts_literal_localhost_and_mock_dns);
  RUN_TEST(test_resolve_ipv4_rejects_unknown_or_invalid_names);
  RUN_TEST(test_family_neutral_resolver_reports_capacity_and_preserves_results);
  RUN_TEST(test_family_neutral_resolver_filters_and_rejects_unsupported_ipv6);
  return UNITY_END();
}
