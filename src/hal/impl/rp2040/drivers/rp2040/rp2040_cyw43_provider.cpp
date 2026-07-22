#include "../../../../hal_target.h"

#if HAL_TARGET_IS_RP2040
#include "../../../../hal_config.h"

#if defined(HAL_ENABLE_WIFI) && defined(HAL_NETWORK_BACKEND_CYW43)

#include "../../../../impl/shared/drivers/cyw43-driver/jh_cyw43_driver.h"
#include "../../../../impl/shared/hal_mutex_once.h"
#include "../../../../impl/shared/network/jh_cyw43_scan.h"
#include "../../../../impl/shared/network/jh_dns_request_state.h"
#include "../../../../impl/shared/network/jh_icmp_echo.h"
#include "../../../../impl/shared/network/jh_network_service.h"
#include "rp2040_cyw43_platform.h"
#include "rp2040_cyw43_provider.h"

#include <lwip/dns.h>
#include <lwip/icmp.h>
#include <lwip/inet_chksum.h>
#include <lwip/ip4_addr.h>
#include <lwip/raw.h>
#include <lwip/timeouts.h>
#include <pico/cyw43_arch.h>
#include <pico/error.h>
#include <pico/time.h>
#include <string.h>

extern "C" void __real_cyw43_cb_tcpip_init(cyw43_t *self, int itf);
extern "C" void __real_cyw43_cb_tcpip_deinit(cyw43_t *self, int itf);

#ifndef HAL_CYW43_SCAN_RESULT_CAPACITY
#define HAL_CYW43_SCAN_RESULT_CAPACITY 64u
#endif

static bool s_initialized = false;
static bool s_sta_netif_initialized = false;
static struct netif *s_sta_netif = nullptr;
static uint32_t s_timeout_ms = 15000u;
static hal_mutex_t s_network_mutex = NULL;
static bool s_ping_active = false;
static bool s_radio_operation_active = false;
static uint16_t s_ping_sequence = 0u;
static bool s_scan_overflow = false;
static size_t s_scan_count = 0u;
static char s_hostname[64] = {};
static hal_wifi_scan_result_t s_scan_results[HAL_CYW43_SCAN_RESULT_CAPACITY];
static jh_network_service_t s_network_service = {};
static bool s_network_service_initialized = false;

static jh_dns_ipv4_request_state_t s_dns_request = {};

struct netif *__getCYW43Netif() { return s_sta_netif; }

static void network_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_network_mutex);
}

static bool network_begin_radio_operation(void) {
  network_ensure_mutex();
  hal_mutex_lock(s_network_mutex);
  if (s_dns_request.active || s_ping_active || s_radio_operation_active) {
    hal_mutex_unlock(s_network_mutex);
    return false;
  }
  s_radio_operation_active = true;
  hal_mutex_unlock(s_network_mutex);
  return true;
}

static void network_end_radio_operation(void) {
  hal_mutex_lock(s_network_mutex);
  s_radio_operation_active = false;
  hal_mutex_unlock(s_network_mutex);
}

static hal_status_t network_init_sta_netif(void) {
  if (s_sta_netif_initialized) {
    return HAL_OK;
  }
  if (s_sta_netif != nullptr &&
      s_sta_netif != &cyw43_state.netif[CYW43_ITF_STA]) {
    return HAL_EBUSY;
  }

  cyw43_arch_lwip_begin();
  __real_cyw43_cb_tcpip_init(&cyw43_state, CYW43_ITF_STA);
  s_sta_netif = &cyw43_state.netif[CYW43_ITF_STA];
  if (s_hostname[0] != '\0') {
    netif_set_hostname(s_sta_netif, s_hostname);
  }
  cyw43_arch_lwip_end();
  s_sta_netif_initialized = true;
  return HAL_OK;
}

