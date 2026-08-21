#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_WIREGUARD)

#include "jh_wireguard_client.h"

#include "hal/network/jh_lwip_extension.h"
#include "wireguard-platform.h"
#include "wireguard_port.h"
#include "wireguardif.h"

#include <lwip/ip.h>
#include <lwip/ip4_addr.h>
#include <lwip/ip_addr.h>
#include <lwip/netif.h>

#include <string.h>

namespace {

struct netif s_wg_netif;
struct netif *s_previous_default_netif;
uint8_t s_peer_index = WIREGUARDIF_INVALID_INDEX;
bool s_netif_added;
bool s_default_route_changed;

bool ipv4_is_zero(const uint8_t address[4]) {
  return address[0] == 0u && address[1] == 0u && address[2] == 0u &&
         address[3] == 0u;
}

bool resolve_ipv4(const char *host_or_ip, ip4_addr_t *out) {
  if (host_or_ip == nullptr || out == nullptr) {
    return false;
  }

  uint8_t resolved[JH_LWIP_EXTENSION_IPV4_SIZE] = {};
  const hal_status_t status = jh_lwip_extension_resolve_ipv4(
      jh_lwip_extension_platform_port(), host_or_ip, resolved);
  if (status != HAL_OK) {
    log_e(TAG "Failed to resolve endpoint '%s': %s", host_or_ip,
          hal_status_to_string(status));
    return false;
  }

  IP4_ADDR(out, resolved[0], resolved[1], resolved[2], resolved[3]);
  return true;
}

void teardown_locked() {
  if (s_default_route_changed && s_previous_default_netif != nullptr) {
    netif_set_default(s_previous_default_netif);
  }
  if (s_peer_index != WIREGUARDIF_INVALID_INDEX) {
    (void)wireguardif_remove_peer(&s_wg_netif, s_peer_index);
  }
  if (s_wg_netif.state != nullptr) {
    wireguardif_shutdown(&s_wg_netif);
  }
  if (s_netif_added) {
    netif_remove(&s_wg_netif);
  }

  memset(&s_wg_netif, 0, sizeof(s_wg_netif));
  s_peer_index = WIREGUARDIF_INVALID_INDEX;
  s_netif_added = false;
  s_default_route_changed = false;
  s_previous_default_netif = nullptr;
}

} // namespace

bool JHWireGuardClient::begin(const uint8_t local_ip[4],
                              const char *private_key,
                              const char *remote_peer_address,
                              const char *remote_peer_public_key,
                              uint16_t remote_peer_port) {
  static const uint8_t route_all[4] = {};
  return begin_advanced(local_ip, private_key, remote_peer_address,
                        remote_peer_public_key, remote_peer_port, route_all,
                        route_all);
}

bool JHWireGuardClient::begin_advanced(const uint8_t local_ip[4],
                                       const char *private_key,
                                       const char *remote_peer_address,
                                       const char *remote_peer_public_key,
                                       uint16_t remote_peer_port,
                                       const uint8_t allowed_ip[4],
                                       const uint8_t allowed_mask[4]) {
  if (initialized_) {
    return true;
  }
  if (local_ip == nullptr || private_key == nullptr ||
      remote_peer_address == nullptr || remote_peer_public_key == nullptr ||
      allowed_ip == nullptr || allowed_mask == nullptr) {
    return false;
  }

  const jh_lwip_extension_port_t *extension = jh_lwip_extension_platform_port();
  const hal_status_t extension_status = jh_lwip_extension_validate(extension);
  if (extension_status != HAL_OK) {
    log_e(TAG "Invalid lwIP extension port: %s",
          hal_status_to_string(extension_status));
    return false;
  }

  uint8_t entropy_preflight[sizeof(uint32_t)] = {};
  const hal_status_t entropy_status = jh_lwip_extension_random_bytes(
      extension, entropy_preflight, sizeof(entropy_preflight));
  memset(entropy_preflight, 0, sizeof(entropy_preflight));
  if (entropy_status != HAL_OK) {
    log_e(TAG "WireGuard entropy preflight failed: %s",
          hal_status_to_string(entropy_status));
    return false;
  }

  uint8_t time_preflight[JH_LWIP_EXTENSION_TAI64N_SIZE] = {};
  const hal_status_t time_status =
      jh_lwip_extension_tai64n_now(extension, time_preflight);
  memset(time_preflight, 0, sizeof(time_preflight));
  if (time_status != HAL_OK) {
    log_e(TAG "WireGuard wall-clock preflight failed: %s",
          hal_status_to_string(time_status));
    return false;
  }

  wireguard_platform_init();

  ip4_addr_t endpoint4 = {};
  if (!resolve_ipv4(remote_peer_address, &endpoint4)) {
    return false;
  }

  ip4_addr_t ipaddr = {};
  ip4_addr_t netmask = {};
  ip4_addr_t gateway = {};
  IP4_ADDR(&ipaddr, local_ip[0], local_ip[1], local_ip[2], local_ip[3]);

  const bool route_all = ipv4_is_zero(allowed_ip) && ipv4_is_zero(allowed_mask);
  if (route_all) {
    IP4_ADDR(&netmask, 255u, 255u, 255u, 255u);
  } else {
    IP4_ADDR(&netmask, allowed_mask[0], allowed_mask[1], allowed_mask[2],
             allowed_mask[3]);
  }

  JHLwipExtensionGuard stack_guard(extension, true);
  if (stack_guard.status() != HAL_OK) {
    log_e(TAG "Failed to enter lwIP context: %s",
          hal_status_to_string(stack_guard.status()));
    return false;
  }

  void *underlay = nullptr;
  const hal_status_t underlay_status =
      jh_lwip_extension_underlay_netif(extension, &underlay);
  if (underlay_status != HAL_OK) {
    log_e(TAG "No host-lwIP underlay netif: %s",
          hal_status_to_string(underlay_status));
    return false;
  }

  s_previous_default_netif = netif_default;
  s_wg_netif.name[0] = 'w';
  s_wg_netif.name[1] = 'g';

  struct wireguardif_init_data wg_init = {};
  wg_init.private_key = private_key;
  wg_init.listen_port = 0u;
  wg_init.bind_netif = static_cast<struct netif *>(underlay);

  if (netif_add(&s_wg_netif, &ipaddr, &netmask, &gateway, &wg_init,
                &wireguardif_init, &ip_input) == nullptr) {
    s_previous_default_netif = nullptr;
    return false;
  }
  s_netif_added = true;

  struct wireguardif_peer peer = {};
  peer.public_key = remote_peer_public_key;
  IP_ADDR4(&peer.allowed_ip, allowed_ip[0], allowed_ip[1], allowed_ip[2],
           allowed_ip[3]);
  IP_ADDR4(&peer.allowed_mask, allowed_mask[0], allowed_mask[1],
           allowed_mask[2], allowed_mask[3]);
  IP_ADDR4(&peer.endpoint_ip, ip4_addr1(&endpoint4), ip4_addr2(&endpoint4),
           ip4_addr3(&endpoint4), ip4_addr4(&endpoint4));
  peer.endport_port = remote_peer_port;

  err_t status = wireguardif_add_peer(&s_wg_netif, &peer, &s_peer_index);
  if (status != ERR_OK) {
    teardown_locked();
    return false;
  }

  netif_set_up(&s_wg_netif);
  status = wireguardif_connect(&s_wg_netif, s_peer_index);
  if (status != ERR_OK) {
    teardown_locked();
    return false;
  }

  if (route_all) {
    netif_set_default(&s_wg_netif);
    s_default_route_changed = true;
  }

  initialized_ = true;
  return true;
}

