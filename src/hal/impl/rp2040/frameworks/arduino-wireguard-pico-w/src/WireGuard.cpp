/*
 * WireGuard implementation for ESP32 Arduino by Kenta Ida (fuga@fugafuga.org)
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../../../../../hal_config.h"
#if defined(HAL_ENABLE_WIREGUARD)

#include "arduino-wireguard-pico-w.h"

#include <lwip/ip.h>
#include <lwip/ip4_addr.h>
#include <lwip/ip_addr.h>
#include <lwip/netif.h>

#include "../../../../shared/network/jh_lwip_extension.h"
#include "wg_port_pico.h"
#include "wireguard-platform.h"
#include "wireguardif.h"

// ---- Globals kept for backward-compat with the original library API ----
static struct netif wg_netif_instance;
static struct netif *wg_netif = &wg_netif_instance;
static struct netif *previous_default_netif = nullptr;
static uint8_t peer_index = WIREGUARDIF_INVALID_INDEX;
static bool netif_added = false;
static bool default_route_changed = false;

static bool resolve_ipv4(const char *host_or_ip, ip4_addr_t *out) {
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

static void teardown_locked() {
  if (peer_index != WIREGUARDIF_INVALID_INDEX) {
    (void)wireguardif_remove_peer(wg_netif, peer_index);
  }
  if (netif_added) {
    netif_remove(wg_netif);
  }
  if (wg_netif->state != nullptr) {
    wireguardif_shutdown(wg_netif);
  }
  if (default_route_changed && previous_default_netif != nullptr) {
    netif_set_default(previous_default_netif);
  }

  peer_index = WIREGUARDIF_INVALID_INDEX;
  netif_added = false;
  default_route_changed = false;
  previous_default_netif = nullptr;
}

bool WireGuard::begin(const IPAddress &localIP, const char *privateKey,
                      const char *remotePeerAddress,
                      const char *remotePeerPublicKey,
                      uint16_t remotePeerPort) {
  // Historical behavior: route everything via WireGuard.
  const IPAddress allowedIP(0, 0, 0, 0);
  const IPAddress allowedMask(0, 0, 0, 0);
  return beginAdvanced(localIP, privateKey, remotePeerAddress,
                       remotePeerPublicKey, remotePeerPort, allowedIP,
                       allowedMask);
}

bool WireGuard::beginAdvanced(const IPAddress &localIP, const char *privateKey,
                              const char *remotePeerAddress,
                              const char *remotePeerPublicKey,
                              uint16_t remotePeerPort,
                              const IPAddress &allowedIP,
                              const IPAddress &allowedMask) {
  if (_is_initialized) {
    return true;
  }
  if (privateKey == nullptr || remotePeerAddress == nullptr ||
      remotePeerPublicKey == nullptr) {
    return false;
  }

  log_d(TAG "initial parameters OK");

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
  for (volatile uint8_t &byte : entropy_preflight) {
    byte = 0u;
  }
  if (entropy_status != HAL_OK) {
    log_e(TAG "WireGuard entropy preflight failed: %s",
          hal_status_to_string(entropy_status));
    return false;
  }

  uint8_t time_preflight[JH_LWIP_EXTENSION_TAI64N_SIZE] = {};
  const hal_status_t time_status =
      jh_lwip_extension_tai64n_now(extension, time_preflight);
  for (volatile uint8_t &byte : time_preflight) {
    byte = 0u;
  }
  if (time_status != HAL_OK) {
    log_e(TAG "WireGuard wall-clock preflight failed: %s",
          hal_status_to_string(time_status));
    return false;
  }

  // Initialize platform glue (timers, RNG, etc.).
  wireguard_platform_init();

  log_d(TAG "wireguard_platform_init OK");

  // Resolve endpoint.
  ip4_addr_t endpoint4;
  if (!resolve_ipv4(remotePeerAddress, &endpoint4)) {
    log_e(TAG "Failed to resolve endpoint '%s'", remotePeerAddress);
    return false;
  }

  // Prepare interface addresses.
  ip4_addr_t ipaddr;
  ip4_addr_t netmask;
  ip4_addr_t gateway;

  IP4_ADDR(&ipaddr, localIP[0], localIP[1], localIP[2], localIP[3]);

  const bool route_all = (allowedIP == IPAddress(0, 0, 0, 0)) &&
                         (allowedMask == IPAddress(0, 0, 0, 0));
  if (route_all) {
    // /32 - only the interface address is treated as local. Default route will
    // be switched to WG.
    IP4_ADDR(&netmask, 255, 255, 255, 255);
  } else {
    // Use the allowed IP mask as the interface netmask to get proper routing
    // without changing default netif.
    IP4_ADDR(&netmask, allowedMask[0], allowedMask[1], allowedMask[2],
             allowedMask[3]);
  }

  IP4_ADDR(&gateway, 0, 0, 0, 0);

  // Capture the current default netif (Wi-Fi) so we can bind UDP traffic there.
  // Hold the selected platform's lwIP context for ALL mutations below so a
  // firing WG timer / RX callback cannot observe half-built state.
  // resolve_ipv4() above is intentionally left OUT of the lock: it may perform
  // a blocking DNS query that needs the background context running.
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

  previous_default_netif = netif_default;

  // Initialize lwIP netif.
  wg_netif->name[0] = 'w';
  wg_netif->name[1] = 'g';

  log_d(TAG "netif start");

  struct wireguardif_init_data wg_init;
  wg_init.private_key = privateKey;
  wg_init.listen_port = 0;

  log_i(TAG "Previous default netif: %p", previous_default_netif);
  log_i(TAG "Underlay netif: %p", underlay);
  wg_init.bind_netif = static_cast<struct netif *>(underlay);

  // Important: netif_add expects ip4_addr_t* in this Arduino-Pico (LWIP_IPV6=0)
  // build.
  if (netif_add(wg_netif, &ipaddr, &netmask, &gateway, &wg_init,
                &wireguardif_init, &ip_input) == nullptr) {
    log_e(TAG "netif_add() failed");
    previous_default_netif = nullptr;
    return false;
  }
  netif_added = true;

  log_d(TAG "peer start");

  struct wireguardif_peer peer;
  memset(&peer, 0, sizeof(peer));

  peer.public_key = remotePeerPublicKey;
  peer.preshared_key = nullptr;
  IP_ADDR4(&peer.allowed_ip, allowedIP[0], allowedIP[1], allowedIP[2],
           allowedIP[3]);
  IP_ADDR4(&peer.allowed_mask, allowedMask[0], allowedMask[1], allowedMask[2],
           allowedMask[3]);
  IP_ADDR4(&peer.endpoint_ip, ip4_addr1(&endpoint4), ip4_addr2(&endpoint4),
           ip4_addr3(&endpoint4), ip4_addr4(&endpoint4));
  peer.endport_port = remotePeerPort;

  err_t perr = wireguardif_add_peer(wg_netif, &peer, &peer_index);
  if (perr != ERR_OK) {
    log_e(TAG "wireguardif_add_peer() failed err=%d", (int)perr);
    teardown_locked();
    return false;
  }

  log_d(TAG "connecting...");

  // Bring up WireGuard.
  netif_set_up(wg_netif);
  perr = wireguardif_connect(wg_netif, peer_index);
  if (perr != ERR_OK) {
    log_e(TAG "wireguardif_connect() failed err=%d", (int)perr);
    teardown_locked();
    return false;
  }

  log_d(TAG "connected!...");

  // Route configuration.
  if (route_all) {
    netif_set_default(wg_netif);
    default_route_changed = true;
  }

  _is_initialized = true;

  log_i(TAG "WireGuard initialized. local=%u.%u.%u.%u endpoint=%u.%u.%u.%u:%u "
            "allowed=%u.%u.%u.%u/%u.%u.%u.%u listen=%u",
        localIP[0], localIP[1], localIP[2], localIP[3], ip4_addr1(&endpoint4),
        ip4_addr2(&endpoint4), ip4_addr3(&endpoint4), ip4_addr4(&endpoint4),
        (unsigned)remotePeerPort, allowedIP[0], allowedIP[1], allowedIP[2],
        allowedIP[3], allowedMask[0], allowedMask[1], allowedMask[2],
        allowedMask[3], (unsigned)wg_init.listen_port);

  return true;
}

void WireGuard::end() {
  if (!_is_initialized) {
    return;
  }

  JHLwipExtensionGuard stack_guard(jh_lwip_extension_platform_port(), false);
  if (stack_guard.status() != HAL_OK) {
    log_e(TAG "Failed to enter lwIP context for teardown: %s",
          hal_status_to_string(stack_guard.status()));
    return;
  }

  teardown_locked();
  _is_initialized = false;
  _has_kicked = false;
  _lastKickMs = 0u;
}

bool WireGuard::peerUp(IPAddress *currentEndpointIp,
                       uint16_t *currentEndpointPort) const {
  if (!_is_initialized)
    return false;
  if (wg_netif == nullptr || peer_index == WIREGUARDIF_INVALID_INDEX)
    return false;

  ip_addr_t ep_ip;
  u16_t ep_port = 0;

  JHLwipExtensionGuard stack_guard(jh_lwip_extension_platform_port(), false);
  if (stack_guard.status() != HAL_OK)
    return false;
  if (wireguardif_poll(wg_netif) != ERR_OK)
    return false;
  err_t rc = wireguardif_peer_is_up(wg_netif, peer_index,
                                    (currentEndpointIp ? &ep_ip : nullptr),
                                    (currentEndpointPort ? &ep_port : nullptr));
  if (rc != ERR_OK)
    return false;

  if (currentEndpointIp) {
    if (IP_IS_V4(&ep_ip)) {
      const ip4_addr_t *a = ip_2_ip4(&ep_ip);
      *currentEndpointIp =
          IPAddress(ip4_addr1(a), ip4_addr2(a), ip4_addr3(a), ip4_addr4(a));
    } else {
      *currentEndpointIp = IPAddress(0, 0, 0, 0);
    }
  }
  if (currentEndpointPort)
    *currentEndpointPort = (uint16_t)ep_port;

  return true;
}

bool WireGuard::kickHandshake(const IPAddress &probeIp, uint16_t probePort,
                              uint32_t minIntervalMs) {
  if (!_is_initialized)
    return false;

  const jh_lwip_extension_port_t *extension = jh_lwip_extension_platform_port();
  uint32_t now = 0u;
  if (jh_lwip_extension_monotonic_ms(extension, &now) != HAL_OK) {
    return false;
  }
  if (_has_kicked && (uint32_t)(now - _lastKickMs) < minIntervalMs) {
    return true; // rate-limited: already kicked recently
  }

  (void)probeIp;
  (void)probePort;
  JHLwipExtensionGuard stack_guard(extension, false);
  if (stack_guard.status() != HAL_OK || wireguardif_poll(wg_netif) != ERR_OK) {
    return false;
  }

  _has_kicked = true;
  _lastKickMs = now;
  return true;
}

#endif /* HAL_ENABLE_WIREGUARD */
