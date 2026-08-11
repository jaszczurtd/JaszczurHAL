#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_RP
#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_WIFI) && defined(HAL_NETWORK_BACKEND_CYW43)

#if HAL_BOARD_HAS_CYW43

#include "hal/core/hal_mutex_once.h"
#include "hal/network/cyw43/jh_cyw43_driver.h"
#include "hal/network/cyw43/jh_cyw43_lwip.h"
#include "hal/network/cyw43/jh_cyw43_radio.h"
#include "hal/network/cyw43/jh_cyw43_scan_results.h"
#include "hal/network/jh_cyw43_scan.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"
#include "rp2040_cyw43_platform.h"
#include "rp2040_cyw43_provider.h"

extern "C" {
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
}

#include <string.h>

#ifndef HAL_CYW43_SCAN_RESULT_CAPACITY
#define HAL_CYW43_SCAN_RESULT_CAPACITY 64u
#endif

namespace {

bool s_initialized;
uint32_t s_timeout_ms = 15000u;
hal_mutex_t s_operation_mutex;
bool s_operation_active;
bool s_scan_overflow;
size_t s_scan_count;
char s_hostname[64]{};
hal_wifi_scan_result_t s_scan_results[HAL_CYW43_SCAN_RESULT_CAPACITY]{};

constexpr hal_board_capabilities_t kCyw43Capabilities =
    HAL_BOARD_CAP_CYW43 | (HAL_BOARD_HAS_EXTERNAL_RADIO_FRONTEND
                               ? HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND
                               : 0u);

void ensure_mutex(void) { (void)jh_hal_mutex_create_once(&s_operation_mutex); }

bool operation_begin(void) {
  ensure_mutex();
  hal_mutex_lock(s_operation_mutex);
  if (s_operation_active) {
    hal_mutex_unlock(s_operation_mutex);
    return false;
  }
  s_operation_active = true;
  hal_mutex_unlock(s_operation_mutex);
  return true;
}

void operation_end(void) {
  hal_mutex_lock(s_operation_mutex);
  s_operation_active = false;
  hal_mutex_unlock(s_operation_mutex);
}

bool deadline_expired(uint32_t started, uint32_t timeout_ms) {
  return (uint32_t)(hal_millis() - started) >= timeout_ms;
}

void copy_ipv4(uint32_t address, uint8_t out[HAL_NET_IPV4_ADDR_LEN]) {
  ip4_addr_t value{};
  value.addr = address;
  out[0] = ip4_addr1(&value);
  out[1] = ip4_addr2(&value);
  out[2] = ip4_addr3(&value);
  out[3] = ip4_addr4(&value);
}

uint32_t ipv4_from_bytes(const uint8_t address[HAL_NET_IPV4_ADDR_LEN]) {
  ip4_addr_t value{};
  IP4_ADDR(&value, address[0], address[1], address[2], address[3]);
  return value.addr;
}

int scan_result_callback(void *, const cyw43_ev_scan_result_t *result) {
  return jh_cyw43_collect_scan_result(s_scan_results,
                                      HAL_CYW43_SCAN_RESULT_CAPACITY,
                                      &s_scan_count, &s_scan_overflow, result);
}

} // namespace

extern "C" struct netif *__getCYW43Netif() {
  return s_initialized ? &cyw43_state.netif[CYW43_ITF_STA] : nullptr;
}

hal_status_t jh_rp2040_cyw43_provider_init(void) {
  if (s_initialized) {
    return HAL_OK;
  }
  hal_status_t status = jh_cyw43_radio_acquire(JH_CYW43_RADIO_CLIENT_WIFI);
  if (status != HAL_OK) {
    return status;
  }
  s_initialized = true;
  if (s_hostname[0] != '\0') {
    netif_set_hostname(&cyw43_state.netif[CYW43_ITF_STA], s_hostname);
  }
  return HAL_OK;
}

hal_status_t jh_rp2040_cyw43_provider_deinit_for_baseline(void) {
  if (!s_initialized) {
    return HAL_OK;
  }
  if (!operation_begin()) {
    return HAL_EBUSY;
  }
  hal_status_t status = jh_cyw43_radio_enter(JH_CYW43_RADIO_CLIENT_WIFI, false);
  if (status == HAL_OK) {
    if (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) !=
        CYW43_LINK_DOWN) {
      status = jh_cyw43_lwip_leave();
    }
    (void)jh_cyw43_radio_leave();
  }
  if (status == HAL_OK) {
    status = jh_cyw43_radio_release(JH_CYW43_RADIO_CLIENT_WIFI);
  }
  if (status == HAL_OK) {
    s_initialized = false;
    s_scan_count = 0u;
    s_scan_overflow = false;
  }
  operation_end();
  return status;
}

