#include "hal/network/jh_lwip_extension.h"
#include "hal/network/wireguard/core/jh_wireguard_client.h"
#include "hal/network/wireguard/core/wireguard-platform.h"
#include "hal/network/wireguard/core/wireguard_allowed_ip.h"
#include "hal/network/wireguard/core/wireguardif.h"
#include "utils/unity.h"

#include <cstring>

namespace {

struct ExtensionState {
  hal_status_t enter_status;
  hal_status_t resolve_status;
  hal_status_t probe_status;
  hal_status_t random_status;
  hal_status_t tai64n_status;
  void *underlay;
  uint8_t resolved[JH_LWIP_EXTENSION_IPV4_SIZE];
  uint32_t now_ms;
  unsigned enter_count;
  unsigned leave_count;
  unsigned resolve_count;
  unsigned probe_count;
  unsigned random_count;
  bool last_require_ipv4;
  char last_host[64];
  uint8_t last_probe[JH_LWIP_EXTENSION_IPV4_SIZE];
  uint16_t last_probe_port;
};

ExtensionState extension_state;
netif underlay_netif;
int wireguard_device_token;
netif *captured_bind_netif;
err_t netif_init_status;
err_t add_peer_status;
err_t connect_status;
err_t peer_up_status;
err_t poll_status;
ip_addr_t peer_endpoint;
uint16_t peer_endpoint_port;
unsigned netif_add_count;
unsigned netif_remove_count;
unsigned netif_up_count;
unsigned peer_add_count;
unsigned peer_remove_count;
unsigned connect_count;
unsigned poll_count;
unsigned shutdown_count;
unsigned platform_init_count;

hal_status_t fake_stack_enter(void *context, bool require_ipv4) {
  ExtensionState *state = static_cast<ExtensionState *>(context);
  ++state->enter_count;
  state->last_require_ipv4 = require_ipv4;
  return state->enter_status;
}

void fake_stack_leave(void *context) {
  ExtensionState *state = static_cast<ExtensionState *>(context);
  ++state->leave_count;
}

hal_status_t fake_underlay_netif(void *context, void **out_netif) {
  ExtensionState *state = static_cast<ExtensionState *>(context);
  *out_netif = state->underlay;
  return HAL_OK;
}

hal_status_t
fake_resolve_ipv4(void *context, const char *host_or_ip,
                  uint8_t out_address[JH_LWIP_EXTENSION_IPV4_SIZE]) {
  ExtensionState *state = static_cast<ExtensionState *>(context);
  ++state->resolve_count;
  std::strncpy(state->last_host, host_or_ip, sizeof(state->last_host) - 1u);
  if (state->resolve_status == HAL_OK) {
    std::memcpy(out_address, state->resolved, sizeof(state->resolved));
  }
  return state->resolve_status;
}

hal_status_t fake_monotonic_ms(void *context, uint32_t *out_millis) {
  *out_millis = static_cast<ExtensionState *>(context)->now_ms;
  return HAL_OK;
}

hal_status_t fake_random_bytes(void *context, void *buffer, size_t size) {
  ExtensionState *state = static_cast<ExtensionState *>(context);
  ++state->random_count;
  if (state->random_status == HAL_OK && size > 0u) {
    std::memset(buffer, 0xa5, size);
  }
  return state->random_status;
}

hal_status_t
fake_tai64n_now(void *context,
                uint8_t out_tai64n[JH_LWIP_EXTENSION_TAI64N_SIZE]) {
  ExtensionState *state = static_cast<ExtensionState *>(context);
  if (state->tai64n_status == HAL_OK) {
    for (size_t index = 0u; index < JH_LWIP_EXTENSION_TAI64N_SIZE; ++index) {
      out_tai64n[index] = static_cast<uint8_t>(index);
    }
  }
  return state->tai64n_status;
}

hal_status_t
fake_send_udp_probe(void *context,
                    const uint8_t address[JH_LWIP_EXTENSION_IPV4_SIZE],
                    uint16_t port) {
  ExtensionState *state = static_cast<ExtensionState *>(context);
  ++state->probe_count;
  std::memcpy(state->last_probe, address, sizeof(state->last_probe));
  state->last_probe_port = port;
  return state->probe_status;
}

jh_lwip_extension_port_t extension_port = {
    &extension_state,    fake_stack_enter,  fake_stack_leave,
    fake_underlay_netif, fake_resolve_ipv4, fake_monotonic_ms,
    fake_random_bytes,   fake_tai64n_now,   fake_send_udp_probe,
};

} // namespace

