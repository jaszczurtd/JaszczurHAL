#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_MOCK
#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_NETWORK_CORE) && defined(HAL_NETWORK_BACKEND_HOST)

#include "hal/core/hal_mutex_once.h"
#include "hal/network/jh_network_backend.h"
#include "hal/network/native_sockets/jh_native_socket_platform.h"
#include "hal/network/native_sockets/jh_native_sockets.h"
#include "hal/system/hal_sync.h"

/* Host programs reach the network through the operating system: there is no
 * link to control, so the descriptor offers no WiFi operations and the
 * hal_wifi facade reports HAL_EUNSUPPORTED. */

namespace {

#if defined(JH_NATIVE_SOCKETS_WINSOCK)
hal_mutex_t s_winsock_mutex;
bool s_winsock_started = false;

hal_status_t start_winsock(void) {
  if (jh_hal_mutex_create_once(&s_winsock_mutex) == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_winsock_mutex);
  hal_status_t status = HAL_OK;
  if (!s_winsock_started) {
    // Started once for the process lifetime; Windows releases it at exit.
    WSADATA data = {};
    status = WSAStartup(MAKEWORD(2, 2), &data) == 0 ? HAL_OK : HAL_EIO;
    s_winsock_started = status == HAL_OK;
  }
  hal_mutex_unlock(s_winsock_mutex);
  return status;
}
#endif

hal_status_t service_initialize(void) {
  const hal_status_t ready = jh_native_sockets_require_ready();
  return ready == HAL_OK ? jh_native_sockets_initialize() : ready;
}

hal_status_t service_deinitialize(void) {
  return jh_native_sockets_shutdown_all();
}

hal_status_t service_service(void) { return HAL_OK; }

hal_status_t resolver_resolve(const char *hostname,
                              hal_net_family_t family_hint,
                              hal_net_endpoint_t *results, size_t capacity,
                              size_t *out_count) {
  const hal_status_t argument_status = jh_native_sockets_resolve_validate(
      hostname, family_hint, results, capacity, out_count);
  if (argument_status != HAL_OK) {
    return argument_status;
  }
  const hal_status_t ready = jh_native_sockets_require_ready();
  return ready == HAL_OK
             ? jh_native_sockets_resolve_lookup(hostname, family_hint, results,
                                                capacity, out_count)
             : ready;
}

const jh_network_service_ops_t s_service_ops = {
    service_initialize, service_deinitialize, service_service, nullptr, nullptr,
};

const jh_network_resolver_ops_t s_resolver_ops = {resolver_resolve};

} // namespace

extern "C" hal_status_t jh_native_sockets_require_ready(void) {
#if defined(JH_NATIVE_SOCKETS_WINSOCK)
  return start_winsock();
#else
  return HAL_OK;
#endif
}

extern "C" const jh_network_backend_descriptor_t *
jh_network_backend_selected(void) {
  static const jh_network_backend_descriptor_t backend = {
      JH_NETWORK_BACKEND_ABI_VERSION,
      "host-sockets",
      JH_NET_CAP_DNS |
#if defined(HAL_ENABLE_TCP)
          JH_NET_CAP_TCP_CLIENT | JH_NET_CAP_TCP_LISTENER |
#endif
#if defined(HAL_ENABLE_UDP)
          JH_NET_CAP_UDP |
#endif
          JH_NET_CAP_IPV4 | JH_NET_CAP_IPV6,
      JH_NETWORK_EXECUTION_POLL,
      &s_service_ops,
      nullptr,
      &s_resolver_ops,
#if defined(HAL_ENABLE_TCP)
      jh_native_sockets_tcp_ops(),
#else
      nullptr,
#endif
#if defined(HAL_ENABLE_UDP)
      jh_native_sockets_udp_ops(),
#else
      nullptr,
#endif
  };
  return &backend;
}

#endif // HAL_ENABLE_NETWORK_CORE && HAL_NETWORK_BACKEND_HOST
#endif // HAL_TARGET_IS_MOCK
