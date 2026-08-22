#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_WIFI) && (defined(HAL_NETWORK_BACKEND_CYW43) ||         \
                                 defined(HAL_NETWORK_BACKEND_ESP_IDF))

#include "hal/core/hal_mutex_once.h"
#include "hal/network/hal_net.h"
#include "hal/network/hal_wifi.h"
#include "hal/network/jh_network_backend.h"
#include "hal/network/jh_network_runtime.h"
#if defined(HAL_ENABLE_WIREGUARD)
#include "hal/network/wireguard/hal_wireguard_internal.h"
#endif
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_sync.h"

#include <stdio.h>

static hal_mutex_t s_wifi_mutex = NULL;
static uint32_t s_timeout_ms = 15000u;
static bool s_operation_running = false;
static int s_scan_count = 0;

#if defined(HAL_ENABLE_TCP)
extern "C" void jh_network_facade_tcp_reset_all(void);
#endif
#if defined(HAL_ENABLE_UDP)
extern "C" void jh_network_facade_udp_reset_all(void);
#endif

static void reset_transport_handles(void) {
#if defined(HAL_ENABLE_TCP)
  jh_network_facade_tcp_reset_all();
#endif
#if defined(HAL_ENABLE_UDP)
  jh_network_facade_udp_reset_all();
#endif
}

static hal_status_t quiesce_persistent_services(void) {
#if defined(HAL_ENABLE_WIREGUARD)
  // WireGuard owns a raw lwIP netif and UDP PCB rather than a facade socket.
  // It must be removed while the underlay is still alive. If entering the
  // stack context fails, refuse the underlay transition without invalidating
  // any transport handles.
  return jh_hal_wireguard_end_provider();
#else
  return HAL_OK;
#endif
}

static hal_status_t ensure_mutex(void) {
  return jh_hal_mutex_create_once(&s_wifi_mutex) != nullptr ? HAL_OK
                                                            : HAL_ENOMEM;
}

static const jh_network_wifi_ops_t *wifi_ops(void) {
  const jh_network_backend_descriptor_t *backend =
      jh_network_backend_selected();
  return jh_network_backend_validate(backend, JH_NET_CAP_WIFI_STA) == HAL_OK
             ? backend->wifi
             : nullptr;
}

static hal_status_t begin_operation(uint32_t *out_timeout_ms) {
  const hal_status_t mutex_status = ensure_mutex();
  if (mutex_status != HAL_OK) {
    return mutex_status;
  }
  hal_mutex_lock(s_wifi_mutex);
  if (s_operation_running) {
    hal_mutex_unlock(s_wifi_mutex);
    return HAL_EBUSY;
  }
  s_operation_running = true;
  if (out_timeout_ms != nullptr) {
    *out_timeout_ms = s_timeout_ms;
  }
  hal_mutex_unlock(s_wifi_mutex);
  return HAL_OK;
}

static void end_operation(void) {
  hal_mutex_lock(s_wifi_mutex);
  s_operation_running = false;
  hal_mutex_unlock(s_wifi_mutex);
}

static hal_status_t format_ipv4(const hal_net_endpoint_t *endpoint, char *out,
                                size_t out_size) {
  if (out == nullptr || out_size == 0u || endpoint == nullptr) {
    return HAL_EINVAL;
  }
  if (endpoint->family != HAL_NET_AF_INET ||
      endpoint->addr_len != HAL_NET_IPV4_ADDR_LEN) {
    return HAL_EUNSUPPORTED;
  }
  const int written =
      snprintf(out, out_size, "%u.%u.%u.%u", (unsigned)endpoint->addr[0],
               (unsigned)endpoint->addr[1], (unsigned)endpoint->addr[2],
               (unsigned)endpoint->addr[3]);
  return written >= 0 && (size_t)written < out_size ? HAL_OK : HAL_EOVERFLOW;
}

hal_status_t hal_wifi_set_mode_ex(hal_wifi_mode_t mode) {
  if (mode < HAL_WIFI_MODE_OFF || mode > HAL_WIFI_MODE_AP_STA) {
    return HAL_EINVAL;
  }
  const hal_status_t hardware_status = jh_network_require_hardware();
  if (hardware_status != HAL_OK) {
    return hardware_status;
  }
  const jh_network_wifi_ops_t *ops = wifi_ops();
  if (ops == nullptr || ops->set_mode == nullptr) {
    return HAL_EUNSUPPORTED;
  }
  const hal_status_t start = begin_operation(nullptr);
  if (start != HAL_OK) {
    return start;
  }
  if (mode == HAL_WIFI_MODE_OFF) {
    const hal_status_t quiesce_status = quiesce_persistent_services();
    if (quiesce_status != HAL_OK) {
      end_operation();
      return quiesce_status;
    }
  }
  const hal_status_t status = ops->set_mode(mode);
  if (status == HAL_OK && mode == HAL_WIFI_MODE_OFF) {
    reset_transport_handles();
  }
  end_operation();
  return status;
}

