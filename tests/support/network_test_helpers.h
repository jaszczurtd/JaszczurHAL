#ifndef JH_NETWORK_TEST_HELPERS_H
#define JH_NETWORK_TEST_HELPERS_H

#include "hal/impl/.mock/hal_mock.h"
#include "hal/network/hal_net.h"
#include "utils/unity.h"

static inline hal_net_endpoint_t make_endpoint(uint8_t a, uint8_t b, uint8_t c,
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

#if defined(HAL_ENABLE_TCP)
/** Local endpoint of the mock TCP listener bound to @p port. */
static inline hal_net_endpoint_t mock_listener_endpoint(uint16_t port) {
  hal_net_endpoint_t bound = {};
  hal_tcp_listener_t listener = hal_mock_tcp_listener_find_by_port(port);
  TEST_ASSERT_NOT_NULL(listener);
  TEST_ASSERT_TRUE(hal_mock_tcp_listener_get_local_endpoint(listener, &bound));
  return bound;
}
#endif

#endif