static void network_deinit_sta_netif(void) {
  if (!s_sta_netif_initialized) {
    return;
  }

  cyw43_arch_lwip_begin();
  __real_cyw43_cb_tcpip_deinit(&cyw43_state, CYW43_ITF_STA);
  if (s_sta_netif == &cyw43_state.netif[CYW43_ITF_STA]) {
    s_sta_netif = nullptr;
  }
  const ip_addr_t no_dns_server = {};
  dns_setserver(0u, &no_dns_server);
  cyw43_arch_lwip_end();
  s_sta_netif_initialized = false;
}

static hal_status_t network_reset_sta_netif(void) {
  hal_status_t status = jh_network_service_stop(&s_network_service);
  if (status != HAL_OK) {
    return status;
  }

  status = jh_rp2040_cyw43_platform_status(
      cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA));
  if (status == HAL_OK) {
    network_deinit_sta_netif();
  }
  const hal_status_t restart_status =
      jh_network_service_start(&s_network_service);
  return status == HAL_OK ? restart_status : status;
}

static int network_join_blocking(const char *ssid, const char *password,
                                 uint32_t auth, uint32_t timeout_ms) {
  const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
  int platform_status = PICO_ERROR_CONNECT_FAILED;

  do {
    const int64_t remaining_us =
        absolute_time_diff_us(get_absolute_time(), deadline);
    if (remaining_us <= 0) {
      break;
    }
    const uint64_t remaining_ms_rounded =
        ((uint64_t)remaining_us + 999u) / 1000u;
    const uint32_t remaining_ms = remaining_ms_rounded > UINT32_MAX
                                      ? UINT32_MAX
                                      : (uint32_t)remaining_ms_rounded;
    platform_status =
        cyw43_arch_wifi_connect_timeout_ms(ssid, password, auth, remaining_ms);
    if (platform_status != PICO_ERROR_CONNECT_FAILED) {
      return platform_status;
    }
    cyw43_arch_poll();
  } while (!time_reached(deadline));

  return platform_status;
}

static size_t find_scan_result(const uint8_t bssid[HAL_WIFI_BSSID_LEN]) {
  for (size_t index = 0u; index < s_scan_count; ++index) {
    if (memcmp(s_scan_results[index].bssid, bssid, HAL_WIFI_BSSID_LEN) == 0) {
      return index;
    }
  }
  return s_scan_count;
}

static int scan_result_callback(void *, const cyw43_ev_scan_result_t *result) {
  if (result == nullptr) {
    return 0;
  }

  size_t index = find_scan_result(result->bssid);
  if (index == s_scan_count) {
    if (s_scan_count >= HAL_CYW43_SCAN_RESULT_CAPACITY) {
      s_scan_overflow = true;
      return 0;
    }
    ++s_scan_count;
  }

  hal_wifi_scan_result_t *destination = &s_scan_results[index];
  memset(destination, 0, sizeof(*destination));
  size_t ssid_length = result->ssid_len;
  if (ssid_length >= sizeof(destination->ssid)) {
    ssid_length = sizeof(destination->ssid) - 1u;
  }
  memcpy(destination->ssid, result->ssid, ssid_length);
  memcpy(destination->bssid, result->bssid, sizeof(destination->bssid));
  destination->encryption = jh_cyw43_scan_auth_to_hal(result->auth_mode);
  destination->rssi = result->rssi;
  destination->channel = result->channel;
  return 0;
}

hal_status_t jh_rp2040_cyw43_provider_init(void) {
  if (s_initialized) {
    return HAL_OK;
  }

  hal_status_t status = jh_rp2040_cyw43_platform_init(HAL_CYW43_COUNTRY_CODE);
  if (status != HAL_OK) {
    return status;
  }
  network_ensure_mutex();
  hal_status_t service_status = HAL_OK;
  if (!s_network_service_initialized) {
    service_status = jh_network_service_init(
        &s_network_service, jh_rp2040_cyw43_platform_service_port());
    s_network_service_initialized = service_status == HAL_OK;
  }
  if (service_status == HAL_OK) {
    service_status = jh_network_service_start(&s_network_service);
  }
  if (service_status != HAL_OK) {
    jh_rp2040_cyw43_platform_deinit();
    return service_status;
  }
  s_initialized = true;
  return HAL_OK;
}

