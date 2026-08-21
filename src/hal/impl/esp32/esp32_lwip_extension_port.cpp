#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_ESP32_FAMILY
#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_WIREGUARD) && defined(HAL_NETWORK_BACKEND_ESP_IDF)

#include "hal/network/jh_lwip_extension.h"
#include "hal/network/jh_lwip_extension_port_common.h"
#include "hal/network/wireguard/core/crypto/crypto.h"
#include "hal/security/jh_secure_random.h"
#include "jh_esp32_network.h"

#include <string.h>
#include <sys/time.h>

namespace {

constexpr uint64_t kMinimumWireGuardUnixTime = UINT64_C(1577836800);

hal_status_t stack_enter(void *, bool require_ipv4) {
  return jh_esp32_network_stack_enter(require_ipv4);
}

void stack_leave(void *) { jh_esp32_network_stack_leave(); }

hal_status_t underlay_netif(void *, void **out_netif) {
  return jh_esp32_network_underlay_netif(out_netif);
}

hal_status_t resolve_ipv4(void *, const char *host_or_ip,
                          uint8_t out_address[JH_LWIP_EXTENSION_IPV4_SIZE]) {
  return jh_lwip_extension_resolve_ipv4(host_or_ip, out_address);
}

hal_status_t monotonic_ms(void *, uint32_t *out_millis) {
  return jh_lwip_extension_monotonic_ms(out_millis);
}

hal_status_t random_bytes(void *, void *buffer, size_t size) {
  return jh_secure_random_bytes(buffer, size);
}

hal_status_t tai64n_now(void *,
                        uint8_t out_tai64n[JH_LWIP_EXTENSION_TAI64N_SIZE]) {
  if (out_tai64n == nullptr) {
    return HAL_EINVAL;
  }
  timeval now = {};
  if (gettimeofday(&now, nullptr) != 0 || now.tv_sec < 0 ||
      static_cast<uint64_t>(now.tv_sec) < kMinimumWireGuardUnixTime) {
    memset(out_tai64n, 0, JH_LWIP_EXTENSION_TAI64N_SIZE);
    return HAL_ESTATE;
  }

  const uint64_t seconds =
      UINT64_C(0x400000000000000a) + static_cast<uint64_t>(now.tv_sec);
  U64TO8_BIG(out_tai64n, seconds);
  U32TO8_BIG(out_tai64n + 8u,
             static_cast<uint32_t>(now.tv_usec) * UINT32_C(1000));
  return HAL_OK;
}

hal_status_t send_udp_probe(void *,
                            const uint8_t address[JH_LWIP_EXTENSION_IPV4_SIZE],
                            uint16_t port) {
  return jh_lwip_extension_send_udp_probe(address, port);
}

const jh_lwip_extension_port_t s_port = {
    nullptr,      stack_enter,  stack_leave, underlay_netif, resolve_ipv4,
    monotonic_ms, random_bytes, tai64n_now,  send_udp_probe,
};

} // namespace

extern "C" const jh_lwip_extension_port_t *
jh_lwip_extension_platform_port(void) {
  return &s_port;
}

#endif // HAL_ENABLE_WIREGUARD && HAL_NETWORK_BACKEND_ESP_IDF
#endif // HAL_TARGET_IS_ESP32_FAMILY