void JHWireGuardClient::end() {
  if (!initialized_) {
    return;
  }

  JHLwipExtensionGuard stack_guard(jh_lwip_extension_platform_port(), false);
  if (stack_guard.status() != HAL_OK) {
    log_e(TAG "Failed to enter lwIP context for teardown: %s",
          hal_status_to_string(stack_guard.status()));
    return;
  }

  teardown_locked();
  initialized_ = false;
  has_kicked_ = false;
  last_kick_ms_ = 0u;
}

bool JHWireGuardClient::peer_up(uint8_t current_endpoint_ip[4],
                                uint16_t *current_endpoint_port) const {
  if (!initialized_ || s_peer_index == WIREGUARDIF_INVALID_INDEX) {
    return false;
  }

  ip_addr_t endpoint = {};
  u16_t port = 0u;
  JHLwipExtensionGuard stack_guard(jh_lwip_extension_platform_port(), false);
  if (stack_guard.status() != HAL_OK ||
      wireguardif_poll(&s_wg_netif) != ERR_OK) {
    return false;
  }
  const err_t status = wireguardif_peer_is_up(
      &s_wg_netif, s_peer_index,
      current_endpoint_ip != nullptr ? &endpoint : nullptr,
      current_endpoint_port != nullptr ? &port : nullptr);
  if (status != ERR_OK) {
    return false;
  }

  if (current_endpoint_ip != nullptr) {
    if (!IP_IS_V4(&endpoint)) {
      memset(current_endpoint_ip, 0, 4u);
    } else {
      const ip4_addr_t *address = ip_2_ip4(&endpoint);
      current_endpoint_ip[0] = ip4_addr1(address);
      current_endpoint_ip[1] = ip4_addr2(address);
      current_endpoint_ip[2] = ip4_addr3(address);
      current_endpoint_ip[3] = ip4_addr4(address);
    }
  }
  if (current_endpoint_port != nullptr) {
    *current_endpoint_port = port;
  }
  return true;
}

bool JHWireGuardClient::kick_handshake(const uint8_t probe_ip[4],
                                       uint16_t probe_port,
                                       uint32_t min_interval_ms) {
  if (!initialized_ || probe_ip == nullptr || probe_port == 0u) {
    return false;
  }

  const jh_lwip_extension_port_t *extension = jh_lwip_extension_platform_port();
  uint32_t now = 0u;
  if (jh_lwip_extension_monotonic_ms(extension, &now) != HAL_OK) {
    return false;
  }
  if (has_kicked_ && (uint32_t)(now - last_kick_ms_) < min_interval_ms) {
    return true;
  }

  {
    JHLwipExtensionGuard stack_guard(extension, false);
    if (stack_guard.status() != HAL_OK ||
        wireguardif_poll(&s_wg_netif) != ERR_OK) {
      return false;
    }
  }

  // send_udp_probe() enters the host-lwIP context itself. Release the poll
  // guard first so non-recursive stack locks cannot deadlock here.
  if (jh_lwip_extension_send_udp_probe(extension, probe_ip, probe_port) !=
      HAL_OK) {
    return false;
  }

  has_kicked_ = true;
  last_kick_ms_ = now;
  return true;
}

#endif /* HAL_ENABLE_WIREGUARD */