hal_status_t jh_rp2040_cyw43_provider_deinit_for_baseline(void) {
  if (!s_initialized) {
    return HAL_OK;
  }
  if (!network_begin_radio_operation()) {
    return HAL_EBUSY;
  }

  hal_status_t status = jh_network_service_stop(&s_network_service);
  if (status == HAL_OK) {
    (void)cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    network_deinit_sta_netif();
    jh_rp2040_cyw43_platform_deinit();
    s_initialized = false;
    s_ping_active = false;
    s_scan_overflow = false;
    s_scan_count = 0u;
    memset(&s_dns_request, 0, sizeof(s_dns_request));
  }
  network_end_radio_operation();
  return status;
}

hal_status_t jh_cyw43_provider_service(void) {
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  cyw43_arch_poll();
  sys_check_timeouts();
  return HAL_OK;
}

hal_status_t jh_rp2040_cyw43_provider_join(const char *ssid,
                                           const char *password,
                                           bool non_blocking,
                                           uint32_t timeout_ms) {
  if (ssid == nullptr || ssid[0] == '\0' || password == nullptr ||
      (!non_blocking && timeout_ms == 0u)) {
    return HAL_EINVAL;
  }
  hal_status_t status = jh_rp2040_cyw43_provider_init();
  if (status != HAL_OK) {
    return status;
  }
  if (!network_begin_radio_operation()) {
    return HAL_EBUSY;
  }
  cyw43_arch_poll();
  if (cyw43_wifi_scan_active(&cyw43_state)) {
    network_end_radio_operation();
    return HAL_EBUSY;
  }
  if (s_sta_netif_initialized) {
    status = network_reset_sta_netif();
    if (status != HAL_OK) {
      network_end_radio_operation();
      return status;
    }
  }
  status = network_init_sta_netif();
  if (status != HAL_OK) {
    network_end_radio_operation();
    return status;
  }

  const uint32_t auth =
      password[0] == '\0' ? CYW43_AUTH_OPEN : CYW43_AUTH_WPA2_AES_PSK;
  const int platform_status =
      non_blocking ? cyw43_arch_wifi_connect_async(ssid, password, auth)
                   : network_join_blocking(ssid, password, auth, timeout_ms);
  network_end_radio_operation();
  return jh_rp2040_cyw43_platform_status(platform_status);
}

hal_status_t jh_rp2040_cyw43_provider_leave(void) {
  if (!s_initialized) {
    return HAL_OK;
  }
  if (!network_begin_radio_operation()) {
    return HAL_EBUSY;
  }
  cyw43_arch_poll();
  if (cyw43_wifi_scan_active(&cyw43_state)) {
    network_end_radio_operation();
    return HAL_EBUSY;
  }
  const hal_status_t status = network_reset_sta_netif();
  network_end_radio_operation();
  return status;
}

hal_status_t
jh_rp2040_cyw43_provider_link_status(jh_cyw43_link_status_t *out_link_status) {
  if (out_link_status == nullptr) {
    return HAL_EINVAL;
  }
  hal_status_t status = jh_rp2040_cyw43_provider_init();
  if (status != HAL_OK) {
    return status;
  }
  if (!network_begin_radio_operation()) {
    return HAL_EBUSY;
  }
  cyw43_arch_poll();
  cyw43_arch_lwip_begin();
  const int link_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
  cyw43_arch_lwip_end();
  switch (link_status) {
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
  case CYW43_LINK_FAIL:
    *out_link_status = JH_CYW43_LINK_FAILED;
    break;
  case CYW43_LINK_NONET:
    *out_link_status = JH_CYW43_LINK_NO_NETWORK;
    break;
  case CYW43_LINK_BADAUTH:
    *out_link_status = JH_CYW43_LINK_BAD_AUTH;
    break;
  default:
    *out_link_status = JH_CYW43_LINK_UNKNOWN;
    break;
  }
  network_end_radio_operation();
  return HAL_OK;
}