bool hal_wifi_set_mode(hal_wifi_mode_t mode) {
  return hal_status_to_bool(hal_wifi_set_mode_ex(mode));
}

hal_status_t hal_wifi_disconnect_ex(bool erase_credentials) {
  const hal_status_t hardware_status = jh_network_require_hardware();
  if (hardware_status != HAL_OK) {
    return hardware_status;
  }
  const jh_network_wifi_ops_t *ops = wifi_ops();
  if (ops == nullptr || ops->disconnect == nullptr) {
    return HAL_EUNSUPPORTED;
  }
  const hal_status_t start = begin_operation(nullptr);
  if (start != HAL_OK) {
    return start;
  }
  const hal_status_t quiesce_status = quiesce_persistent_services();
  if (quiesce_status != HAL_OK) {
    end_operation();
    return quiesce_status;
  }
  const hal_status_t status = ops->disconnect(erase_credentials);
  if (status == HAL_OK) {
    reset_transport_handles();
  }
  end_operation();
  return status;
}

bool hal_wifi_disconnect(bool erase_credentials) {
  return hal_status_to_bool(hal_wifi_disconnect_ex(erase_credentials));
}

hal_status_t hal_wifi_set_hostname_ex(const char *hostname) {
  if (hostname == nullptr || hostname[0] == '\0') {
    return HAL_EINVAL;
  }
  const hal_status_t hardware_status = jh_network_require_hardware();
  if (hardware_status != HAL_OK) {
    return hardware_status;
  }
  const jh_network_wifi_ops_t *ops = wifi_ops();
  return ops != nullptr && ops->set_hostname != nullptr
             ? ops->set_hostname(hostname)
             : HAL_EUNSUPPORTED;
}

bool hal_wifi_set_hostname(const char *hostname) {
  return hal_status_to_bool(hal_wifi_set_hostname_ex(hostname));
}

