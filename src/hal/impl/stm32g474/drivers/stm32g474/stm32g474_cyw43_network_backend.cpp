#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_STM32G474
#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_NETWORK_CORE) && defined(HAL_NETWORK_BACKEND_CYW43) &&  \
    defined(HAL_CYW43_BUS_STM32_GSPI) && defined(HAL_CYW43_STACK_LWIP)

#include "hal/core/hal_mutex_once.h"
#include "hal/network/cyw43/jh_cyw43_driver.h"
#include "hal/network/cyw43/jh_cyw43_hostname.h"
#include "hal/network/cyw43/jh_cyw43_lwip.h"
#include "hal/network/cyw43/jh_cyw43_radio.h"
#include "hal/network/cyw43/jh_cyw43_scan_results.h"
#include "hal/network/jh_cyw43_scan.h"
#include "hal/network/jh_net_address_utils.h"
#include "hal/network/jh_network_backend.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

#if defined(HAL_ENABLE_TCP)
#include "hal/network/jh_lwip_tcp.h"
#endif
#if defined(HAL_ENABLE_UDP)
#include "hal/network/jh_lwip_udp.h"
#endif

extern "C" {
#include "lwip/netif.h"
}

#include <string.h>

#ifndef HAL_CYW43_SCAN_RESULT_CAPACITY
#define HAL_CYW43_SCAN_RESULT_CAPACITY 32u
#endif

namespace {

constexpr uint32_t kDnsTimeoutMs = 10000u;
hal_mutex_t s_pool_mutex;
hal_mutex_t s_lifecycle_mutex;
bool s_initialized;
char s_hostname[64]{};
hal_wifi_scan_result_t s_scan_results[HAL_CYW43_SCAN_RESULT_CAPACITY]{};
size_t s_scan_count;
bool s_scan_overflow;

#if defined(HAL_ENABLE_TCP)
struct tcp_socket_slot_t {
  bool in_use;
  jh_lwip_tcp_socket_t socket;
};

struct tcp_listener_slot_t {
  bool in_use;
  jh_lwip_tcp_listener_t listener;
};

tcp_socket_slot_t s_tcp_sockets[HAL_TCP_SOCKET_MAX_INSTANCES]{};
tcp_listener_slot_t s_tcp_listeners[HAL_TCP_LISTENER_MAX_INSTANCES]{};
#endif

#if defined(HAL_ENABLE_UDP)
struct udp_socket_slot_t {
  bool in_use;
  jh_lwip_udp_socket_t socket;
};

udp_socket_slot_t s_udp_sockets[HAL_UDP_SOCKET_MAX_INSTANCES]{};
#endif

void ensure_mutexes(void) {
  (void)jh_hal_mutex_create_once(&s_pool_mutex);
  (void)jh_hal_mutex_create_once(&s_lifecycle_mutex);
}

bool deadline_expired(uint32_t started, uint32_t timeout_ms) {
  return timeout_ms != HAL_NET_TIMEOUT_FOREVER &&
         (uint32_t)(hal_millis() - started) >= timeout_ms;
}

hal_status_t service_initialize(void);

hal_status_t status_from_cyw43(int status) {
  if (status == 0) {
    return HAL_OK;
  }
  if (status == -CYW43_ETIMEDOUT) {
    return HAL_ETIMEOUT;
  }
  if (status == -CYW43_EINVAL) {
    return HAL_EINVAL;
  }
  return HAL_EIO;
}

hal_status_t stack_enter(bool require_ipv4) {
  if (!s_initialized) {
    const hal_status_t status = service_initialize();
    if (status != HAL_OK) {
      return status;
    }
  }
  return jh_cyw43_radio_enter(JH_CYW43_RADIO_CLIENT_WIFI, require_ipv4);
}

void stack_leave(void) { (void)jh_cyw43_radio_leave(); }

void endpoint_from_ipv4(uint32_t address, uint16_t port,
                        hal_net_endpoint_t *out) {
  ip4_addr_t ipv4{};
  ipv4.addr = address;
  memset(out, 0, sizeof(*out));
  out->family = HAL_NET_AF_INET;
  out->addr_len = HAL_NET_IPV4_ADDR_LEN;
  out->addr[0] = ip4_addr1(&ipv4);
  out->addr[1] = ip4_addr2(&ipv4);
  out->addr[2] = ip4_addr3(&ipv4);
  out->addr[3] = ip4_addr4(&ipv4);
  out->port = port;
}

ip4_addr_t ipv4_from_endpoint(const hal_net_endpoint_t *endpoint) {
  ip4_addr_t address{};
  IP4_ADDR(&address, endpoint->addr[0], endpoint->addr[1], endpoint->addr[2],
           endpoint->addr[3]);
  return address;
}

hal_status_t validate_ipv4_endpoint(const hal_net_endpoint_t *endpoint,
                                    bool allow_unspecified) {
  const hal_status_t shape =
      jh_net_validate_endpoint_shape(endpoint, true, allow_unspecified);
  if (shape != HAL_OK) {
    return shape;
  }
  return endpoint->family == HAL_NET_AF_INET ? HAL_OK : HAL_EUNSUPPORTED;
}

hal_status_t service_initialize(void) {
  ensure_mutexes();
  hal_mutex_lock(s_lifecycle_mutex);
  if (s_initialized) {
    hal_mutex_unlock(s_lifecycle_mutex);
    return HAL_OK;
  }
  hal_status_t status = jh_cyw43_radio_acquire(JH_CYW43_RADIO_CLIENT_WIFI);
  if (status != HAL_OK) {
    hal_mutex_unlock(s_lifecycle_mutex);
    return status;
  }
  s_initialized = true;
  if (s_hostname[0] != '\0') {
    (void)jh_cyw43_hostname_apply(&cyw43_state.netif[CYW43_ITF_STA],
                                  s_hostname);
  }
  hal_mutex_unlock(s_lifecycle_mutex);
  return HAL_OK;
}

#if defined(HAL_ENABLE_TCP)
void close_all_tcp_tokens(void);
extern "C" void jh_network_facade_tcp_reset_all(void);
#endif
#if defined(HAL_ENABLE_UDP)
void close_all_udp_tokens(void);
extern "C" void jh_network_facade_udp_reset_all(void);
#endif

hal_status_t service_deinitialize(void) {
  ensure_mutexes();
  hal_mutex_lock(s_lifecycle_mutex);
  if (!s_initialized) {
    hal_mutex_unlock(s_lifecycle_mutex);
    return HAL_OK;
  }
#if defined(HAL_ENABLE_TCP)
  jh_network_facade_tcp_reset_all();
  close_all_tcp_tokens();
#endif
#if defined(HAL_ENABLE_UDP)
  jh_network_facade_udp_reset_all();
  close_all_udp_tokens();
#endif
  hal_status_t status = stack_enter(false);
  if (status == HAL_OK) {
    if (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) !=
        CYW43_LINK_DOWN) {
      status = jh_cyw43_lwip_leave();
    }
    stack_leave();
  }
  if (status == HAL_OK) {
    status = jh_cyw43_radio_release(JH_CYW43_RADIO_CLIENT_WIFI);
  }
  if (status == HAL_OK) {
    s_initialized = false;
    s_scan_count = 0u;
    s_scan_overflow = false;
  }
  hal_mutex_unlock(s_lifecycle_mutex);
  return status;
}