hal_status_t jh_rp2040_cyw43_provider_get_mac(uint8_t mac[HAL_WIFI_BSSID_LEN]) {
  if (mac == nullptr) {
    return HAL_EINVAL;
  }
  hal_status_t status = jh_rp2040_cyw43_provider_init();
  if (status != HAL_OK) {
    return status;
  }
  if (!network_begin_radio_operation()) {
    return HAL_EBUSY;
  }
  const hal_status_t mac_status = jh_rp2040_cyw43_platform_status(
      cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac));
  network_end_radio_operation();
  return mac_status;
}

hal_status_t jh_rp2040_cyw43_provider_get_rssi(int32_t *out_rssi) {
  if (out_rssi == nullptr) {
    return HAL_EINVAL;
  }
  hal_status_t status = jh_rp2040_cyw43_provider_init();
  if (status != HAL_OK) {
    return status;
  }
  if (!network_begin_radio_operation()) {
    return HAL_EBUSY;
  }
  if (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_JOIN) {
    network_end_radio_operation();
    return HAL_ESTATE;
  }
  const hal_status_t rssi_status = jh_rp2040_cyw43_platform_status(
      cyw43_wifi_get_rssi(&cyw43_state, out_rssi));
  network_end_radio_operation();
  return rssi_status;
}

hal_status_t jh_rp2040_cyw43_provider_set_hostname(const char *hostname) {
  if (hostname == nullptr || hostname[0] == '\0') {
    return HAL_EINVAL;
  }
  const size_t hostname_length = strlen(hostname);
  if (hostname_length >= sizeof(s_hostname)) {
    return HAL_EOVERFLOW;
  }
  if (!network_begin_radio_operation()) {
    return HAL_EBUSY;
  }

  memcpy(s_hostname, hostname, hostname_length + 1u);
  if (s_sta_netif_initialized) {
    cyw43_arch_lwip_begin();
    netif_set_hostname(&cyw43_state.netif[CYW43_ITF_STA], s_hostname);
    cyw43_arch_lwip_end();
  }
  network_end_radio_operation();
  return HAL_OK;
}

hal_status_t jh_rp2040_cyw43_provider_lwip_begin(bool require_ipv4) {
  const hal_status_t status = jh_rp2040_cyw43_provider_init();
  if (status != HAL_OK) {
    return status;
  }
  return jh_network_service_enter(&s_network_service, require_ipv4);
}

void jh_rp2040_cyw43_provider_lwip_end(void) {
  (void)jh_network_service_leave(&s_network_service);
}

static void copy_ipv4_bytes(const ip4_addr_t *address, uint8_t out[4]) {
  out[0] = ip4_addr1(address);
  out[1] = ip4_addr2(address);
  out[2] = ip4_addr3(address);
  out[3] = ip4_addr4(address);
}

hal_status_t jh_rp2040_cyw43_provider_get_local_ipv4(uint8_t out_address[4]) {
  if (out_address == nullptr) {
    return HAL_EINVAL;
  }
  hal_status_t status = jh_rp2040_cyw43_provider_init();
  if (status != HAL_OK) {
    return status;
  }
  if (!network_begin_radio_operation()) {
    return HAL_EBUSY;
  }
  cyw43_arch_poll();
  cyw43_arch_lwip_begin();
  copy_ipv4_bytes(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]),
                  out_address);
  cyw43_arch_lwip_end();
  network_end_radio_operation();
  return HAL_OK;
}

hal_status_t jh_rp2040_cyw43_provider_get_dns_ipv4(uint8_t out_address[4]) {
  if (out_address == nullptr) {
    return HAL_EINVAL;
  }
  hal_status_t status = jh_rp2040_cyw43_provider_init();
  if (status != HAL_OK) {
    return status;
  }
  if (!network_begin_radio_operation()) {
    return HAL_EBUSY;
  }
  cyw43_arch_poll();
  cyw43_arch_lwip_begin();
  const ip_addr_t *dns_address = dns_getserver(0u);
  copy_ipv4_bytes(ip_2_ip4(dns_address), out_address);
  cyw43_arch_lwip_end();
  network_end_radio_operation();
  return HAL_OK;
}

