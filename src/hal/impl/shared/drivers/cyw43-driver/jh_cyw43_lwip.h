#pragma once

#include "../../../../hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool initialized;
  bool netif_present;
  bool link_up;
  bool dhcp_bound;
  int32_t wifi_link_status;
  int32_t tcpip_link_status;
  int32_t last_cyw43_error;
  uint32_t ipv4;
  uint32_t netmask;
  uint32_t gateway;
  uint32_t dns;
  uint32_t generation;
  uint32_t service_calls;
  uint32_t host_wake_services;
  uint32_t lwip_heap_used;
  uint32_t lwip_heap_peak;
  uint32_t lwip_pool_used;
  uint32_t lwip_pool_peak;
  uint32_t lwip_allocation_errors;
} jh_cyw43_lwip_snapshot_t;

/** Run the single NO_SYS lwIP/CYW43 execution context once. */
hal_status_t jh_cyw43_lwip_service(void);

/** Join a station and wait for a DHCP lease while servicing CYW43/lwIP. */
hal_status_t jh_cyw43_lwip_join(const char *ssid, const char *password,
                                uint32_t auth_type, uint32_t timeout_ms);

/** Resolve one IPv4 address with a bounded asynchronous DNS request. */
hal_status_t jh_cyw43_lwip_resolve_ipv4(const char *hostname,
                                        uint32_t *out_address,
                                        uint32_t timeout_ms);

/** Send one ICMP echo to an IPv4 address in lwIP network byte order. */
hal_status_t jh_cyw43_lwip_ping_ipv4(uint32_t address, uint32_t timeout_ms,
                                     int *out_ttl, uint32_t *out_rtt_ms);

/** Leave the station without destroying the owned netif. */
hal_status_t jh_cyw43_lwip_leave(void);

hal_status_t jh_cyw43_lwip_get_snapshot(jh_cyw43_lwip_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
