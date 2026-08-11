#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP
#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_WIREGUARD)

#include "hal/network/hal_net.h"
#include "hal/network/jh_lwip_extension.h"
#include "hal/network/jh_lwip_extension_port_common.h"
#include "hal/network/jh_lwip_status.h"
#include "hal/network/wireguard/core/crypto/crypto.h"
#include "hal/system/hal_system.h"

#include "drivers/rp2040/rp2040_cyw43_provider.h"

#include <hardware/regs/addressmap.h>
#include <hardware/regs/rosc.h>
#include <lwip/ip_addr.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/timeouts.h>
#include <lwip/udp.h>
#include <string.h>
#include <sys/time.h>

extern "C" struct netif *__getCYW43Netif();

namespace {

constexpr time_t kMinimumWireGuardUnixTime = 1577836800; // 2020-01-01 UTC

hal_status_t stack_enter(void *, bool require_ipv4) {
  return jh_rp2040_cyw43_provider_lwip_begin(require_ipv4);
}

void stack_leave(void *) { jh_rp2040_cyw43_provider_lwip_end(); }

hal_status_t underlay_netif(void *, void **out_netif) {
  if (out_netif == nullptr) {
    return HAL_EINVAL;
  }
  *out_netif = __getCYW43Netif();
  return *out_netif != nullptr ? HAL_OK : HAL_ESTATE;
}

hal_status_t resolve_ipv4(void *, const char *host_or_ip,
                          uint8_t out_address[JH_LWIP_EXTENSION_IPV4_SIZE]) {
  return jh_lwip_extension_resolve_ipv4(host_or_ip, out_address);
}

hal_status_t monotonic_ms(void *, uint32_t *out_millis) {
  return jh_lwip_extension_monotonic_ms(out_millis);
}

uint32_t hardware_random_word() {
  uint32_t random = 0u;
  volatile const uint32_t *random_bit =
      reinterpret_cast<volatile const uint32_t *>(ROSC_BASE +
                                                  ROSC_RANDOMBIT_OFFSET);
  for (uint32_t bit = 0u; bit < 32u; ++bit) {
    random = (random << 1u) | (*random_bit & 1u);
  }
  return random;
}

hal_status_t random_bytes(void *, void *buffer, size_t size) {
  if (size > 0u && buffer == nullptr) {
    return HAL_EINVAL;
  }

  uint8_t *output = static_cast<uint8_t *>(buffer);
  while (size >= sizeof(uint32_t)) {
    const uint32_t random = hardware_random_word();
    memcpy(output, &random, sizeof(random));
    output += sizeof(random);
    size -= sizeof(random);
  }
  if (size > 0u) {
    const uint32_t random = hardware_random_word();
    memcpy(output, &random, size);
  }
  return HAL_OK;
}

hal_status_t tai64n_now(void *,
                        uint8_t out_tai64n[JH_LWIP_EXTENSION_TAI64N_SIZE]) {
  if (out_tai64n == nullptr) {
    return HAL_EINVAL;
  }

  struct timeval now = {};
  if (gettimeofday(&now, nullptr) != 0) {
    return HAL_EIO;
  }
  if (now.tv_sec < kMinimumWireGuardUnixTime) {
    return HAL_ESTATE;
  }

  const uint64_t seconds =
      0x400000000000000aULL + static_cast<uint64_t>(now.tv_sec);
  const uint32_t nanoseconds = static_cast<uint32_t>(now.tv_usec) * 1000u;
  U64TO8_BIG(out_tai64n, seconds);
  U32TO8_BIG(out_tai64n + 8u, nanoseconds);
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

#endif /* HAL_ENABLE_WIREGUARD */
#endif /* HAL_TARGET_IS_RP */