void jh_rp2040_cyw43_provider_set_timeout_ms(uint32_t timeout_ms) {
  network_ensure_mutex();
  hal_mutex_lock(s_network_mutex);
  s_timeout_ms = timeout_ms;
  hal_mutex_unlock(s_network_mutex);
}

static void dns_result_callback(const char *, const ip_addr_t *address,
                                void *argument) {
  const uint32_t generation = (uint32_t) reinterpret_cast<uintptr_t>(argument);
  uint8_t resolved_address[HAL_NET_IPV4_ADDR_LEN] = {};
  const bool found = address != nullptr && IP_IS_V4(address);
  if (found) {
    copy_ipv4_bytes(ip_2_ip4(address), resolved_address);
  }

  hal_mutex_lock(s_network_mutex);
  (void)jh_dns_ipv4_request_complete(&s_dns_request, generation, found,
                                     resolved_address);
  hal_mutex_unlock(s_network_mutex);
}

hal_status_t jh_rp2040_cyw43_provider_resolve_ipv4(
    const char *hostname, uint8_t out_address[HAL_NET_IPV4_ADDR_LEN]) {
  if (hostname == nullptr || hostname[0] == '\0' || out_address == nullptr) {
    return HAL_EINVAL;
  }
  hal_status_t status = jh_rp2040_cyw43_provider_init();
  if (status != HAL_OK) {
    return status;
  }

  cyw43_arch_poll();
  network_ensure_mutex();
  hal_mutex_lock(s_network_mutex);
  if (s_dns_request.active || s_ping_active || s_radio_operation_active) {
    hal_mutex_unlock(s_network_mutex);
    return HAL_EBUSY;
  }
  if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
    hal_mutex_unlock(s_network_mutex);
    return HAL_ESTATE;
  }
  const uint32_t timeout_ms = s_timeout_ms;
  if (timeout_ms == 0u) {
    hal_mutex_unlock(s_network_mutex);
    return HAL_EINVAL;
  }
  const uint32_t generation = jh_dns_ipv4_request_begin(&s_dns_request);
  hal_mutex_unlock(s_network_mutex);
  if (generation == 0u) {
    return HAL_EBUSY;
  }

  ip_addr_t resolved = {};
  cyw43_arch_lwip_begin();
  const err_t dns_status = dns_gethostbyname_addrtype(
      hostname, &resolved, dns_result_callback,
      reinterpret_cast<void *>(static_cast<uintptr_t>(generation)),
      LWIP_DNS_ADDRTYPE_IPV4);
  cyw43_arch_lwip_end();
  if (dns_status == ERR_OK) {
    uint8_t resolved_address[HAL_NET_IPV4_ADDR_LEN] = {};
    copy_ipv4_bytes(ip_2_ip4(&resolved), resolved_address);
    hal_mutex_lock(s_network_mutex);
    (void)jh_dns_ipv4_request_complete(&s_dns_request, generation, true,
                                       resolved_address);
    memcpy(out_address, s_dns_request.address, HAL_NET_IPV4_ADDR_LEN);
    hal_mutex_unlock(s_network_mutex);
    return HAL_OK;
  }
  if (dns_status != ERR_INPROGRESS) {
    hal_mutex_lock(s_network_mutex);
    (void)jh_dns_ipv4_request_cancel(&s_dns_request, generation);
    hal_mutex_unlock(s_network_mutex);
    if (dns_status == ERR_MEM) {
      return HAL_ENOMEM;
    }
    return dns_status == ERR_ARG || dns_status == ERR_VAL ? HAL_EINVAL
                                                          : HAL_EIO;
  }

  const uint64_t deadline_us = time_us_64() + ((uint64_t)timeout_ms * 1000u);
  for (;;) {
    cyw43_arch_poll();
    hal_mutex_lock(s_network_mutex);
    const bool completed = s_dns_request.completed;
    const bool found = s_dns_request.found;
    if (completed && found) {
      memcpy(out_address, s_dns_request.address, HAL_NET_IPV4_ADDR_LEN);
    }
    hal_mutex_unlock(s_network_mutex);
    if (completed) {
      return found ? HAL_OK : HAL_ENOENT;
    }
    if (time_us_64() >= deadline_us) {
      hal_mutex_lock(s_network_mutex);
      const bool completed_at_deadline =
          s_dns_request.generation == generation && s_dns_request.completed;
      const bool found_at_deadline =
          completed_at_deadline && s_dns_request.found;
      if (completed_at_deadline && found_at_deadline) {
        memcpy(out_address, s_dns_request.address, HAL_NET_IPV4_ADDR_LEN);
      }
      if (!completed_at_deadline) {
        (void)jh_dns_ipv4_request_cancel(&s_dns_request, generation);
      }
      hal_mutex_unlock(s_network_mutex);
      if (completed_at_deadline) {
        return found_at_deadline ? HAL_OK : HAL_ENOENT;
      }
      return HAL_ETIMEOUT;
    }
    sleep_ms(1u);
  }
}