hal_status_t service_once(void) {
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  return jh_cyw43_radio_service(JH_CYW43_RADIO_CLIENT_WIFI);
}

hal_status_t wifi_set_mode(hal_wifi_mode_t mode);
hal_status_t wifi_disconnect(bool erase_credentials);

hal_status_t wifi_set_mode(hal_wifi_mode_t mode) {
  if (mode == HAL_WIFI_MODE_STA) {
    return service_initialize();
  }
  if (mode == HAL_WIFI_MODE_OFF) {
    return wifi_disconnect(false);
  }
  return mode == HAL_WIFI_MODE_AP || mode == HAL_WIFI_MODE_AP_STA
             ? HAL_EUNSUPPORTED
             : HAL_EINVAL;
}

hal_status_t wifi_disconnect(bool erase_credentials) {
  (void)erase_credentials;
  if (!s_initialized) {
    return HAL_OK;
  }
  hal_status_t status = stack_enter(false);
  if (status == HAL_OK) {
    status = cyw43_wifi_scan_active(&cyw43_state) ? HAL_EBUSY
                                                  : jh_cyw43_lwip_leave();
    stack_leave();
  }
  return status;
}

hal_status_t wifi_set_hostname(const char *hostname) {
  if (hostname == nullptr || hostname[0] == '\0') {
    return HAL_EINVAL;
  }
  const size_t length = strlen(hostname);
  if (length >= sizeof(s_hostname)) {
    return HAL_EOVERFLOW;
  }
  memcpy(s_hostname, hostname, length + 1u);
  if (!s_initialized) {
    return HAL_OK;
  }
  hal_status_t status = stack_enter(false);
  if (status == HAL_OK) {
    status =
        jh_cyw43_hostname_apply(&cyw43_state.netif[CYW43_ITF_STA], s_hostname);
    stack_leave();
  }
  return status;
}

hal_status_t wifi_join(const char *ssid, const char *password,
                       bool non_blocking, uint32_t timeout_ms) {
  if (ssid == nullptr || ssid[0] == '\0' || password == nullptr ||
      timeout_ms == 0u) {
    return HAL_EINVAL;
  }
  if (non_blocking) {
    return HAL_EUNSUPPORTED;
  }
  hal_status_t status = service_initialize();
  if (status != HAL_OK) {
    return status;
  }
  status = stack_enter(false);
  if (status != HAL_OK) {
    return status;
  }
  if (cyw43_wifi_scan_active(&cyw43_state)) {
    stack_leave();
    return HAL_EBUSY;
  }
  if (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_DOWN) {
    status = jh_cyw43_lwip_leave();
  }
  if (status == HAL_OK) {
    const uint32_t auth =
        password[0] == '\0' ? CYW43_AUTH_OPEN : CYW43_AUTH_WPA2_AES_PSK;
    status = jh_cyw43_lwip_join(ssid, password, auth, timeout_ms);
  }
  stack_leave();
  return status;
}