hal_status_t jh_cyw43_provider_service(void) {
  const hal_status_t status =
      hal_board_require_capabilities(kCyw43Capabilities);
  return status == HAL_OK ? jh_cyw43_radio_service(JH_CYW43_RADIO_CLIENT_WIFI)
                          : status;
}

hal_status_t jh_rp2040_cyw43_provider_join(const char *ssid,
                                           const char *password,
                                           bool non_blocking,
                                           uint32_t timeout_ms) {
  if (ssid == nullptr || ssid[0] == '\0' || password == nullptr ||
      timeout_ms == 0u) {
    return HAL_EINVAL;
  }
  hal_status_t status = jh_rp2040_cyw43_provider_init();
  if (status != HAL_OK) {
    return status;
  }
  if (!operation_begin()) {
    return HAL_EBUSY;
  }
  status = jh_cyw43_radio_enter(JH_CYW43_RADIO_CLIENT_WIFI, false);
  if (status == HAL_OK) {
    if (cyw43_wifi_scan_active(&cyw43_state)) {
      status = HAL_EBUSY;
    } else {
      if (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) !=
          CYW43_LINK_DOWN) {
        status = jh_cyw43_lwip_leave();
      }
      if (status == HAL_OK) {
        const uint32_t auth =
            password[0] == '\0' ? CYW43_AUTH_OPEN : CYW43_AUTH_WPA2_AES_PSK;
        status = non_blocking
                     ? jh_cyw43_lwip_join_start(ssid, password, auth)
                     : jh_cyw43_lwip_join(ssid, password, auth, timeout_ms);
      }
    }
    (void)jh_cyw43_radio_leave();
  }
  operation_end();
  return status;
}

hal_status_t jh_rp2040_cyw43_provider_leave(void) {
  if (!s_initialized) {
    return HAL_OK;
  }
  if (!operation_begin()) {
    return HAL_EBUSY;
  }
  hal_status_t status = jh_cyw43_radio_enter(JH_CYW43_RADIO_CLIENT_WIFI, false);
  if (status == HAL_OK) {
    status = cyw43_wifi_scan_active(&cyw43_state) ? HAL_EBUSY
                                                  : jh_cyw43_lwip_leave();
    (void)jh_cyw43_radio_leave();
  }
  operation_end();
  return status;
}

hal_status_t
jh_rp2040_cyw43_provider_link_status(jh_cyw43_link_status_t *out_link_status) {
  if (out_link_status == nullptr) {
    return HAL_EINVAL;
  }
  hal_status_t status = hal_board_require_capabilities(kCyw43Capabilities);
  if (status != HAL_OK) {
    return status;
  }
  status = jh_cyw43_radio_enter(JH_CYW43_RADIO_CLIENT_WIFI, false);
  if (status != HAL_OK) {
    return status;
  }
  switch (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA)) {
  case CYW43_LINK_DOWN:
    *out_link_status = JH_CYW43_LINK_DOWN;
    break;
  case CYW43_LINK_JOIN:
    *out_link_status = cyw43_state.wifi_join_state == 1
                           ? JH_CYW43_LINK_NO_IP
                           : JH_CYW43_LINK_CONNECTING;
    break;
  case CYW43_LINK_NOIP:
    *out_link_status = JH_CYW43_LINK_NO_IP;
    break;
  case CYW43_LINK_UP:
    *out_link_status = JH_CYW43_LINK_JOINED;
    break;
  case CYW43_LINK_NONET:
    *out_link_status = JH_CYW43_LINK_NO_NETWORK;
    break;
  case CYW43_LINK_BADAUTH:
    *out_link_status = JH_CYW43_LINK_BAD_AUTH;
    break;
  case CYW43_LINK_FAIL:
    *out_link_status = JH_CYW43_LINK_FAILED;
    break;
  default:
    *out_link_status = JH_CYW43_LINK_UNKNOWN;
    break;
  }
  (void)jh_cyw43_radio_leave();
  return HAL_OK;
}

hal_status_t jh_rp2040_cyw43_provider_get_mac(uint8_t mac[HAL_WIFI_BSSID_LEN]) {
  if (mac == nullptr) {
    return HAL_EINVAL;
  }
  hal_status_t status = hal_board_require_capabilities(kCyw43Capabilities);
  if (status == HAL_OK) {
    status = jh_cyw43_radio_enter(JH_CYW43_RADIO_CLIENT_WIFI, false);
  }
  if (status == HAL_OK) {
    status = jh_rp2040_cyw43_platform_status(
        cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac));
    (void)jh_cyw43_radio_leave();
  }
  return status;
}

