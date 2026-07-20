#pragma once

#include "../../../hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JH_LWIP_EXTENSION_IPV4_SIZE 4u
#define JH_LWIP_EXTENSION_TAI64N_SIZE 12u

typedef struct {
  void *context;
  hal_status_t (*stack_enter)(void *context, bool require_ipv4);
  void (*stack_leave)(void *context);
  hal_status_t (*underlay_netif)(void *context, void **out_netif);
  hal_status_t (*resolve_ipv4)(
      void *context, const char *host_or_ip,
      uint8_t out_address[JH_LWIP_EXTENSION_IPV4_SIZE]);
  hal_status_t (*monotonic_ms)(void *context, uint32_t *out_millis);
  hal_status_t (*random_bytes)(void *context, void *buffer, size_t size);
  hal_status_t (*tai64n_now)(void *context,
                             uint8_t out_tai64n[JH_LWIP_EXTENSION_TAI64N_SIZE]);
  hal_status_t (*send_udp_probe)(
      void *context, const uint8_t address[JH_LWIP_EXTENSION_IPV4_SIZE],
      uint16_t port);
} jh_lwip_extension_port_t;

typedef struct {
  const jh_lwip_extension_port_t *port;
  bool entered;
} jh_lwip_extension_guard_t;

hal_status_t jh_lwip_extension_validate(const jh_lwip_extension_port_t *port);
hal_status_t
jh_lwip_extension_guard_enter(const jh_lwip_extension_port_t *port,
                              bool require_ipv4,
                              jh_lwip_extension_guard_t *out_guard);
void jh_lwip_extension_guard_leave(jh_lwip_extension_guard_t *guard);
hal_status_t
jh_lwip_extension_underlay_netif(const jh_lwip_extension_port_t *port,
                                 void **out_netif);
hal_status_t jh_lwip_extension_resolve_ipv4(
    const jh_lwip_extension_port_t *port, const char *host_or_ip,
    uint8_t out_address[JH_LWIP_EXTENSION_IPV4_SIZE]);
hal_status_t
jh_lwip_extension_monotonic_ms(const jh_lwip_extension_port_t *port,
                               uint32_t *out_millis);
hal_status_t
jh_lwip_extension_random_bytes(const jh_lwip_extension_port_t *port,
                               void *buffer, size_t size);
hal_status_t
jh_lwip_extension_tai64n_now(const jh_lwip_extension_port_t *port,
                             uint8_t out_tai64n[JH_LWIP_EXTENSION_TAI64N_SIZE]);
hal_status_t jh_lwip_extension_send_udp_probe(
    const jh_lwip_extension_port_t *port,
    const uint8_t address[JH_LWIP_EXTENSION_IPV4_SIZE], uint16_t port_number);

/* Implemented by the compile-time-selected host-lwIP platform backend. */
const jh_lwip_extension_port_t *jh_lwip_extension_platform_port(void);

#ifdef __cplusplus
}

class JHLwipExtensionGuard final {
public:
  JHLwipExtensionGuard(const jh_lwip_extension_port_t *port, bool require_ipv4)
      : guard_{nullptr, false},
        status_(jh_lwip_extension_guard_enter(port, require_ipv4, &guard_)) {}

  ~JHLwipExtensionGuard() { jh_lwip_extension_guard_leave(&guard_); }

  JHLwipExtensionGuard(const JHLwipExtensionGuard &) = delete;
  JHLwipExtensionGuard &operator=(const JHLwipExtensionGuard &) = delete;

  hal_status_t status() const { return status_; }
  bool entered() const { return guard_.entered; }

private:
  jh_lwip_extension_guard_t guard_;
  hal_status_t status_;
};
#endif