hal_status_t wifi_get_state(hal_wifi_state_t *out_state) {
  if (out_state == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_initialized) {
    *out_state = HAL_WIFI_STATE_OFF;
    return HAL_OK;
  }
  hal_status_t status = stack_enter(false);
  if (status != HAL_OK) {
    return status;
  }
  const int link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
  switch (link) {
  case CYW43_LINK_DOWN:
    *out_state = HAL_WIFI_STATE_IDLE;
    break;
  case CYW43_LINK_JOIN:
    *out_state = cyw43_state.wifi_join_state == 1
                     ? HAL_WIFI_STATE_CONNECTED_NO_IP
                     : HAL_WIFI_STATE_CONNECTING;
    break;
  case CYW43_LINK_NOIP:
    *out_state = HAL_WIFI_STATE_CONNECTED_NO_IP;
    break;
  case CYW43_LINK_UP:
    *out_state = HAL_WIFI_STATE_CONNECTED;
    break;
  case CYW43_LINK_NONET:
    *out_state = HAL_WIFI_STATE_NO_NETWORK;
    break;
  case CYW43_LINK_BADAUTH:
    *out_state = HAL_WIFI_STATE_AUTH_FAILED;
    break;
  default:
    *out_state = HAL_WIFI_STATE_FAILED;
    break;
  }
  stack_leave();
  return HAL_OK;
}

hal_status_t snapshot_address(bool dns_address,
                              hal_net_endpoint_t *out_address) {
  if (out_address == nullptr) {
    return HAL_EINVAL;
  }
  hal_status_t status = stack_enter(true);
  if (status != HAL_OK) {
    return status;
  }
  jh_cyw43_lwip_snapshot_t snapshot{};
  status = jh_cyw43_lwip_get_snapshot(&snapshot);
  const uint32_t address = dns_address ? snapshot.dns : snapshot.ipv4;
  if (status == HAL_OK && address == 0u) {
    status = HAL_ESTATE;
  }
  if (status == HAL_OK) {
    endpoint_from_ipv4(address, 0u, out_address);
  }
  stack_leave();
  return status;
}

hal_status_t wifi_get_local(hal_net_endpoint_t *out_address) {
  return snapshot_address(false, out_address);
}

hal_status_t wifi_get_dns(hal_net_endpoint_t *out_address) {
  return snapshot_address(true, out_address);
}

hal_status_t wifi_get_mac(uint8_t out_mac[HAL_WIFI_BSSID_LEN]) {
  if (out_mac == nullptr) {
    return HAL_EINVAL;
  }
  hal_status_t status = service_initialize();
  if (status == HAL_OK) {
    status = stack_enter(false);
  }
  if (status == HAL_OK) {
    status = status_from_cyw43(
        cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, out_mac));
    stack_leave();
  }
  return status;
}

hal_status_t wifi_get_rssi(int32_t *out_rssi) {
  if (out_rssi == nullptr) {
    return HAL_EINVAL;
  }
  hal_status_t status = stack_enter(true);
  if (status == HAL_OK) {
    status = status_from_cyw43(cyw43_wifi_get_rssi(&cyw43_state, out_rssi));
    stack_leave();
  }
  return status;
}

hal_status_t wifi_ping(const hal_net_endpoint_t *remote, uint32_t timeout_ms,
                       int *out_result) {
  if (out_result != nullptr) {
    *out_result = -1;
  }
  const hal_status_t endpoint_status = validate_ipv4_endpoint(remote, false);
  if (endpoint_status != HAL_OK || out_result == nullptr || timeout_ms == 0u) {
    return endpoint_status != HAL_OK ? endpoint_status : HAL_EINVAL;
  }
  const ip4_addr_t address = ipv4_from_endpoint(remote);
  hal_status_t status = stack_enter(true);
  if (status == HAL_OK) {
    uint32_t rtt_ms = 0u;
    status =
        jh_cyw43_lwip_ping_ipv4(address.addr, timeout_ms, out_result, &rtt_ms);
    stack_leave();
  }
  return status;
}

int scan_result_callback(void *, const cyw43_ev_scan_result_t *result) {
  return jh_cyw43_collect_scan_result(s_scan_results,
                                      HAL_CYW43_SCAN_RESULT_CAPACITY,
                                      &s_scan_count, &s_scan_overflow, result);
}