hal_status_t jh_rp2040_cyw43_provider_get_rssi(int32_t *out_rssi) {
  if (out_rssi == nullptr) {
    return HAL_EINVAL;
  }
  hal_status_t status = hal_board_require_capabilities(kCyw43Capabilities);
  if (status == HAL_OK) {
    status = jh_cyw43_radio_enter(JH_CYW43_RADIO_CLIENT_WIFI, true);
  }
  if (status == HAL_OK) {
    status = jh_rp2040_cyw43_platform_status(
        cyw43_wifi_get_rssi(&cyw43_state, out_rssi));
    (void)jh_cyw43_radio_leave();
  }
  return status;
}

hal_status_t jh_rp2040_cyw43_provider_set_hostname(const char *hostname) {
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
  const hal_status_t status =
      jh_cyw43_radio_enter(JH_CYW43_RADIO_CLIENT_WIFI, false);
  if (status == HAL_OK) {
    netif_set_hostname(&cyw43_state.netif[CYW43_ITF_STA], s_hostname);
    (void)jh_cyw43_radio_leave();
  }
  return status;
}

hal_status_t jh_rp2040_cyw43_provider_lwip_begin(bool require_ipv4) {
  const hal_status_t status =
      hal_board_require_capabilities(kCyw43Capabilities);
  return status == HAL_OK
             ? jh_cyw43_radio_enter(JH_CYW43_RADIO_CLIENT_WIFI, require_ipv4)
             : status;
}

void jh_rp2040_cyw43_provider_lwip_end(void) { (void)jh_cyw43_radio_leave(); }

hal_status_t jh_rp2040_cyw43_provider_get_local_ipv4(uint8_t out_address[4]) {
  if (out_address == nullptr) {
    return HAL_EINVAL;
  }
  hal_status_t status = jh_rp2040_cyw43_provider_lwip_begin(true);
  if (status == HAL_OK) {
    jh_cyw43_lwip_snapshot_t snapshot{};
    status = jh_cyw43_lwip_get_snapshot(&snapshot);
    if (status == HAL_OK && snapshot.ipv4 == 0u) {
      status = HAL_ESTATE;
    }
    if (status == HAL_OK) {
      copy_ipv4(snapshot.ipv4, out_address);
    }
    jh_rp2040_cyw43_provider_lwip_end();
  }
  return status;
}

hal_status_t jh_rp2040_cyw43_provider_get_dns_ipv4(uint8_t out_address[4]) {
  if (out_address == nullptr) {
    return HAL_EINVAL;
  }
  hal_status_t status = jh_rp2040_cyw43_provider_lwip_begin(true);
  if (status == HAL_OK) {
    jh_cyw43_lwip_snapshot_t snapshot{};
    status = jh_cyw43_lwip_get_snapshot(&snapshot);
    if (status == HAL_OK && snapshot.dns == 0u) {
      status = HAL_ESTATE;
    }
    if (status == HAL_OK) {
      copy_ipv4(snapshot.dns, out_address);
    }
    jh_rp2040_cyw43_provider_lwip_end();
  }
  return status;
}

void jh_rp2040_cyw43_provider_set_timeout_ms(uint32_t timeout_ms) {
  s_timeout_ms = timeout_ms;
}

hal_status_t jh_rp2040_cyw43_provider_resolve_ipv4(
    const char *hostname, uint8_t out_address[HAL_NET_IPV4_ADDR_LEN]) {
  if (hostname == nullptr || hostname[0] == '\0' || out_address == nullptr ||
      s_timeout_ms == 0u) {
    return HAL_EINVAL;
  }
  hal_status_t status = jh_rp2040_cyw43_provider_lwip_begin(true);
  if (status == HAL_OK) {
    uint32_t address = 0u;
    status = jh_cyw43_lwip_resolve_ipv4(hostname, &address, s_timeout_ms);
    if (status == HAL_OK) {
      copy_ipv4(address, out_address);
    }
    jh_rp2040_cyw43_provider_lwip_end();
  }
  return status;
}

hal_status_t
jh_rp2040_cyw43_provider_ping_ipv4(const uint8_t address[HAL_NET_IPV4_ADDR_LEN],
                                   uint32_t timeout_ms, int *out_result) {
  if (address == nullptr || out_result == nullptr || timeout_ms == 0u) {
    return HAL_EINVAL;
  }
  *out_result = -1;
  hal_status_t status = jh_rp2040_cyw43_provider_lwip_begin(true);
  if (status == HAL_OK) {
    uint32_t rtt_ms = 0u;
    status = jh_cyw43_lwip_ping_ipv4(ipv4_from_bytes(address), timeout_ms,
                                     out_result, &rtt_ms);
    jh_rp2040_cyw43_provider_lwip_end();
  }
  return status;
}

