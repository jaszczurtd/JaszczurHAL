#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_STM32G474
#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_WIREGUARD)

#include "hal/network/cyw43/jh_cyw43_driver.h"
#include "hal/network/hal_net.h"
#include "hal/network/jh_lwip_extension.h"
#include "hal/network/jh_lwip_extension_port_common.h"
#include "hal/network/jh_lwip_status.h"
#include "hal/network/jh_network_backend.h"
#include "hal/network/wireguard/core/crypto/crypto.h"
#include "hal/security/jh_secure_random.h"
#include "hal/system/hal_system.h"
#include "hal/time/hal_time.h"

#include <lwip/ip_addr.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>

#include <string.h>
#include <sys/time.h>

namespace {

constexpr uint64_t kMinimumWireGuardUnixTime = UINT64_C(1577836800);

const jh_network_service_ops_t *service_ops() {
  const jh_network_backend_descriptor_t *backend =
      jh_network_backend_selected();
  return backend != nullptr ? backend->service : nullptr;
}

hal_status_t stack_enter(void *, bool require_ipv4) {
  const jh_network_service_ops_t *service = service_ops();
  return service != nullptr && service->stack_enter != nullptr
             ? service->stack_enter(require_ipv4)
             : HAL_ECONFIG;
}

void stack_leave(void *) {
  const jh_network_service_ops_t *service = service_ops();
  if (service != nullptr && service->stack_leave != nullptr) {
    service->stack_leave();
  }
}

hal_status_t underlay_netif(void *, void **out_netif) {
  if (out_netif == nullptr) {
    return HAL_EINVAL;
  }
  *out_netif = &cyw43_state.netif[CYW43_ITF_STA];
  return HAL_OK;
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
  struct timeval now = {};
  if (gettimeofday(&now, nullptr) != 0 || now.tv_sec < 0) {
    memset(out_tai64n, 0, JH_LWIP_EXTENSION_TAI64N_SIZE);
    return HAL_ESTATE;
  }
  const uint64_t unix_seconds = static_cast<uint64_t>(now.tv_sec);
  if (unix_seconds < kMinimumWireGuardUnixTime) {
    memset(out_tai64n, 0, JH_LWIP_EXTENSION_TAI64N_SIZE);
    return HAL_ESTATE;
  }

  const uint64_t seconds = UINT64_C(0x400000000000000a) + unix_seconds;
  U64TO8_BIG(out_tai64n, seconds);
  U32TO8_BIG(out_tai64n + 8u, static_cast<uint32_t>(now.tv_usec) * 1000u);
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

#endif
#endif