extern "C" {

void hal_deb(const char *, ...) {}
void hal_derr(const char *, ...) {}

netif *netif_default = nullptr;

netif *netif_add(netif *network_interface, const ip4_addr_t *address,
                 const ip4_addr_t *netmask, const ip4_addr_t *gateway,
                 void *state, netif_init_fn initialize, netif_input_fn) {
  ++netif_add_count;
  network_interface->state = state;
  network_interface->address = *address;
  network_interface->netmask = *netmask;
  network_interface->gateway = *gateway;
  if (initialize(network_interface) != ERR_OK) {
    network_interface->state = nullptr;
    return nullptr;
  }
  network_interface->added = true;
  return network_interface;
}

void netif_remove(netif *network_interface) {
  ++netif_remove_count;
  network_interface->added = false;
  if (netif_default == network_interface) {
    netif_default = nullptr;
  }
}

void netif_set_up(netif *) { ++netif_up_count; }

void netif_set_default(netif *network_interface) {
  netif_default = network_interface;
}

err_t ip_input(struct pbuf *, netif *) { return ERR_OK; }

err_t wireguardif_init(netif *network_interface) {
  wireguardif_init_data *config =
      static_cast<wireguardif_init_data *>(network_interface->state);
  captured_bind_netif = config->bind_netif;
  if (netif_init_status == ERR_OK) {
    network_interface->state = &wireguard_device_token;
  }
  return netif_init_status;
}

void wireguardif_shutdown(netif *network_interface) {
  ++shutdown_count;
  network_interface->state = nullptr;
}

void wireguardif_peer_init(wireguardif_peer *) {}

err_t wireguardif_add_peer(netif *, wireguardif_peer *, u8_t *peer_index) {
  ++peer_add_count;
  if (add_peer_status == ERR_OK && peer_index != nullptr) {
    *peer_index = 3u;
  }
  return add_peer_status;
}

err_t wireguardif_remove_peer(netif *, u8_t) {
  ++peer_remove_count;
  return ERR_OK;
}

err_t wireguardif_update_endpoint(netif *, u8_t, const ip_addr_t *, u16_t) {
  return ERR_OK;
}

err_t wireguardif_connect(netif *, u8_t) {
  ++connect_count;
  return connect_status;
}

err_t wireguardif_disconnect(netif *, u8_t) { return ERR_OK; }

err_t wireguardif_poll(netif *) {
  ++poll_count;
  return poll_status;
}

err_t wireguardif_peer_is_up(netif *, u8_t, ip_addr_t *current_ip,
                             u16_t *current_port) {
  if (peer_up_status == ERR_OK) {
    if (current_ip != nullptr) {
      *current_ip = peer_endpoint;
    }
    if (current_port != nullptr) {
      *current_port = peer_endpoint_port;
    }
  }
  return peer_up_status;
}

const jh_lwip_extension_port_t *jh_lwip_extension_platform_port(void) {
  return &extension_port;
}

void wireguard_platform_init(void) { ++platform_init_count; }
uint32_t wireguard_sys_now(void) { return extension_state.now_ms; }
void wireguard_random_bytes(void *bytes, size_t size) {
  (void)fake_random_bytes(&extension_state, bytes, size);
}
void wireguard_tai64n_now(uint8_t *output) {
  (void)fake_tai64n_now(&extension_state, output);
}
bool wireguard_is_under_load(void) { return false; }

} // extern "C"