hal_status_t jh_rp2040_cyw43_provider_scan(uint32_t timeout_ms,
                                           int *out_count) {
  if (out_count == nullptr || timeout_ms == 0u) {
    return HAL_EINVAL;
  }
  *out_count = 0;
  hal_status_t status = hal_board_require_capabilities(kCyw43Capabilities);
  if (status != HAL_OK) {
    return status;
  }
  if (!operation_begin()) {
    return HAL_EBUSY;
  }
  bool entered = false;
  status = jh_cyw43_radio_enter(JH_CYW43_RADIO_CLIENT_WIFI, false);
  entered = status == HAL_OK;
  if (status == HAL_OK && cyw43_wifi_scan_active(&cyw43_state)) {
    status = HAL_EBUSY;
  }
  if (status == HAL_OK) {
    memset(s_scan_results, 0, sizeof(s_scan_results));
    s_scan_count = 0u;
    s_scan_overflow = false;
    cyw43_wifi_scan_options_t options{};
    status = jh_rp2040_cyw43_platform_status(
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
    if (status == HAL_OK && s_scan_overflow) {
      status = HAL_EOVERFLOW;
    }
  }
  if (entered) {
    (void)jh_cyw43_radio_leave();
  }
  operation_end();
  return status;
}

hal_status_t
jh_rp2040_cyw43_provider_get_scan_result(size_t index,
                                         hal_wifi_scan_result_t *out) {
  if (out == nullptr) {
    return HAL_EINVAL;
  }
  if (index >= s_scan_count) {
    return HAL_ENOENT;
  }
  *out = s_scan_results[index];
  return HAL_OK;
}

#else

#include "hal/network/jh_cyw43_provider.h"

struct netif;
extern "C" struct netif *__getCYW43Netif(void) { return nullptr; }

hal_status_t jh_cyw43_provider_init(void) { return HAL_EUNSUPPORTED; }
hal_status_t jh_cyw43_provider_deinit(void) { return HAL_EUNSUPPORTED; }
hal_status_t jh_cyw43_provider_join(const char *, const char *, bool,
                                    uint32_t) {
  return HAL_EUNSUPPORTED;
}
hal_status_t jh_cyw43_provider_leave(void) { return HAL_EUNSUPPORTED; }
hal_status_t
jh_cyw43_provider_link_status(jh_cyw43_link_status_t *out_link_status) {
  if (out_link_status == nullptr) {
    return HAL_EINVAL;
  }
  *out_link_status = JH_CYW43_LINK_UNKNOWN;
  return HAL_EUNSUPPORTED;
}
hal_status_t jh_cyw43_provider_get_mac(uint8_t mac[HAL_WIFI_BSSID_LEN]) {
  return mac == nullptr ? HAL_EINVAL : HAL_EUNSUPPORTED;
}
hal_status_t jh_cyw43_provider_get_rssi(int32_t *out_rssi) {
  return out_rssi == nullptr ? HAL_EINVAL : HAL_EUNSUPPORTED;
}
hal_status_t jh_cyw43_provider_set_hostname(const char *hostname) {
  return hostname == nullptr || hostname[0] == '\0' ? HAL_EINVAL
                                                    : HAL_EUNSUPPORTED;
}
hal_status_t jh_cyw43_provider_stack_enter(bool) { return HAL_EUNSUPPORTED; }
void jh_cyw43_provider_stack_leave(void) {}
hal_status_t jh_cyw43_provider_service(void) { return HAL_EUNSUPPORTED; }
hal_status_t jh_cyw43_provider_get_local_ipv4(uint8_t out_address[4]) {
  return out_address == nullptr ? HAL_EINVAL : HAL_EUNSUPPORTED;
}
hal_status_t jh_cyw43_provider_get_dns_ipv4(uint8_t out_address[4]) {
  return out_address == nullptr ? HAL_EINVAL : HAL_EUNSUPPORTED;
}
void jh_cyw43_provider_set_timeout_ms(uint32_t) {}
hal_status_t
jh_cyw43_provider_resolve_ipv4(const char *hostname,
                               uint8_t out_address[HAL_NET_IPV4_ADDR_LEN]) {
  return hostname == nullptr || hostname[0] == '\0' || out_address == nullptr
             ? HAL_EINVAL
             : HAL_EUNSUPPORTED;
}
hal_status_t
jh_cyw43_provider_ping_ipv4(const uint8_t address[HAL_NET_IPV4_ADDR_LEN],
                            uint32_t timeout_ms, int *out_result) {
  return address == nullptr || timeout_ms == 0u || out_result == nullptr
             ? HAL_EINVAL
             : HAL_EUNSUPPORTED;
}
hal_status_t jh_cyw43_provider_scan(uint32_t timeout_ms, int *out_count) {
  return timeout_ms == 0u || out_count == nullptr ? HAL_EINVAL
                                                  : HAL_EUNSUPPORTED;
}
hal_status_t jh_cyw43_provider_get_scan_result(size_t,
                                               hal_wifi_scan_result_t *out) {
  return out == nullptr ? HAL_EINVAL : HAL_EUNSUPPORTED;
}

#endif
#endif
#endif
