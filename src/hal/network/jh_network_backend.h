#pragma once

#include "hal/core/hal_config.h"
#include "hal/core/hal_status.h"
#include "hal/network/hal_net.h"
#include "hal/network/hal_wifi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAL_ENABLE_NETWORK_CORE

#define JH_NETWORK_BACKEND_ABI_VERSION 1u

typedef uint64_t jh_network_capabilities_t;

#define JH_NET_CAP_WIFI_STA (UINT64_C(1) << 0u)
#define JH_NET_CAP_WIFI_AP (UINT64_C(1) << 1u)
#define JH_NET_CAP_WIFI_SCAN (UINT64_C(1) << 2u)
#define JH_NET_CAP_DNS (UINT64_C(1) << 3u)
#define JH_NET_CAP_PING (UINT64_C(1) << 4u)
#define JH_NET_CAP_TCP_CLIENT (UINT64_C(1) << 5u)
#define JH_NET_CAP_TCP_LISTENER (UINT64_C(1) << 6u)
#define JH_NET_CAP_UDP (UINT64_C(1) << 7u)
#define JH_NET_CAP_IPV4 (UINT64_C(1) << 8u)
#define JH_NET_CAP_IPV6 (UINT64_C(1) << 9u)
#define JH_NET_CAP_POWER_SAVE (UINT64_C(1) << 10u)
#define JH_NET_CAP_TLS_OFFLOAD (UINT64_C(1) << 11u)
#define JH_NET_CAP_AUX_GPIO (UINT64_C(1) << 12u)
#define JH_NET_CAP_HOST_STACK_L3 (UINT64_C(1) << 13u)
#define JH_NET_CAP_VIRTUAL_NETIF_ROUTE (UINT64_C(1) << 14u)
#define JH_NET_CAP_SECURE_ENTROPY (UINT64_C(1) << 15u)
#define JH_NET_CAP_STACK_CONTEXT (UINT64_C(1) << 16u)

typedef enum {
  JH_NETWORK_EXECUTION_POLL = 0,
  JH_NETWORK_EXECUTION_OWNED_WORKER,
  JH_NETWORK_EXECUTION_PLATFORM_OWNED
} jh_network_execution_model_t;

typedef struct {
  hal_status_t (*initialize)(void);
  hal_status_t (*deinitialize)(void);
  hal_status_t (*service)(void);
  hal_status_t (*stack_enter)(bool require_ipv4);
  void (*stack_leave)(void);
} jh_network_service_ops_t;

typedef struct {
  hal_status_t (*set_mode)(hal_wifi_mode_t mode);
  hal_status_t (*disconnect)(bool erase_credentials);
  hal_status_t (*set_hostname)(const char *hostname);
  hal_status_t (*join)(const char *ssid, const char *password,
                       bool non_blocking, uint32_t timeout_ms);
  hal_status_t (*get_state)(hal_wifi_state_t *out_state);
  hal_status_t (*get_local_address)(hal_net_endpoint_t *out_address);
  hal_status_t (*get_dns_address)(hal_net_endpoint_t *out_address);
  hal_status_t (*get_mac)(uint8_t out_mac[HAL_WIFI_BSSID_LEN]);
  hal_status_t (*get_rssi)(int32_t *out_rssi);
  hal_status_t (*ping)(const hal_net_endpoint_t *remote, uint32_t timeout_ms,
                       int *out_result);
  hal_status_t (*scan)(uint32_t timeout_ms, int *out_count);
  hal_status_t (*get_scan_result)(size_t index,
                                  hal_wifi_scan_result_t *out_result);
} jh_network_wifi_ops_t;

typedef struct {
  hal_status_t (*resolve)(const char *hostname, hal_net_family_t family_hint,
                          hal_net_endpoint_t *results, size_t capacity,
                          size_t *out_count);
} jh_network_resolver_ops_t;

typedef struct {
  hal_status_t (*socket_open)(void **out_socket);
  hal_status_t (*socket_connect)(void *socket, const hal_net_endpoint_t *remote,
                                 uint32_t timeout_ms);
  hal_status_t (*socket_send)(void *socket, const void *data, size_t len,
                              size_t *out_sent);
  hal_status_t (*socket_recv)(void *socket, void *buffer, size_t max_len,
                              uint32_t timeout_ms, size_t *out_received);
  bool (*socket_can_recv)(void *socket);
  bool (*socket_can_send)(void *socket);
  bool (*socket_is_connected)(void *socket);
  void (*socket_shutdown)(void *socket);
  void (*socket_close)(void *socket);
  hal_status_t (*listener_open)(void **out_listener);
  hal_status_t (*listener_bind)(void *listener,
                                const hal_net_endpoint_t *local);
  hal_status_t (*listener_listen)(void *listener, uint8_t backlog);
  hal_status_t (*listener_accept)(void *listener, hal_net_endpoint_t *remote,
                                  uint32_t timeout_ms, void **out_socket);
  bool (*listener_can_accept)(void *listener);
  void (*listener_close)(void *listener);
} jh_network_tcp_ops_t;

typedef struct {
  hal_status_t (*socket_open)(void **out_socket);
  hal_status_t (*socket_bind)(void *socket, const hal_net_endpoint_t *local);
  hal_status_t (*socket_sendto)(void *socket, const void *data, size_t len,
                                const hal_net_endpoint_t *remote,
                                size_t *out_sent);
  hal_status_t (*socket_recvfrom)(void *socket, void *buffer, size_t max_len,
                                  hal_net_endpoint_t *remote,
                                  uint32_t timeout_ms, size_t *out_received);
  bool (*socket_can_recv)(void *socket);
  bool (*socket_can_send)(void *socket);
  void (*socket_close)(void *socket);
} jh_network_udp_ops_t;

typedef struct {
  uint32_t abi_version;
  const char *name;
  jh_network_capabilities_t capabilities;
  jh_network_execution_model_t execution_model;
  const jh_network_service_ops_t *service;
  const jh_network_wifi_ops_t *wifi;
  const jh_network_resolver_ops_t *resolver;
  const jh_network_tcp_ops_t *tcp;
  const jh_network_udp_ops_t *udp;
} jh_network_backend_descriptor_t;

hal_status_t
jh_network_backend_validate(const jh_network_backend_descriptor_t *backend,
                            jh_network_capabilities_t required_capabilities);

/** Defined exactly once by the selected backend package. */
const jh_network_backend_descriptor_t *jh_network_backend_selected(void);

#endif /* HAL_ENABLE_NETWORK_CORE */

#ifdef __cplusplus
}
#endif