typedef struct {
  uint16_t identifier;
  uint16_t sequence;
  bool completed;
  int ttl;
} cyw43_ping_request_t;

static uint8_t ping_receive_callback(void *argument, struct raw_pcb *,
                                     struct pbuf *packet, const ip_addr_t *) {
  cyw43_ping_request_t *request = static_cast<cyw43_ping_request_t *>(argument);
  uint8_t reply[68] = {};
  const size_t reply_size =
      packet->tot_len < sizeof(reply) ? packet->tot_len : sizeof(reply);
  if (pbuf_copy_partial(packet, reply, reply_size, 0u) != reply_size) {
    return 0u;
  }
  int ttl = -1;
  if (jh_icmp_echo_reply_parse(reply, reply_size, request->identifier,
                               request->sequence, &ttl) != HAL_OK) {
    return 0u;
  }

  hal_mutex_lock(s_network_mutex);
  request->ttl = ttl;
  request->completed = true;
  hal_mutex_unlock(s_network_mutex);
  pbuf_free(packet);
  return 1u;
}

hal_status_t
jh_rp2040_cyw43_provider_ping_ipv4(const uint8_t address[HAL_NET_IPV4_ADDR_LEN],
                                   uint32_t timeout_ms, int *out_result) {
  if (address == nullptr || out_result == nullptr || timeout_ms == 0u) {
    return HAL_EINVAL;
  }
  *out_result = -1;
  hal_status_t status = jh_rp2040_cyw43_provider_init();
  if (status != HAL_OK) {
    return status;
  }
  cyw43_arch_poll();
  if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
    return HAL_ESTATE;
  }

  network_ensure_mutex();
  hal_mutex_lock(s_network_mutex);
  if (s_dns_request.active || s_ping_active || s_radio_operation_active) {
    hal_mutex_unlock(s_network_mutex);
    return HAL_EBUSY;
  }
  s_ping_active = true;
  cyw43_ping_request_t request = {
      0x4a48u,
      ++s_ping_sequence,
      false,
      -1,
  };
  hal_mutex_unlock(s_network_mutex);

  ip_addr_t destination = {};
  IP_ADDR4(&destination, address[0], address[1], address[2], address[3]);
  constexpr uint16_t payload_size = 32u;
  const uint16_t packet_size = sizeof(struct icmp_echo_hdr) + payload_size;

  cyw43_arch_lwip_begin();
  struct raw_pcb *pcb = raw_new(IP_PROTO_ICMP);
  struct pbuf *packet =
      pcb == nullptr ? nullptr : pbuf_alloc(PBUF_IP, packet_size, PBUF_RAM);
  if (pcb == nullptr || packet == nullptr || packet->len != packet->tot_len ||
      packet->next != nullptr) {
    if (packet != nullptr) {
      pbuf_free(packet);
    }
    if (pcb != nullptr) {
      raw_remove(pcb);
    }
    cyw43_arch_lwip_end();
    hal_mutex_lock(s_network_mutex);
    s_ping_active = false;
    hal_mutex_unlock(s_network_mutex);
    return HAL_ENOMEM;
  }

  pcb->ttl = 64u;
  raw_recv(pcb, ping_receive_callback, &request);
  const err_t bind_status = raw_bind(pcb, IP_ADDR_ANY);
  struct icmp_echo_hdr *echo =
      static_cast<struct icmp_echo_hdr *>(packet->payload);
  ICMPH_TYPE_SET(echo, ICMP_ECHO);
  ICMPH_CODE_SET(echo, 0u);
  echo->chksum = 0u;
  echo->id = lwip_htons(request.identifier);
  echo->seqno = lwip_htons(request.sequence);
  for (uint16_t index = 0u; index < payload_size; ++index) {
    static_cast<uint8_t *>(packet->payload)[sizeof(*echo) + index] =
        (uint8_t)('A' + (index % 26u));
  }
  echo->chksum = inet_chksum(echo, packet_size);
  const err_t send_status = bind_status == ERR_OK
                                ? raw_sendto(pcb, packet, &destination)
                                : bind_status;
  cyw43_arch_lwip_end();

  if (send_status == ERR_OK) {
    const uint64_t deadline_us = time_us_64() + ((uint64_t)timeout_ms * 1000u);
    for (;;) {
      cyw43_arch_poll();
      hal_mutex_lock(s_network_mutex);
      const bool completed = request.completed;
      const int ttl = request.ttl;
      hal_mutex_unlock(s_network_mutex);
      if (completed) {
        *out_result = ttl;
        status = HAL_OK;
        break;
      }
      if (time_us_64() >= deadline_us) {
        status = HAL_ETIMEOUT;
        break;
      }
      sleep_ms(1u);
    }
  } else {
    status = send_status == ERR_MEM ? HAL_ENOMEM : HAL_EIO;
  }

  cyw43_arch_lwip_begin();
  pbuf_free(packet);
  raw_remove(pcb);
  cyw43_arch_lwip_end();
  hal_mutex_lock(s_network_mutex);
  s_ping_active = false;
  hal_mutex_unlock(s_network_mutex);
  return status;
}