void setUp(void) {
  std::memset(&extension_state, 0, sizeof(extension_state));
  std::memset(&underlay_netif, 0, sizeof(underlay_netif));
  extension_state.enter_status = HAL_OK;
  extension_state.resolve_status = HAL_OK;
  extension_state.probe_status = HAL_OK;
  extension_state.random_status = HAL_OK;
  extension_state.tai64n_status = HAL_OK;
  extension_state.underlay = &underlay_netif;
  extension_state.resolved[0] = 192u;
  extension_state.resolved[1] = 0u;
  extension_state.resolved[2] = 2u;
  extension_state.resolved[3] = 10u;
  extension_state.now_ms = 10u;
  captured_bind_netif = nullptr;
  netif_init_status = ERR_OK;
  add_peer_status = ERR_OK;
  connect_status = ERR_OK;
  peer_up_status = ERR_CONN;
  poll_status = ERR_OK;
  peer_endpoint = {};
  peer_endpoint_port = 0u;
  netif_add_count = 0u;
  netif_remove_count = 0u;
  netif_up_count = 0u;
  peer_add_count = 0u;
  peer_remove_count = 0u;
  connect_count = 0u;
  poll_count = 0u;
  shutdown_count = 0u;
  platform_init_count = 0u;
  netif_default = &underlay_netif;
}

void tearDown(void) {}

void test_extension_validates_and_balances_stack_guard(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_lwip_extension_validate(&extension_port));
  {
    JHLwipExtensionGuard guard(&extension_port, true);
    TEST_ASSERT_EQUAL_INT(HAL_OK, guard.status());
    TEST_ASSERT_TRUE(guard.entered());
    TEST_ASSERT_TRUE(extension_state.last_require_ipv4);
    TEST_ASSERT_EQUAL_UINT32(0u, extension_state.leave_count);
  }
  TEST_ASSERT_EQUAL_UINT32(1u, extension_state.enter_count);
  TEST_ASSERT_EQUAL_UINT32(1u, extension_state.leave_count);

  extension_state.enter_status = HAL_EBUSY;
  {
    JHLwipExtensionGuard guard(&extension_port, false);
    TEST_ASSERT_EQUAL_INT(HAL_EBUSY, guard.status());
    TEST_ASSERT_FALSE(guard.entered());
  }
  TEST_ASSERT_EQUAL_UINT32(2u, extension_state.enter_count);
  TEST_ASSERT_EQUAL_UINT32(1u, extension_state.leave_count);

  jh_lwip_extension_port_t incomplete = extension_port;
  incomplete.send_udp_probe = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, jh_lwip_extension_validate(&incomplete));
}

void test_extension_forwards_portable_operations_and_statuses(void) {
  uint8_t address[4] = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_lwip_extension_resolve_ipv4(
                                    &extension_port, "wg.example", address));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(extension_state.resolved, address,
                                sizeof(address));
  TEST_ASSERT_EQUAL_STRING("wg.example", extension_state.last_host);

  void *underlay = nullptr;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lwip_extension_underlay_netif(&extension_port, &underlay));
  TEST_ASSERT_EQUAL_PTR(&underlay_netif, underlay);

  uint32_t milliseconds = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lwip_extension_monotonic_ms(&extension_port, &milliseconds));
  TEST_ASSERT_EQUAL_UINT32(extension_state.now_ms, milliseconds);

  uint8_t random[8] = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_lwip_extension_random_bytes(
                                    &extension_port, random, sizeof(random)));
  for (uint8_t byte : random) {
    TEST_ASSERT_EQUAL_HEX8(0xa5u, byte);
  }

  uint8_t tai64n[JH_LWIP_EXTENSION_TAI64N_SIZE] = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_lwip_extension_tai64n_now(&extension_port, tai64n));
  for (size_t index = 0u; index < sizeof(tai64n); ++index) {
    TEST_ASSERT_EQUAL_UINT8(index, tai64n[index]);
  }

  const uint8_t probe[4] = {1u, 1u, 1u, 1u};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_lwip_extension_send_udp_probe(&extension_port, probe, 53u));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(probe, extension_state.last_probe,
                                sizeof(probe));
  TEST_ASSERT_EQUAL_UINT16(53u, extension_state.last_probe_port);

  extension_state.resolve_status = HAL_ETIMEOUT;
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, jh_lwip_extension_resolve_ipv4(
                                          &extension_port, "timeout", address));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, jh_lwip_extension_send_udp_probe(&extension_port, probe, 0u));
}