hal_status_t hal_wifi_begin_station_ex(const char *ssid, const char *password,
                                       bool non_blocking) {
  if (ssid == nullptr || ssid[0] == '\0' || password == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t hardware_status = jh_network_require_hardware();
  if (hardware_status != HAL_OK) {
    return hardware_status;
  }
  const jh_network_wifi_ops_t *ops = wifi_ops();
  if (ops == nullptr || ops->join == nullptr) {
    return HAL_EUNSUPPORTED;
  }
  uint32_t timeout_ms = 0u;
  const hal_status_t start = begin_operation(&timeout_ms);
  if (start != HAL_OK) {
    return start;
  }
  const hal_status_t quiesce_status = quiesce_persistent_services();
  if (quiesce_status != HAL_OK) {
    end_operation();
    return quiesce_status;
  }
  // A join attempt replaces the station underlay even when the blocking
  // result is authentication failure, no network, or timeout. Invalidate
  // transports before handing control to the backend so no facade handle can
  // survive a partially completed reassociation.
  reset_transport_handles();
  const hal_status_t status =
      ops->join(ssid, password, non_blocking, timeout_ms);
  end_operation();
  return status;
}

bool hal_wifi_begin_station(const char *ssid, const char *password,
                            bool non_blocking) {
  return hal_status_to_bool(
      hal_wifi_begin_station_ex(ssid, password, non_blocking));
}

hal_status_t hal_wifi_set_timeout_ms_ex(uint32_t timeout_ms) {
  const hal_status_t hardware_status = jh_network_require_hardware();
  if (hardware_status != HAL_OK) {
    return hardware_status;
  }
  const hal_status_t mutex_status = ensure_mutex();
  if (mutex_status != HAL_OK) {
    return mutex_status;
  }
  hal_mutex_lock(s_wifi_mutex);
  s_timeout_ms = timeout_ms;
  hal_mutex_unlock(s_wifi_mutex);
  return HAL_OK;
}

bool hal_wifi_set_timeout_ms(uint32_t timeout_ms) {
  return hal_status_to_bool(hal_wifi_set_timeout_ms_ex(timeout_ms));
}

hal_status_t hal_wifi_get_state_ex(hal_wifi_state_t *out_state) {
  if (out_state == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  const jh_network_wifi_ops_t *ops = wifi_ops();
  return ops != nullptr && ops->get_state != nullptr ? ops->get_state(out_state)
                                                     : HAL_EUNSUPPORTED;
}

bool hal_wifi_is_connected(void) {
  hal_wifi_state_t state = HAL_WIFI_STATE_FAILED;
  return hal_wifi_get_state_ex(&state) == HAL_OK &&
         state == HAL_WIFI_STATE_CONNECTED;
}

int hal_wifi_status(void) {
  hal_wifi_state_t state = HAL_WIFI_STATE_FAILED;
  if (hal_wifi_get_state_ex(&state) != HAL_OK) {
    return 255;
  }
  switch (state) {
  case HAL_WIFI_STATE_CONNECTED:
    return 3;
  case HAL_WIFI_STATE_NO_NETWORK:
    return 1;
  case HAL_WIFI_STATE_AUTH_FAILED:
  case HAL_WIFI_STATE_FAILED:
    return 4;
  case HAL_WIFI_STATE_CONNECTED_NO_IP:
    return 6;
  case HAL_WIFI_STATE_OFF:
  case HAL_WIFI_STATE_IDLE:
  case HAL_WIFI_STATE_CONNECTING:
  default:
    return 0;
  }
}

bool hal_wifi_has_local_ip(void) {
  if (jh_network_require_ready() != HAL_OK) {
    return false;
  }
  hal_net_endpoint_t address = {};
  const jh_network_wifi_ops_t *ops = wifi_ops();
  return ops != nullptr && ops->get_local_address != nullptr &&
         ops->get_local_address(&address) == HAL_OK &&
         (address.addr[0] | address.addr[1] | address.addr[2] |
          address.addr[3]) != 0u;
}

int32_t hal_wifi_rssi(void) {
  if (jh_network_require_ready() != HAL_OK) {
    return 0;
  }
  int32_t rssi = 0;
  const jh_network_wifi_ops_t *ops = wifi_ops();
  return ops != nullptr && ops->get_rssi != nullptr &&
                 ops->get_rssi(&rssi) == HAL_OK
             ? rssi
             : 0;
}

int hal_wifi_get_strength(void) {
  const int32_t rssi = hal_wifi_rssi();
  if (rssi >= 0)
    return 0;
  if (rssi >= -50)
    return 5;
  if (rssi >= -60)
    return 4;
  if (rssi >= -70)
    return 3;
  if (rssi >= -80)
    return 2;
  if (rssi >= -90)
    return 1;
  return 0;
}

hal_status_t hal_wifi_get_local_ip_ex(char *out, size_t out_size) {
  if (out == nullptr || out_size == 0u) {
    return HAL_EINVAL;
  }
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  hal_net_endpoint_t address = {};
  const jh_network_wifi_ops_t *ops = wifi_ops();
  if (ops == nullptr || ops->get_local_address == nullptr) {
    return HAL_EUNSUPPORTED;
  }
  const hal_status_t status = ops->get_local_address(&address);
  return status == HAL_OK ? format_ipv4(&address, out, out_size) : status;
}

bool hal_wifi_get_local_ip(char *out, size_t out_size) {
  return hal_status_to_bool(hal_wifi_get_local_ip_ex(out, out_size));
}

hal_status_t hal_wifi_get_dns_ip_ex(char *out, size_t out_size) {
  if (out == nullptr || out_size == 0u) {
    return HAL_EINVAL;
  }
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  hal_net_endpoint_t address = {};
  const jh_network_wifi_ops_t *ops = wifi_ops();
  if (ops == nullptr || ops->get_dns_address == nullptr) {
    return HAL_EUNSUPPORTED;
  }
  const hal_status_t status = ops->get_dns_address(&address);
  return status == HAL_OK ? format_ipv4(&address, out, out_size) : status;
}

bool hal_wifi_get_dns_ip(char *out, size_t out_size) {
  return hal_status_to_bool(hal_wifi_get_dns_ip_ex(out, out_size));
}

hal_status_t hal_wifi_get_mac_ex(char *out, size_t out_size) {
  if (out == nullptr || out_size == 0u) {
    return HAL_EINVAL;
  }
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  uint8_t mac[HAL_WIFI_BSSID_LEN] = {};
  const jh_network_wifi_ops_t *ops = wifi_ops();
  if (ops == nullptr || ops->get_mac == nullptr) {
    return HAL_EUNSUPPORTED;
  }
  const hal_status_t status = ops->get_mac(mac);
  if (status != HAL_OK) {
    return status;
  }
  const int written =
      snprintf(out, out_size, "%02X:%02X:%02X:%02X:%02X:%02X", (unsigned)mac[0],
               (unsigned)mac[1], (unsigned)mac[2], (unsigned)mac[3],
               (unsigned)mac[4], (unsigned)mac[5]);
  return written >= 0 && (size_t)written < out_size ? HAL_OK : HAL_EOVERFLOW;
}

bool hal_wifi_get_mac(char *out, size_t out_size) {
  return hal_status_to_bool(hal_wifi_get_mac_ex(out, out_size));
}

hal_status_t hal_wifi_ping_status_ex(const char *host_or_ip,
                                     uint32_t timeout_ms, int *out_result) {
  if (out_result != nullptr) {
    *out_result = -1;
  }
  if (host_or_ip == nullptr || host_or_ip[0] == '\0' || out_result == nullptr ||
      timeout_ms == 0u) {
    return HAL_EINVAL;
  }
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  hal_net_endpoint_t remote = {};
  size_t count = 0u;
  hal_status_t status =
      hal_net_resolve_ex(host_or_ip, HAL_NET_AF_INET, &remote, 1u, &count);
  const jh_network_wifi_ops_t *ops = wifi_ops();
  if (status == HAL_OK) {
    status = ops != nullptr && ops->ping != nullptr
                 ? ops->ping(&remote, timeout_ms, out_result)
                 : HAL_EUNSUPPORTED;
  }
  return status;
}

int hal_wifi_ping_ex(const char *host_or_ip, uint32_t timeout_ms) {
  int result = -1;
  (void)hal_wifi_ping_status_ex(host_or_ip, timeout_ms, &result);
  return result;
}

int hal_wifi_ping(const char *host_or_ip) {
  if (ensure_mutex() != HAL_OK) {
    return -1;
  }
  hal_mutex_lock(s_wifi_mutex);
  const uint32_t timeout_ms = s_timeout_ms;
  hal_mutex_unlock(s_wifi_mutex);
  return hal_wifi_ping_ex(host_or_ip, timeout_ms);
}

hal_status_t hal_wifi_scan_networks_ex(int *out_count) {
  if (out_count == nullptr) {
    return HAL_EINVAL;
  }
  *out_count = 0;
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  const jh_network_wifi_ops_t *ops = wifi_ops();
  if (ops == nullptr || ops->scan == nullptr) {
    return HAL_EUNSUPPORTED;
  }
  uint32_t timeout_ms = 0u;
  const hal_status_t start = begin_operation(&timeout_ms);
  if (start != HAL_OK) {
    return start;
  }
  const hal_status_t status = ops->scan(timeout_ms, &s_scan_count);
  *out_count = s_scan_count;
  end_operation();
  return status;
}

int hal_wifi_scan_networks(void) {
  int count = -1;
  (void)hal_wifi_scan_networks_ex(&count);
  return count;
}

hal_status_t hal_wifi_get_scan_result_ex(size_t index,
                                         hal_wifi_scan_result_t *out) {
  if (out == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t runtime_status = jh_network_require_ready();
  if (runtime_status != HAL_OK) {
    return runtime_status;
  }
  const jh_network_wifi_ops_t *ops = wifi_ops();
  return ops != nullptr && ops->get_scan_result != nullptr
             ? ops->get_scan_result(index, out)
             : HAL_EUNSUPPORTED;
}

bool hal_wifi_get_scan_result(size_t index, hal_wifi_scan_result_t *out) {
  return hal_status_to_bool(hal_wifi_get_scan_result_ex(index, out));
}

const char *hal_wifi_encryption_to_string(hal_wifi_encryption_t encryption) {
  switch (encryption) {
  case HAL_WIFI_ENC_NONE:
    return "NONE";
  case HAL_WIFI_ENC_WPA:
    return "WPA";
  case HAL_WIFI_ENC_WPA2:
    return "WPA2";
  case HAL_WIFI_ENC_AUTO:
    return "AUTO";
  default:
    return "UNKN";
  }
}

#endif