hal_status_t jh_rp2040_cyw43_provider_scan(uint32_t timeout_ms,
                                           int *out_count) {
  if (out_count == nullptr || timeout_ms == 0u) {
    return HAL_EINVAL;
  }
  *out_count = 0;

  hal_status_t status = jh_rp2040_cyw43_provider_init();
  if (status != HAL_OK) {
    return status;
  }
  if (!network_begin_radio_operation()) {
    return HAL_EBUSY;
  }
  if (cyw43_wifi_scan_active(&cyw43_state)) {
    network_end_radio_operation();
    return HAL_EBUSY;
  }

  memset(s_scan_results, 0, sizeof(s_scan_results));
  s_scan_count = 0u;
  s_scan_overflow = false;
  cyw43_wifi_scan_options_t options = {};
  int platform_status =
      cyw43_wifi_scan(&cyw43_state, &options, nullptr, scan_result_callback);
  if (platform_status != PICO_OK) {
    network_end_radio_operation();
    return jh_rp2040_cyw43_platform_status(platform_status);
  }

  const uint64_t deadline_us = time_us_64() + ((uint64_t)timeout_ms * 1000u);
  while (cyw43_wifi_scan_active(&cyw43_state)) {
    cyw43_arch_poll();
    if (time_us_64() >= deadline_us) {
      network_end_radio_operation();
      return HAL_ETIMEOUT;
    }
    sleep_ms(1u);
  }

  *out_count = (int)s_scan_count;
  network_end_radio_operation();
  return s_scan_overflow ? HAL_EOVERFLOW : HAL_OK;
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

#endif
#endif
