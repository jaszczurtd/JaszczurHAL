#include "jh_lwip_extension.h"

static bool port_has_all_operations(const jh_lwip_extension_port_t *port) {
  return port != nullptr && port->stack_enter != nullptr &&
         port->stack_leave != nullptr && port->underlay_netif != nullptr &&
         port->resolve_ipv4 != nullptr && port->monotonic_ms != nullptr &&
         port->random_bytes != nullptr && port->tai64n_now != nullptr &&
         port->send_udp_probe != nullptr;
}

hal_status_t jh_lwip_extension_validate(const jh_lwip_extension_port_t *port) {
  return port_has_all_operations(port) ? HAL_OK : HAL_ECONFIG;
}

hal_status_t
jh_lwip_extension_guard_enter(const jh_lwip_extension_port_t *port,
                              bool require_ipv4,
                              jh_lwip_extension_guard_t *out_guard) {
  if (out_guard == nullptr) {
    return HAL_EINVAL;
  }
  out_guard->port = nullptr;
  out_guard->entered = false;

  const hal_status_t validation_status = jh_lwip_extension_validate(port);
  if (validation_status != HAL_OK) {
    return validation_status;
  }

  const hal_status_t status = port->stack_enter(port->context, require_ipv4);
  if (status == HAL_OK) {
    out_guard->port = port;
    out_guard->entered = true;
  }
  return status;
}

void jh_lwip_extension_guard_leave(jh_lwip_extension_guard_t *guard) {
  if (guard == nullptr || !guard->entered || guard->port == nullptr) {
    return;
  }

  const jh_lwip_extension_port_t *port = guard->port;
  guard->port = nullptr;
  guard->entered = false;
  port->stack_leave(port->context);
}

hal_status_t
jh_lwip_extension_underlay_netif(const jh_lwip_extension_port_t *port,
                                 void **out_netif) {
  if (out_netif == nullptr) {
    return HAL_EINVAL;
  }
  *out_netif = nullptr;
  const hal_status_t validation_status = jh_lwip_extension_validate(port);
  if (validation_status != HAL_OK) {
    return validation_status;
  }

  const hal_status_t status = port->underlay_netif(port->context, out_netif);
  if (status != HAL_OK) {
    return status;
  }
  return *out_netif != nullptr ? HAL_OK : HAL_ESTATE;
}

hal_status_t jh_lwip_extension_resolve_ipv4(
    const jh_lwip_extension_port_t *port, const char *host_or_ip,
    uint8_t out_address[JH_LWIP_EXTENSION_IPV4_SIZE]) {
  if (host_or_ip == nullptr || host_or_ip[0] == '\0' ||
      out_address == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t validation_status = jh_lwip_extension_validate(port);
  return validation_status == HAL_OK
             ? port->resolve_ipv4(port->context, host_or_ip, out_address)
             : validation_status;
}

hal_status_t
jh_lwip_extension_monotonic_ms(const jh_lwip_extension_port_t *port,
                               uint32_t *out_millis) {
  if (out_millis == nullptr) {
    return HAL_EINVAL;
  }
  *out_millis = 0u;
  const hal_status_t validation_status = jh_lwip_extension_validate(port);
  return validation_status == HAL_OK
             ? port->monotonic_ms(port->context, out_millis)
             : validation_status;
}

hal_status_t
jh_lwip_extension_random_bytes(const jh_lwip_extension_port_t *port,
                               void *buffer, size_t size) {
  if (size > 0u && buffer == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t validation_status = jh_lwip_extension_validate(port);
  return validation_status == HAL_OK
             ? port->random_bytes(port->context, buffer, size)
             : validation_status;
}

hal_status_t jh_lwip_extension_tai64n_now(
    const jh_lwip_extension_port_t *port,
    uint8_t out_tai64n[JH_LWIP_EXTENSION_TAI64N_SIZE]) {
  if (out_tai64n == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t validation_status = jh_lwip_extension_validate(port);
  return validation_status == HAL_OK
             ? port->tai64n_now(port->context, out_tai64n)
             : validation_status;
}

hal_status_t jh_lwip_extension_send_udp_probe(
    const jh_lwip_extension_port_t *port,
    const uint8_t address[JH_LWIP_EXTENSION_IPV4_SIZE], uint16_t port_number) {
  if (address == nullptr || port_number == 0u) {
    return HAL_EINVAL;
  }
  const hal_status_t validation_status = jh_lwip_extension_validate(port);
  return validation_status == HAL_OK
             ? port->send_udp_probe(port->context, address, port_number)
             : validation_status;
}