hal_status_t wifi_scan(uint32_t timeout_ms, int *out_count) {
  if (out_count == nullptr || timeout_ms == 0u) {
    return HAL_EINVAL;
  }
  *out_count = 0;
  hal_status_t status = service_initialize();
  if (status == HAL_OK) {
    status = stack_enter(false);
  }
  if (status != HAL_OK) {
    return status;
  }
  if (cyw43_wifi_scan_active(&cyw43_state)) {
    stack_leave();
    return HAL_EBUSY;
  }
  memset(s_scan_results, 0, sizeof(s_scan_results));
  s_scan_count = 0u;
  s_scan_overflow = false;
  cyw43_wifi_scan_options_t options{};
  status = status_from_cyw43(
      cyw43_wifi_scan(&cyw43_state, &options, nullptr, scan_result_callback));
  const uint32_t started = hal_millis();
  while (status == HAL_OK && cyw43_wifi_scan_active(&cyw43_state)) {
    status = jh_cyw43_lwip_service();
    if (status == HAL_OK && deadline_expired(started, timeout_ms)) {
      status = HAL_ETIMEOUT;
    }
    if (status == HAL_OK) {
      hal_delay_ms(1u);
    }
  }
  *out_count = (int)s_scan_count;
  stack_leave();
  return status == HAL_OK && s_scan_overflow ? HAL_EOVERFLOW : status;
}

hal_status_t wifi_get_scan_result(size_t index,
                                  hal_wifi_scan_result_t *out_result) {
  if (out_result == nullptr) {
    return HAL_EINVAL;
  }
  if (index >= s_scan_count) {
    return HAL_ENOENT;
  }
  *out_result = s_scan_results[index];
  return HAL_OK;
}

hal_status_t resolver_resolve(const char *hostname,
                              hal_net_family_t family_hint,
                              hal_net_endpoint_t *results, size_t capacity,
                              size_t *out_count) {
  if (out_count != nullptr) {
    *out_count = 0u;
  }
  if (hostname == nullptr || hostname[0] == '\0' || out_count == nullptr ||
      (capacity > 0u && results == nullptr)) {
    return HAL_EINVAL;
  }
  if (family_hint == HAL_NET_AF_INET6) {
    return HAL_EUNSUPPORTED;
  }
  if (family_hint != HAL_NET_AF_UNSPEC && family_hint != HAL_NET_AF_INET) {
    return HAL_EINVAL;
  }
  hal_status_t status = stack_enter(true);
  uint32_t address = 0u;
  if (status == HAL_OK) {
    status = jh_cyw43_lwip_resolve_ipv4(hostname, &address, kDnsTimeoutMs);
    stack_leave();
  }
  if (status != HAL_OK) {
    return status;
  }
  *out_count = 1u;
  if (capacity < 1u) {
    return HAL_EOVERFLOW;
  }
  endpoint_from_ipv4(address, 0u, &results[0]);
  return HAL_OK;
}

#if defined(HAL_ENABLE_TCP)
bool valid_tcp_socket(void *token) {
  for (auto &slot : s_tcp_sockets) {
    if (token == &slot && slot.in_use) {
      return true;
    }
  }
  return false;
}

bool valid_tcp_listener(void *token) {
  for (auto &slot : s_tcp_listeners) {
    if (token == &slot && slot.in_use) {
      return true;
    }
  }
  return false;
}

tcp_socket_slot_t *allocate_tcp_socket(void) {
  for (auto &slot : s_tcp_sockets) {
    if (!slot.in_use) {
      jh_lwip_tcp_socket_init(&slot.socket);
      slot.in_use = true;
      return &slot;
    }
  }
  return nullptr;
}

hal_status_t tcp_socket_open(void **out_socket) {
  if (out_socket == nullptr) {
    return HAL_EINVAL;
  }
  *out_socket = nullptr;
  ensure_mutexes();
  hal_mutex_lock(s_pool_mutex);
  *out_socket = allocate_tcp_socket();
  hal_mutex_unlock(s_pool_mutex);
  return *out_socket == nullptr ? HAL_ENOMEM : HAL_OK;
}

hal_status_t tcp_socket_connect(void *token, const hal_net_endpoint_t *remote,
                                uint32_t timeout_ms) {
  const hal_status_t endpoint_status = validate_ipv4_endpoint(remote, false);
  if (endpoint_status != HAL_OK) {
    return endpoint_status;
  }
  const ip4_addr_t address = ipv4_from_endpoint(remote);
  hal_mutex_lock(s_pool_mutex);
  if (!valid_tcp_socket(token)) {
    hal_mutex_unlock(s_pool_mutex);
    return HAL_EINVAL;
  }
  auto *slot = static_cast<tcp_socket_slot_t *>(token);
  hal_status_t status = stack_enter(true);
  if (status == HAL_OK) {
    jh_lwip_tcp_socket_close(&slot->socket);
    status = jh_lwip_tcp_socket_connect(&slot->socket, &address, remote->port);
    stack_leave();
  }
  hal_mutex_unlock(s_pool_mutex);
  if (status != HAL_OK) {
    return status;
  }

  const uint32_t started = hal_millis();
  for (;;) {
    hal_mutex_lock(s_pool_mutex);
    if (!valid_tcp_socket(token)) {
      hal_mutex_unlock(s_pool_mutex);
      return HAL_EINVAL;
    }
    status = stack_enter(false);
    if (status == HAL_OK) {
      status = jh_lwip_tcp_socket_connection_status(&slot->socket);
      stack_leave();
    }
    hal_mutex_unlock(s_pool_mutex);
    if (status != HAL_EAGAIN) {
      return status;
    }
    if (deadline_expired(started, timeout_ms)) {
      hal_mutex_lock(s_pool_mutex);
      if (valid_tcp_socket(token) && stack_enter(false) == HAL_OK) {
        jh_lwip_tcp_socket_close(&slot->socket);
        stack_leave();
      }
      hal_mutex_unlock(s_pool_mutex);
      return HAL_ETIMEOUT;
    }
    status = service_once();
    if (status != HAL_OK) {
      return status;
    }
    hal_idle();
    hal_delay_ms(1u);
  }
}

