#include "network_contract_control.h"

#include "hal/hal_tcp.h"
#include "hal/hal_udp.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/network/jh_net_address_utils.h"

#include <stdio.h>

extern "C" void jh_contract_backend_reset(void) {
  hal_mock_wifi_reset();
  hal_mock_net_reset();
  hal_mock_tcp_reset();
  hal_mock_udp_reset();
  hal_mock_wifi_set_connected(true);
  hal_mock_wifi_set_status(3);
  hal_mock_wifi_set_local_ip("192.0.2.20");
  hal_mock_wifi_set_dns_ip("192.0.2.53");
  hal_mock_wifi_set_mac("02:00:00:00:00:20");
  hal_mock_wifi_set_rssi(-55);
  hal_mock_wifi_set_ping_result(4);
  (void)hal_mock_net_set_capabilities(HAL_NET_CAP_IPV4 | HAL_NET_CAP_IPV6 |
                                      HAL_NET_CAP_DUAL_STACK);
  (void)hal_mock_net_set_dns_entry("contract.test", "203.0.113.20");
}

extern "C" bool jh_contract_backend_is_socket_offload(void) { return false; }

extern "C" void jh_contract_backend_tcp_inject(void *socket,
                                               const uint8_t *payload,
                                               size_t length) {
  const uint16_t bounded = length > UINT16_MAX ? UINT16_MAX : (uint16_t)length;
  hal_mock_tcp_inject_rx(reinterpret_cast<hal_tcp_socket_t>(socket), payload,
                         bounded);
}

extern "C" void jh_contract_backend_udp_inject(void *socket,
                                               const hal_net_endpoint_t *remote,
                                               const uint8_t *payload,
                                               size_t length) {
  if (remote == nullptr || remote->family != HAL_NET_AF_INET ||
      remote->addr_len != HAL_NET_IPV4_ADDR_LEN) {
    return;
  }
  char address[16] = {};
  const int written =
      snprintf(address, sizeof(address), "%u.%u.%u.%u",
               (unsigned)remote->addr[0], (unsigned)remote->addr[1],
               (unsigned)remote->addr[2], (unsigned)remote->addr[3]);
  if (written <= 0 || (size_t)written >= sizeof(address)) {
    return;
  }
  const uint16_t bounded = length > UINT16_MAX ? UINT16_MAX : (uint16_t)length;
  hal_mock_udp_inject_packet_to(reinterpret_cast<hal_udp_socket_t>(socket),
                                address, remote->port, payload, bounded);
}