void test_inbound_allowed_ips_validates_inner_source_not_destination(void) {
  wireguard_peer peer = {};
  ip_hdr packet = {};

  peer.allowed_source_ips[0].valid = true;
  IP_ADDR4(&peer.allowed_source_ips[0].ip, 10u, 77u, 0u, 2u);
  IP_ADDR4(&peer.allowed_source_ips[0].mask, 255u, 255u, 255u, 255u);

  IP4_ADDR(&packet.src, 10u, 77u, 0u, 2u);
  IP4_ADDR(&packet.dest, 10u, 77u, 0u, 1u);
  TEST_ASSERT_TRUE(wireguard_ipv4_source_is_allowed(&peer, &packet));

  IP4_ADDR(&packet.src, 192u, 0u, 2u, 123u);
  IP4_ADDR(&packet.dest, 10u, 77u, 0u, 2u);
  TEST_ASSERT_FALSE(wireguard_ipv4_source_is_allowed(&peer, &packet));
}

void test_full_tunnel_lifecycle_restores_route_and_resources(void) {
  JHWireGuardClient wireguard;
  const uint8_t local_ip[4] = {10u, 8u, 0u, 2u};
  TEST_ASSERT_TRUE(
      wireguard.begin(local_ip, "private", "wg.example", "public", 51820u));
  TEST_ASSERT_TRUE(wireguard.is_initialized());
  TEST_ASSERT_EQUAL_PTR(&underlay_netif, captured_bind_netif);
  TEST_ASSERT_TRUE(netif_default != &underlay_netif);
  TEST_ASSERT_EQUAL_UINT32(1u, netif_add_count);
  TEST_ASSERT_EQUAL_UINT32(1u, netif_up_count);
  TEST_ASSERT_EQUAL_UINT32(1u, peer_add_count);
  TEST_ASSERT_EQUAL_UINT32(1u, connect_count);
  TEST_ASSERT_EQUAL_UINT32(1u, extension_state.enter_count);
  TEST_ASSERT_EQUAL_UINT32(1u, extension_state.leave_count);

  wireguard.end();
  TEST_ASSERT_FALSE(wireguard.is_initialized());
  TEST_ASSERT_EQUAL_PTR(&underlay_netif, netif_default);
  TEST_ASSERT_EQUAL_UINT32(1u, peer_remove_count);
  TEST_ASSERT_EQUAL_UINT32(1u, netif_remove_count);
  TEST_ASSERT_EQUAL_UINT32(1u, shutdown_count);
  TEST_ASSERT_EQUAL_UINT32(2u, extension_state.enter_count);
  TEST_ASSERT_EQUAL_UINT32(2u, extension_state.leave_count);
}

void test_split_tunnel_keeps_default_route(void) {
  JHWireGuardClient wireguard;
  const uint8_t local_ip[4] = {10u, 8u, 0u, 2u};
  const uint8_t allowed_ip[4] = {10u, 20u, 0u, 0u};
  const uint8_t allowed_mask[4] = {255u, 255u, 0u, 0u};
  TEST_ASSERT_TRUE(wireguard.begin_advanced(local_ip, "private", "wg.example",
                                            "public", 51820u, allowed_ip,
                                            allowed_mask));
  TEST_ASSERT_EQUAL_PTR(&underlay_netif, netif_default);
  wireguard.end();
  TEST_ASSERT_EQUAL_PTR(&underlay_netif, netif_default);
  TEST_ASSERT_EQUAL_UINT32(1u, shutdown_count);
}

