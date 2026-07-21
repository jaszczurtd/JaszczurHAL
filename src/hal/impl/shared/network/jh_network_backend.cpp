#include "jh_network_backend.h"

#ifdef HAL_ENABLE_NETWORK_CORE

static bool has_all(jh_network_capabilities_t value,
                    jh_network_capabilities_t required) {
  return (value & required) == required;
}

hal_status_t
jh_network_backend_validate(const jh_network_backend_descriptor_t *backend,
                            jh_network_capabilities_t required_capabilities) {
  if (backend == nullptr || backend->name == nullptr ||
      backend->name[0] == '\0' ||
      backend->abi_version != JH_NETWORK_BACKEND_ABI_VERSION ||
      backend->service == nullptr || backend->service->initialize == nullptr ||
      backend->service->service == nullptr) {
    return HAL_ECONFIG;
  }
  if (!has_all(backend->capabilities, required_capabilities)) {
    return HAL_EUNSUPPORTED;
  }
  if (has_all(required_capabilities, JH_NET_CAP_WIFI_STA) &&
      (backend->wifi == nullptr || backend->wifi->set_mode == nullptr ||
       backend->wifi->disconnect == nullptr || backend->wifi->join == nullptr ||
       backend->wifi->get_state == nullptr)) {
    return HAL_ECONFIG;
  }
  if (has_all(required_capabilities, JH_NET_CAP_WIFI_SCAN) &&
      (backend->wifi == nullptr || backend->wifi->scan == nullptr ||
       backend->wifi->get_scan_result == nullptr)) {
    return HAL_ECONFIG;
  }
  if (has_all(required_capabilities, JH_NET_CAP_DNS) &&
      (backend->resolver == nullptr || backend->resolver->resolve == nullptr)) {
    return HAL_ECONFIG;
  }
  if ((required_capabilities &
       (JH_NET_CAP_TCP_CLIENT | JH_NET_CAP_TCP_LISTENER)) != 0u &&
      backend->tcp == nullptr) {
    return HAL_ECONFIG;
  }
  if (has_all(required_capabilities, JH_NET_CAP_TCP_CLIENT) &&
      (backend->tcp->socket_open == nullptr ||
       backend->tcp->socket_connect == nullptr ||
       backend->tcp->socket_send == nullptr ||
       backend->tcp->socket_recv == nullptr ||
       backend->tcp->socket_close == nullptr)) {
    return HAL_ECONFIG;
  }
  if (has_all(required_capabilities, JH_NET_CAP_TCP_LISTENER) &&
      (backend->tcp->listener_open == nullptr ||
       backend->tcp->listener_bind == nullptr ||
       backend->tcp->listener_listen == nullptr ||
       backend->tcp->listener_accept == nullptr ||
       backend->tcp->listener_close == nullptr)) {
    return HAL_ECONFIG;
  }
  if (has_all(required_capabilities, JH_NET_CAP_UDP) &&
      (backend->udp == nullptr || backend->udp->socket_open == nullptr ||
       backend->udp->socket_bind == nullptr ||
       backend->udp->socket_sendto == nullptr ||
       backend->udp->socket_recvfrom == nullptr ||
       backend->udp->socket_close == nullptr)) {
    return HAL_ECONFIG;
  }
  if (has_all(required_capabilities, JH_NET_CAP_STACK_CONTEXT) &&
      (backend->service->stack_enter == nullptr ||
       backend->service->stack_leave == nullptr)) {
    return HAL_ECONFIG;
  }
  if (has_all(required_capabilities, JH_NET_CAP_HOST_STACK_L3) &&
      !has_all(backend->capabilities, JH_NET_CAP_STACK_CONTEXT)) {
    return HAL_ECONFIG;
  }
  return HAL_OK;
}

#endif