hal_status_t tcp_socket_send(void *token, const void *data, size_t length,
                             size_t *out_sent) {
  if (out_sent != nullptr) {
    *out_sent = 0u;
  }
  if (out_sent == nullptr || (length > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(s_pool_mutex);
  if (!valid_tcp_socket(token)) {
    hal_mutex_unlock(s_pool_mutex);
    return HAL_EINVAL;
  }
  auto *slot = static_cast<tcp_socket_slot_t *>(token);
  hal_status_t status = stack_enter(true);
  if (status == HAL_OK) {
    status = jh_lwip_tcp_socket_send(&slot->socket, data, length, out_sent);
    stack_leave();
  }
  hal_mutex_unlock(s_pool_mutex);
  return status;
}

hal_status_t tcp_socket_recv(void *token, void *buffer, size_t max_length,
                             uint32_t timeout_ms, size_t *out_received) {
  if (out_received != nullptr) {
    *out_received = 0u;
  }
  if (out_received == nullptr || (max_length > 0u && buffer == nullptr)) {
    return HAL_EINVAL;
  }
  const uint32_t started = hal_millis();
  for (;;) {
    hal_mutex_lock(s_pool_mutex);
    if (!valid_tcp_socket(token)) {
      hal_mutex_unlock(s_pool_mutex);
      return HAL_EINVAL;
    }
    auto *slot = static_cast<tcp_socket_slot_t *>(token);
    hal_status_t status = stack_enter(false);
    if (status == HAL_OK) {
      if (jh_lwip_tcp_socket_available(&slot->socket) > 0u) {
        status = jh_lwip_tcp_socket_receive(&slot->socket, buffer, max_length,
                                            out_received);
        stack_leave();
        hal_mutex_unlock(s_pool_mutex);
        return status;
      }
      const bool connected = jh_lwip_tcp_socket_is_connected(&slot->socket);
      stack_leave();
      if (!connected) {
        hal_mutex_unlock(s_pool_mutex);
        return HAL_OK;
      }
    }
    hal_mutex_unlock(s_pool_mutex);
    if (status != HAL_OK) {
      return status;
    }
    if (timeout_ms == 0u || deadline_expired(started, timeout_ms)) {
      return HAL_OK;
    }
    status = service_once();
    if (status != HAL_OK) {
      return status;
    }
    hal_idle();
    hal_delay_ms(1u);
  }
}

bool tcp_socket_can_recv(void *token) {
  hal_mutex_lock(s_pool_mutex);
  bool ready = false;
  if (valid_tcp_socket(token) && stack_enter(false) == HAL_OK) {
    ready = jh_lwip_tcp_socket_available(
                &static_cast<tcp_socket_slot_t *>(token)->socket) > 0u;
    stack_leave();
  }
  hal_mutex_unlock(s_pool_mutex);
  return ready;
}

bool tcp_socket_can_send(void *token) {
  hal_mutex_lock(s_pool_mutex);
  bool ready = false;
  if (valid_tcp_socket(token) && stack_enter(true) == HAL_OK) {
    ready = jh_lwip_tcp_socket_can_send(
        &static_cast<tcp_socket_slot_t *>(token)->socket);
    stack_leave();
  }
  hal_mutex_unlock(s_pool_mutex);
  return ready;
}

bool tcp_socket_is_connected(void *token) {
  hal_mutex_lock(s_pool_mutex);
  bool connected = false;
  if (valid_tcp_socket(token) && stack_enter(false) == HAL_OK) {
    connected = jh_lwip_tcp_socket_is_connected(
        &static_cast<tcp_socket_slot_t *>(token)->socket);
    stack_leave();
  }
  hal_mutex_unlock(s_pool_mutex);
  return connected;
}

void tcp_socket_shutdown(void *token) {
  hal_mutex_lock(s_pool_mutex);
  if (valid_tcp_socket(token) && stack_enter(false) == HAL_OK) {
    jh_lwip_tcp_socket_close(&static_cast<tcp_socket_slot_t *>(token)->socket);
    stack_leave();
  }
  hal_mutex_unlock(s_pool_mutex);
}

void tcp_socket_close(void *token) {
  hal_mutex_lock(s_pool_mutex);
  if (valid_tcp_socket(token)) {
    auto *slot = static_cast<tcp_socket_slot_t *>(token);
    if (stack_enter(false) == HAL_OK) {
      jh_lwip_tcp_socket_close(&slot->socket);
      stack_leave();
    }
    jh_lwip_tcp_socket_init(&slot->socket);
    slot->in_use = false;
  }
  hal_mutex_unlock(s_pool_mutex);
}

hal_status_t tcp_listener_open(void **out_listener) {
  if (out_listener == nullptr) {
    return HAL_EINVAL;
  }
  *out_listener = nullptr;
  hal_mutex_lock(s_pool_mutex);
  for (auto &slot : s_tcp_listeners) {
    if (!slot.in_use) {
      jh_lwip_tcp_listener_init(&slot.listener);
      slot.in_use = true;
      *out_listener = &slot;
      break;
    }
  }
  hal_mutex_unlock(s_pool_mutex);
  return *out_listener == nullptr ? HAL_ENOMEM : HAL_OK;
}

hal_status_t tcp_listener_bind(void *token, const hal_net_endpoint_t *local) {
  const hal_status_t endpoint_status = validate_ipv4_endpoint(local, true);
  if (endpoint_status != HAL_OK) {
    return endpoint_status;
  }
  const ip4_addr_t address = ipv4_from_endpoint(local);
  hal_mutex_lock(s_pool_mutex);
  if (!valid_tcp_listener(token)) {
    hal_mutex_unlock(s_pool_mutex);
    return HAL_EINVAL;
  }
  hal_status_t status = stack_enter(true);
  if (status == HAL_OK) {
    status = jh_lwip_tcp_listener_bind(
        &static_cast<tcp_listener_slot_t *>(token)->listener, &address,
        local->port);
    stack_leave();
  }
  hal_mutex_unlock(s_pool_mutex);
  return status;
}

hal_status_t tcp_listener_listen(void *token, uint8_t backlog) {
  if (backlog == 0u) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(s_pool_mutex);
  if (!valid_tcp_listener(token)) {
    hal_mutex_unlock(s_pool_mutex);
    return HAL_EINVAL;
  }
  hal_status_t status = stack_enter(true);
  if (status == HAL_OK) {
    status = jh_lwip_tcp_listener_listen(
        &static_cast<tcp_listener_slot_t *>(token)->listener, backlog);
    stack_leave();
  }
  hal_mutex_unlock(s_pool_mutex);
  return status;
}

hal_status_t tcp_listener_accept(void *token, hal_net_endpoint_t *remote,
                                 uint32_t timeout_ms, void **out_socket) {
  if (out_socket == nullptr) {
    return HAL_EINVAL;
  }
  *out_socket = nullptr;
  const uint32_t started = hal_millis();
  for (;;) {
    hal_mutex_lock(s_pool_mutex);
    if (!valid_tcp_listener(token)) {
      hal_mutex_unlock(s_pool_mutex);
      return HAL_EINVAL;
    }
    hal_status_t status = stack_enter(true);
    tcp_socket_slot_t *socket = nullptr;
    ip4_addr_t remote_address{};
    uint16_t remote_port = 0u;
    if (status == HAL_OK) {
      auto *listener = static_cast<tcp_listener_slot_t *>(token);
      if (!jh_lwip_tcp_listener_can_accept(&listener->listener)) {
        status = HAL_EAGAIN;
      } else {
        socket = allocate_tcp_socket();
        status = socket == nullptr ? HAL_ENOMEM
                                   : jh_lwip_tcp_listener_accept(
                                         &listener->listener, &socket->socket,
                                         &remote_address, &remote_port);
        if (status != HAL_OK && socket != nullptr) {
          socket->in_use = false;
          socket = nullptr;
        }
      }
      stack_leave();
    }
    hal_mutex_unlock(s_pool_mutex);
    if (status == HAL_OK && socket != nullptr) {
      if (remote != nullptr) {
        endpoint_from_ipv4(remote_address.addr, remote_port, remote);
      }
      *out_socket = socket;
      return HAL_OK;
    }
    if (status != HAL_EAGAIN) {
      return status;
    }
    if (timeout_ms == 0u || deadline_expired(started, timeout_ms)) {
      return HAL_EAGAIN;
    }
    status = service_once();
    if (status != HAL_OK) {
      return status;
    }
    hal_idle();
    hal_delay_ms(1u);
  }
}

bool tcp_listener_can_accept(void *token) {
  hal_mutex_lock(s_pool_mutex);
  bool ready = false;
  if (valid_tcp_listener(token) && stack_enter(true) == HAL_OK) {
    ready = jh_lwip_tcp_listener_can_accept(
        &static_cast<tcp_listener_slot_t *>(token)->listener);
    stack_leave();
  }
  hal_mutex_unlock(s_pool_mutex);
  return ready;
}

void tcp_listener_close(void *token) {
  hal_mutex_lock(s_pool_mutex);
  if (valid_tcp_listener(token)) {
    auto *slot = static_cast<tcp_listener_slot_t *>(token);
    if (stack_enter(false) == HAL_OK) {
      jh_lwip_tcp_listener_close(&slot->listener);
      stack_leave();
    }
    jh_lwip_tcp_listener_init(&slot->listener);
    slot->in_use = false;
  }
  hal_mutex_unlock(s_pool_mutex);
}

void close_all_tcp_tokens(void) {
  hal_mutex_lock(s_pool_mutex);
  if (stack_enter(false) == HAL_OK) {
    for (auto &slot : s_tcp_sockets) {
      if (slot.in_use) {
        jh_lwip_tcp_socket_close(&slot.socket);
        slot.in_use = false;
      }
    }
    for (auto &slot : s_tcp_listeners) {
      if (slot.in_use) {
        jh_lwip_tcp_listener_close(&slot.listener);
        slot.in_use = false;
      }
    }
    stack_leave();
  }
  hal_mutex_unlock(s_pool_mutex);
}

const jh_network_tcp_ops_t s_tcp_ops = {
    tcp_socket_open,         tcp_socket_connect,      tcp_socket_send,
    tcp_socket_recv,         tcp_socket_can_recv,     tcp_socket_can_send,
    tcp_socket_is_connected, tcp_socket_shutdown,     tcp_socket_close,
    tcp_listener_open,       tcp_listener_bind,       tcp_listener_listen,
    tcp_listener_accept,     tcp_listener_can_accept, tcp_listener_close,
};
#endif

#if defined(HAL_ENABLE_UDP)
bool valid_udp_socket(void *token) {
  for (auto &slot : s_udp_sockets) {
    if (token == &slot && slot.in_use) {
      return true;
    }
  }
  return false;
}

hal_status_t udp_socket_open(void **out_socket) {
  if (out_socket == nullptr) {
    return HAL_EINVAL;
  }
  *out_socket = nullptr;
  ensure_mutexes();
  hal_mutex_lock(s_pool_mutex);
  for (auto &slot : s_udp_sockets) {
    if (!slot.in_use) {
      jh_lwip_udp_socket_init(&slot.socket);
      slot.in_use = true;
      *out_socket = &slot;
      break;
    }
  }
  hal_mutex_unlock(s_pool_mutex);
  return *out_socket == nullptr ? HAL_ENOMEM : HAL_OK;
}

hal_status_t udp_socket_bind(void *token, const hal_net_endpoint_t *local) {
  const hal_status_t endpoint_status = validate_ipv4_endpoint(local, true);
  if (endpoint_status != HAL_OK) {
    return endpoint_status;
  }
  hal_mutex_lock(s_pool_mutex);
  if (!valid_udp_socket(token)) {
    hal_mutex_unlock(s_pool_mutex);
    return HAL_EINVAL;
  }
  hal_status_t status = stack_enter(true);
  if (status == HAL_OK) {
    status = jh_lwip_udp_socket_bind(
        &static_cast<udp_socket_slot_t *>(token)->socket, local->port);
    stack_leave();
  }
  hal_mutex_unlock(s_pool_mutex);
  return status;
}

hal_status_t udp_socket_sendto(void *token, const void *data, size_t length,
                               const hal_net_endpoint_t *remote,
                               size_t *out_sent) {
  if (out_sent != nullptr) {
    *out_sent = 0u;
  }
  const hal_status_t endpoint_status = validate_ipv4_endpoint(remote, false);
  if (endpoint_status != HAL_OK || out_sent == nullptr ||
      (length > 0u && data == nullptr)) {
    return endpoint_status != HAL_OK ? endpoint_status : HAL_EINVAL;
  }
  const ip4_addr_t address = ipv4_from_endpoint(remote);
  hal_mutex_lock(s_pool_mutex);
  if (!valid_udp_socket(token)) {
    hal_mutex_unlock(s_pool_mutex);
    return HAL_EINVAL;
  }
  hal_status_t status = stack_enter(true);
  if (status == HAL_OK) {
    status = jh_lwip_udp_socket_sendto(
        &static_cast<udp_socket_slot_t *>(token)->socket, data, length,
        &address, remote->port, out_sent);
    stack_leave();
  }
  hal_mutex_unlock(s_pool_mutex);
  return status;
}

hal_status_t udp_socket_recvfrom(void *token, void *buffer, size_t max_length,
                                 hal_net_endpoint_t *remote,
                                 uint32_t timeout_ms, size_t *out_received) {
  if (out_received != nullptr) {
    *out_received = 0u;
  }
  if (out_received == nullptr || (max_length > 0u && buffer == nullptr)) {
    return HAL_EINVAL;
  }
  const uint32_t started = hal_millis();
  for (;;) {
    hal_mutex_lock(s_pool_mutex);
    if (!valid_udp_socket(token)) {
      hal_mutex_unlock(s_pool_mutex);
      return HAL_EINVAL;
    }
    auto *slot = static_cast<udp_socket_slot_t *>(token);
    hal_status_t status = stack_enter(false);
    if (status == HAL_OK) {
      const int packet_size = jh_lwip_udp_socket_parse(&slot->socket);
      if (packet_size > 0 || jh_lwip_udp_socket_has_packet(&slot->socket)) {
        ip4_addr_t remote_address{};
        uint16_t remote_port = 0u;
        const bool has_remote = jh_lwip_udp_socket_get_last_remote(
            &slot->socket, &remote_address, &remote_port);
        status = jh_lwip_udp_socket_read(&slot->socket, buffer, max_length,
                                         true, out_received);
        if (status == HAL_OK && remote != nullptr && has_remote) {
          endpoint_from_ipv4(remote_address.addr, remote_port, remote);
        }
        stack_leave();
        hal_mutex_unlock(s_pool_mutex);
        return status;
      }
      stack_leave();
    }
    hal_mutex_unlock(s_pool_mutex);
    if (status != HAL_OK) {
      return status;
    }
    if (timeout_ms == 0u || deadline_expired(started, timeout_ms)) {
      return HAL_OK;
    }
    status = service_once();
    if (status != HAL_OK) {
      return status;
    }
    hal_idle();
    hal_delay_ms(1u);
  }
}

bool udp_socket_can_recv(void *token) {
  hal_mutex_lock(s_pool_mutex);
  bool ready = false;
  if (valid_udp_socket(token) && stack_enter(false) == HAL_OK) {
    ready = jh_lwip_udp_socket_has_packet(
        &static_cast<udp_socket_slot_t *>(token)->socket);
    stack_leave();
  }
  hal_mutex_unlock(s_pool_mutex);
  return ready;
}

bool udp_socket_can_send(void *token) {
  hal_mutex_lock(s_pool_mutex);
  bool ready = false;
  if (valid_udp_socket(token) && stack_enter(true) == HAL_OK) {
    ready = jh_lwip_udp_socket_can_send(
        &static_cast<udp_socket_slot_t *>(token)->socket);
    stack_leave();
  }
  hal_mutex_unlock(s_pool_mutex);
  return ready;
}

void udp_socket_close(void *token) {
  hal_mutex_lock(s_pool_mutex);
  if (valid_udp_socket(token)) {
    auto *slot = static_cast<udp_socket_slot_t *>(token);
    if (stack_enter(false) == HAL_OK) {
      jh_lwip_udp_socket_close(&slot->socket);
      stack_leave();
    }
    jh_lwip_udp_socket_init(&slot->socket);
    slot->in_use = false;
  }
  hal_mutex_unlock(s_pool_mutex);
}

void close_all_udp_tokens(void) {
  hal_mutex_lock(s_pool_mutex);
  if (stack_enter(false) == HAL_OK) {
    for (auto &slot : s_udp_sockets) {
      if (slot.in_use) {
        jh_lwip_udp_socket_close(&slot.socket);
        slot.in_use = false;
      }
    }
    stack_leave();
  }
  hal_mutex_unlock(s_pool_mutex);
}

const jh_network_udp_ops_t s_udp_ops = {
    udp_socket_open,     udp_socket_bind,     udp_socket_sendto,
    udp_socket_recvfrom, udp_socket_can_recv, udp_socket_can_send,
    udp_socket_close,
};
#endif

const jh_network_service_ops_t s_service_ops = {
    service_initialize, service_deinitialize, service_once,
    stack_enter,        stack_leave,
};

const jh_network_wifi_ops_t s_wifi_ops = {
    wifi_set_mode,  wifi_disconnect, wifi_set_hostname, wifi_join,
    wifi_get_state, wifi_get_local,  wifi_get_dns,      wifi_get_mac,
    wifi_get_rssi,  wifi_ping,       wifi_scan,         wifi_get_scan_result,
};

const jh_network_resolver_ops_t s_resolver_ops = {resolver_resolve};

} // namespace

extern "C" const jh_network_backend_descriptor_t *
jh_network_backend_selected(void) {
  static const jh_network_backend_descriptor_t backend = {
      JH_NETWORK_BACKEND_ABI_VERSION,
      "cyw43-stm32gspi-lwip",
      JH_NET_CAP_WIFI_STA | JH_NET_CAP_WIFI_SCAN | JH_NET_CAP_DNS |
          JH_NET_CAP_PING |
#if defined(HAL_ENABLE_TCP)
          JH_NET_CAP_TCP_CLIENT | JH_NET_CAP_TCP_LISTENER |
#endif
#if defined(HAL_ENABLE_UDP)
          JH_NET_CAP_UDP |
#endif
#if defined(JH_STM32G474_HW) &&                                                \
    (defined(HAL_ENABLE_TLS) || defined(HAL_ENABLE_WIREGUARD))
          JH_NET_CAP_SECURE_ENTROPY |
#endif
          JH_NET_CAP_IPV4 | JH_NET_CAP_HOST_STACK_L3 |
          JH_NET_CAP_VIRTUAL_NETIF_ROUTE | JH_NET_CAP_STACK_CONTEXT,
      JH_NETWORK_EXECUTION_POLL,
      &s_service_ops,
      &s_wifi_ops,
      &s_resolver_ops,
#if defined(HAL_ENABLE_TCP)
      &s_tcp_ops,
#else
      nullptr,
#endif
#if defined(HAL_ENABLE_UDP)
      &s_udp_ops,
#else
      nullptr,
#endif
  };
  return &backend;
}

#endif
#endif