void test_peer_and_connect_failures_cleanup_then_reconnect(void) {
  JHWireGuardClient wireguard;
  const uint8_t local_ip[4] = {10u, 8u, 0u, 2u};
  add_peer_status = ERR_MEM;
  TEST_ASSERT_FALSE(
      wireguard.begin(local_ip, "private", "wg.example", "public", 51820u));
  TEST_ASSERT_FALSE(wireguard.is_initialized());
  TEST_ASSERT_EQUAL_UINT32(1u, netif_remove_count);
  TEST_ASSERT_EQUAL_UINT32(1u, shutdown_count);
  TEST_ASSERT_EQUAL_PTR(&underlay_netif, netif_default);
  TEST_ASSERT_EQUAL_UINT32(extension_state.enter_count,
                           extension_state.leave_count);

  add_peer_status = ERR_OK;
  connect_status = ERR_RTE;
  TEST_ASSERT_FALSE(
      wireguard.begin(local_ip, "private", "wg.example", "public", 51820u));
  TEST_ASSERT_EQUAL_UINT32(1u, peer_remove_count);
  TEST_ASSERT_EQUAL_UINT32(2u, netif_remove_count);
  TEST_ASSERT_EQUAL_UINT32(2u, shutdown_count);

  connect_status = ERR_OK;
  TEST_ASSERT_TRUE(
      wireguard.begin(local_ip, "private", "wg.example", "public", 51820u));
  wireguard.end();
  TEST_ASSERT_EQUAL_UINT32(3u, shutdown_count);
  TEST_ASSERT_EQUAL_UINT32(extension_state.enter_count,
                           extension_state.leave_count);
}

void test_repeated_begin_end_does_not_leak_route_or_backend_state(void) {
  JHWireGuardClient wireguard;
  const uint8_t local_ip[4] = {10u, 8u, 0u, 2u};
  const uint8_t allowed_ip[4] = {10u, 20u, 0u, 0u};
  const uint8_t allowed_mask[4] = {255u, 255u, 0u, 0u};

  for (unsigned cycle = 0u; cycle < 5u; ++cycle) {
    TEST_ASSERT_TRUE(wireguard.begin_advanced(local_ip, "private", "wg.example",
                                              "public", 51820u, allowed_ip,
                                              allowed_mask));
    TEST_ASSERT_TRUE(wireguard.is_initialized());
    TEST_ASSERT_EQUAL_PTR(&underlay_netif, netif_default);
    wireguard.end();
    TEST_ASSERT_FALSE(wireguard.is_initialized());
    TEST_ASSERT_EQUAL_PTR(&underlay_netif, netif_default);
  }

  TEST_ASSERT_EQUAL_UINT32(5u, netif_add_count);
  TEST_ASSERT_EQUAL_UINT32(5u, netif_remove_count);
  TEST_ASSERT_EQUAL_UINT32(5u, peer_add_count);
  TEST_ASSERT_EQUAL_UINT32(5u, peer_remove_count);
  TEST_ASSERT_EQUAL_UINT32(5u, shutdown_count);
  TEST_ASSERT_EQUAL_UINT32(extension_state.enter_count,
                           extension_state.leave_count);
}

void test_stack_and_resolver_failures_do_not_mutate_lwip(void) {
  JHWireGuardClient wireguard;
  const uint8_t local_ip[4] = {10u, 8u, 0u, 2u};
  extension_state.random_status = HAL_EHW;
  TEST_ASSERT_FALSE(
      wireguard.begin(local_ip, "private", "wg.example", "public", 51820u));
  TEST_ASSERT_EQUAL_UINT32(0u, extension_state.resolve_count);
  TEST_ASSERT_EQUAL_UINT32(0u, extension_state.enter_count);
  TEST_ASSERT_EQUAL_UINT32(0u, netif_add_count);

  extension_state.random_status = HAL_OK;
  extension_state.tai64n_status = HAL_ESTATE;
  TEST_ASSERT_FALSE(
      wireguard.begin(local_ip, "private", "wg.example", "public", 51820u));
  TEST_ASSERT_EQUAL_UINT32(0u, extension_state.resolve_count);
  TEST_ASSERT_EQUAL_UINT32(0u, extension_state.enter_count);
  TEST_ASSERT_EQUAL_UINT32(0u, netif_add_count);

  extension_state.tai64n_status = HAL_OK;
  extension_state.resolve_status = HAL_ETIMEOUT;
  TEST_ASSERT_FALSE(
      wireguard.begin(local_ip, "private", "wg.example", "public", 51820u));
  TEST_ASSERT_EQUAL_UINT32(0u, extension_state.enter_count);
  TEST_ASSERT_EQUAL_UINT32(0u, netif_add_count);

  extension_state.resolve_status = HAL_OK;
  extension_state.enter_status = HAL_EBUSY;
  TEST_ASSERT_FALSE(
      wireguard.begin(local_ip, "private", "wg.example", "public", 51820u));
  TEST_ASSERT_EQUAL_UINT32(1u, extension_state.enter_count);
  TEST_ASSERT_EQUAL_UINT32(0u, extension_state.leave_count);
  TEST_ASSERT_EQUAL_UINT32(0u, netif_add_count);
  TEST_ASSERT_EQUAL_PTR(&underlay_netif, netif_default);
}

