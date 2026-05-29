/*
 * WireGuard implementation for ESP32 Arduino by Kenta Ida (fuga@fugafuga.org)
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../../../../../hal_config.h"
#if defined(HAL_ENABLE_WIREGUARD)

#include "arduino-wireguard-pico-w.h"

#include <WiFi.h>

#include <lwip/udp.h>
#include <lwip/netif.h>
#include <lwip/ip4_addr.h>
#include <lwip/inet.h>
#include <lwip/ip_addr.h>
#include <lwip/ip4_addr.h>

#include "wireguardif.h"
#include "wireguard-platform.h"
#include "wg_port_pico.h"

// ---- cyw43/lwIP lock (Pico W) ----------------------------------------------
// On Pico W the cyw43/lwIP stack runs in async_context_threadsafe_background
// mode: lwIP timers (incl. the self-rescheduling wireguardif_tmr) and udp RX
// callbacks fire from a low-priority IRQ/alarm, preempting normal code.
// begin()/beginAdvanced()/end() mutate lwIP state (netif list, udp_pcb,
// sys_timeout list) and free the wireguard device, so they must run atomically
// w.r.t. that background context. cyw43_arch_lwip_begin/end is the recursive
// lock that defers it; the app-level mutex in hal_wireguard.cpp does NOT cover
// this. (See JaszczurHAL notes: WG reconnect race -> watchdog reboots.)
#if __has_include("pico/cyw43_arch.h")
#include "pico/cyw43_arch.h"
#define WG_HAS_LWIP_LOCK 1
#endif

// Compile-time guarantee: on RP2040 the lock MUST be compiled in. If
// cyw43_arch.h is ever not found here, fail the build loudly instead of
// silently degrading to a no-op (which would reintroduce the reconnect race).
#if defined(ARDUINO_ARCH_RP2040) && !defined(WG_HAS_LWIP_LOCK)
#error "WireGuard: pico/cyw43_arch.h not found on RP2040 build; lwIP lock would be a silent no-op"
#endif

namespace {
// RAII guard: acquires the cyw43/lwIP lock for its lifetime. Auto-releases on
// every scope exit, including the early `return` paths in beginAdvanced().
struct WgLwipLock {
    WgLwipLock() {
#ifdef WG_HAS_LWIP_LOCK
        cyw43_arch_lwip_begin();
#endif
    }
    ~WgLwipLock() {
#ifdef WG_HAS_LWIP_LOCK
        cyw43_arch_lwip_end();
#endif
    }
    WgLwipLock(const WgLwipLock &) = delete;
    WgLwipLock &operator=(const WgLwipLock &) = delete;
};
} // namespace

// ---- Globals kept for backward-compat with the original library API ----
static struct netif wg_netif_instance;
static struct netif *wg_netif = &wg_netif_instance;
static struct netif *previous_default_netif = nullptr;
static uint8_t peer_index = WIREGUARDIF_INVALID_INDEX;

static bool resolve_ipv4(const char *host_or_ip, ip4_addr_t *out) {
    if (host_or_ip == nullptr || out == nullptr) {
        return false;
    }

    // Fast path: literal IPv4.
    if (ip4addr_aton(host_or_ip, out)) {
        return true;
    }

    // Fallback: use Arduino WiFi resolver.
    IPAddress resolved;
    // WiFi.hostByName returns 1 on success (Arduino convention).
    if (WiFi.hostByName(host_or_ip, resolved) != 1) {
        return false;
    }

    IP4_ADDR(out, resolved[0], resolved[1], resolved[2], resolved[3]);
    return true;
}

bool WireGuard::begin(const IPAddress &localIP,
                     const char *privateKey,
                     const char *remotePeerAddress,
                     const char *remotePeerPublicKey,
                     uint16_t remotePeerPort) {
    // Historical behavior: route everything via WireGuard.
    const IPAddress allowedIP(0, 0, 0, 0);
    const IPAddress allowedMask(0, 0, 0, 0);
    return beginAdvanced(localIP, privateKey, remotePeerAddress, remotePeerPublicKey,
                         remotePeerPort, allowedIP, allowedMask);
}

bool WireGuard::beginAdvanced(const IPAddress &localIP,
                             const char *privateKey,
                             const char *remotePeerAddress,
                             const char *remotePeerPublicKey,
                             uint16_t remotePeerPort,
                             const IPAddress &allowedIP,
                             const IPAddress &allowedMask) {
    if (_is_initialized) {
        return true;
    }
    if (privateKey == nullptr || remotePeerAddress == nullptr || remotePeerPublicKey == nullptr) {
        return false;
    }

    log_d(TAG "initial parameters OK");

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

    const bool route_all = (allowedIP == IPAddress(0, 0, 0, 0)) && (allowedMask == IPAddress(0, 0, 0, 0));
    if (route_all) {
        // /32 - only the interface address is treated as local. Default route will be switched to WG.
        IP4_ADDR(&netmask, 255, 255, 255, 255);
    } else {
        // Use the allowed IP mask as the interface netmask to get proper routing without changing default netif.
        IP4_ADDR(&netmask, allowedMask[0], allowedMask[1], allowedMask[2], allowedMask[3]);
    }

    IP4_ADDR(&gateway, 0, 0, 0, 0);

    // Capture the current default netif (Wi-Fi) so we can bind UDP traffic there.
    // Hold the cyw43/lwIP lock for ALL lwIP mutations below so a firing WG timer
    // / RX callback in the background context cannot observe half-built state.
    // resolve_ipv4() above is intentionally left OUT of the lock: it may perform
    // a blocking DNS query that needs the background context running.
    WgLwipLock _wg_lock;
    previous_default_netif = netif_default;

    // Initialize lwIP netif.
    wg_netif->name[0] = 'w';
    wg_netif->name[1] = 'g';

    log_d(TAG "netif start");

    struct wireguardif_init_data wg_init;
    wg_init.private_key = privateKey;
    wg_init.listen_port = 0;

    log_i(TAG "Previous default netif: %p", previous_default_netif);
    log_i(TAG "WiFi STA netif: %p", tcpip_adapter_get_netif(TCPIP_ADAPTER_IF_STA));

    // Jeśli underlying_netif jest NULL, użyj poprzedniego:
    if (previous_default_netif == NULL) {
        log_e(TAG "No default netif!");
        return false;
    }
    wg_init.bind_netif = previous_default_netif;

    // Important: netif_add expects ip4_addr_t* in this Arduino-Pico (LWIP_IPV6=0) build.
    if (netif_add(wg_netif,
                  &ipaddr,
                  &netmask,
                  &gateway,
                  &wg_init,
                  &wireguardif_init,
                  &ip_input) == nullptr) {
        log_e(TAG "netif_add() failed");
        return false;
    }

    log_d(TAG "peer start");

    struct wireguardif_peer peer;
    memset(&peer, 0, sizeof(peer));

    peer.public_key = remotePeerPublicKey;
    peer.preshared_key = nullptr;
    peer.allowed_ip = allowedIP;
    peer.allowed_mask = allowedMask;
    peer.endpoint_ip = endpoint4;
    peer.endport_port = remotePeerPort;

    err_t perr = wireguardif_add_peer(wg_netif, &peer, &peer_index);
    if (perr != ERR_OK) {
        log_e(TAG "wireguardif_add_peer() failed err=%d", (int)perr);
        return false;
    }

    log_d(TAG "connecting...");

    // Bring up WireGuard.
    netif_set_up(wg_netif);
    wireguardif_connect(wg_netif, peer_index);

    log_d(TAG "connected!...");

    // Route configuration.
    if (route_all) {
        netif_set_default(wg_netif);
    }

    _is_initialized = true;

    log_i(TAG "WireGuard initialized. local=%u.%u.%u.%u endpoint=%u.%u.%u.%u:%u allowed=%u.%u.%u.%u/%u.%u.%u.%u listen=%u",
          localIP[0], localIP[1], localIP[2], localIP[3],
          ip4_addr1(&endpoint4), ip4_addr2(&endpoint4), ip4_addr3(&endpoint4), ip4_addr4(&endpoint4), (unsigned)remotePeerPort,
          allowedIP[0], allowedIP[1], allowedIP[2], allowedIP[3],
          allowedMask[0], allowedMask[1], allowedMask[2], allowedMask[3],
          (unsigned)wg_init.listen_port);

    return true;
}

void WireGuard::end() {
    if (!_is_initialized) {
        return;
    }

    WgLwipLock _wg_lock;
    wireguardif_remove_peer(wg_netif, peer_index);
    netif_remove(wg_netif);

    // netif_remove() does NOT clean up the WireGuard device context. Without
    // this, every begin()/end() cycle leaks the wireguard_device, leaves the
    // UDP pcb registered in lwIP's demux list (with a dangling udp_recv arg)
    // and keeps the self-rescheduling wireguardif_tmr timeout alive forever.
    // Over repeated reconnects this exhausts lwIP MEMP pools and leads to
    // asynchronous hardfaults. wireguardif_shutdown() cancels the timer,
    // removes the pcb and frees the device. netif->state still points at the
    // device here (netif_remove does not clear it), so this is safe.
    wireguardif_shutdown(wg_netif);

    if (previous_default_netif != nullptr) {
        netif_set_default(previous_default_netif);
    }

    _is_initialized = false;
    peer_index = WIREGUARDIF_INVALID_INDEX;
}

bool WireGuard::peerUp(IPAddress* currentEndpointIp, uint16_t* currentEndpointPort) const {
    if (!_is_initialized) return false;
    if (wg_netif == nullptr || peer_index == WIREGUARDIF_INVALID_INDEX) return false;

    ip_addr_t ep_ip;
    u16_t ep_port = 0;

    WgLwipLock _wg_lock;
    err_t rc = wireguardif_peer_is_up(
        wg_netif,
        peer_index,
        (currentEndpointIp ? &ep_ip : nullptr),
        (currentEndpointPort ? &ep_port : nullptr)
    );
    if (rc != ERR_OK) return false;

    if (currentEndpointIp) {
        if (IP_IS_V4(&ep_ip)) {
            const ip4_addr_t* a = ip_2_ip4(&ep_ip);
            *currentEndpointIp = IPAddress(ip4_addr1(a), ip4_addr2(a), ip4_addr3(a), ip4_addr4(a));
        } else {
            *currentEndpointIp = IPAddress(0, 0, 0, 0);
        }
    }
    if (currentEndpointPort) *currentEndpointPort = (uint16_t)ep_port;

    return true;
}

bool WireGuard::kickHandshake(const IPAddress& probeIp, uint16_t probePort, uint32_t minIntervalMs) {
    if (!_is_initialized) return false;

    const uint32_t now = millis();
    if ((uint32_t)(now - _lastKickMs) < minIntervalMs) {
        return true; // rate-limited: already kicked recently
    }
    _lastKickMs = now;

    // Non-blocking trigger: one small UDP datagram to an AllowedIPs address.
    // Serialize the UDP send against the lwIP background context (recursive lock).
    WgLwipLock _wg_lock;
    WiFiUDP udp;
    udp.begin(0); // ephemeral local port
    udp.beginPacket(probeIp, probePort);
    udp.write((uint8_t)0x00);
    udp.endPacket();
    udp.stop();

    return true;
}

#endif /* HAL_ENABLE_WIREGUARD */