void test_peer_endpoint_and_probe_retry_rate_limit(void) {
  JHWireGuardClient wireguard;
  const uint8_t local_ip[4] = {10u, 8u, 0u, 2u};
  TEST_ASSERT_TRUE(
      wireguard.begin(local_ip, "private", "wg.example", "public", 51820u));

  peer_up_status = ERR_OK;
  IP_ADDR4(&peer_endpoint, 198u, 51u, 100u, 9u);
  peer_endpoint_port = 41000u;
  uint8_t endpoint[4] = {};
  uint16_t endpoint_port = 0u;
  TEST_ASSERT_TRUE(wireguard.peer_up(endpoint, &endpoint_port));
  TEST_ASSERT_EQUAL_UINT32(1u, poll_count);
  TEST_ASSERT_EQUAL_UINT8(198u, endpoint[0]);
  TEST_ASSERT_EQUAL_UINT8(51u, endpoint[1]);
  TEST_ASSERT_EQUAL_UINT8(100u, endpoint[2]);
  TEST_ASSERT_EQUAL_UINT8(9u, endpoint[3]);
  TEST_ASSERT_EQUAL_UINT16(41000u, endpoint_port);

  poll_status = ERR_IF;
  TEST_ASSERT_FALSE(wireguard.peer_up(nullptr, nullptr));
  TEST_ASSERT_EQUAL_UINT32(2u, poll_count);
  poll_status = ERR_OK;

  const uint8_t probe[4] = {1u, 1u, 1u, 1u};
  TEST_ASSERT_TRUE(wireguard.kick_handshake(probe, 53u, 250u));
  TEST_ASSERT_EQUAL_UINT32(3u, poll_count);
  TEST_ASSERT_EQUAL_UINT32(0u, extension_state.probe_count);
  extension_state.now_ms = 20u;
  TEST_ASSERT_TRUE(wireguard.kick_handshake(probe, 53u, 250u));
  TEST_ASSERT_EQUAL_UINT32(3u, poll_count);

  extension_state.now_ms = 300u;
  poll_status = ERR_IF;
  TEST_ASSERT_FALSE(wireguard.kick_handshake(probe, 53u, 250u));
  TEST_ASSERT_EQUAL_UINT32(4u, poll_count);
  poll_status = ERR_OK;
  TEST_ASSERT_TRUE(wireguard.kick_handshake(probe, 53u, 250u));
  TEST_ASSERT_EQUAL_UINT32(5u, poll_count);

  wireguard.end();
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_extension_validates_and_balances_stack_guard);
  RUN_TEST(test_extension_forwards_portable_operations_and_statuses);
  RUN_TEST(test_inbound_allowed_ips_validates_inner_source_not_destination);
  RUN_TEST(test_full_tunnel_lifecycle_restores_route_and_resources);
  RUN_TEST(test_split_tunnel_keeps_default_route);
  RUN_TEST(test_peer_and_connect_failures_cleanup_then_reconnect);
  RUN_TEST(test_repeated_begin_end_does_not_leak_route_or_backend_state);
  RUN_TEST(test_stack_and_resolver_failures_do_not_mutate_lwip);
  RUN_TEST(test_peer_endpoint_and_probe_retry_rate_limit);
  return UNITY_END();
}
