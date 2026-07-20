#pragma once

#include "../../../../hal_net.h"
#include "../../../../hal_status.h"
#include "../../../../hal_wifi.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JH_CYW43_LINK_DOWN = 0,
  JH_CYW43_LINK_CONNECTING,
  JH_CYW43_LINK_NO_IP,
  JH_CYW43_LINK_JOINED,
  JH_CYW43_LINK_FAILED,
  JH_CYW43_LINK_NO_NETWORK,
  JH_CYW43_LINK_BAD_AUTH,
  JH_CYW43_LINK_UNKNOWN
} jh_cyw43_link_status_t;

hal_status_t jh_rp2040_cyw43_provider_init(void);
hal_status_t jh_rp2040_cyw43_provider_join(const char *ssid,
                                           const char *password,
                                           bool non_blocking,
                                           uint32_t timeout_ms);
hal_status_t jh_rp2040_cyw43_provider_leave(void);
hal_status_t
jh_rp2040_cyw43_provider_link_status(jh_cyw43_link_status_t *out_link_status);
hal_status_t jh_rp2040_cyw43_provider_get_mac(uint8_t mac[HAL_WIFI_BSSID_LEN]);
hal_status_t jh_rp2040_cyw43_provider_get_rssi(int32_t *out_rssi);
hal_status_t jh_rp2040_cyw43_provider_set_hostname(const char *hostname);
hal_status_t jh_rp2040_cyw43_provider_lwip_begin(bool require_ipv4);
void jh_rp2040_cyw43_provider_lwip_end(void);
hal_status_t jh_rp2040_cyw43_provider_get_local_ipv4(uint8_t out_address[4]);
hal_status_t jh_rp2040_cyw43_provider_get_dns_ipv4(uint8_t out_address[4]);
void jh_rp2040_cyw43_provider_set_timeout_ms(uint32_t timeout_ms);
hal_status_t jh_rp2040_cyw43_provider_resolve_ipv4(
    const char *hostname, uint8_t out_address[HAL_NET_IPV4_ADDR_LEN]);
hal_status_t
jh_rp2040_cyw43_provider_ping_ipv4(const uint8_t address[HAL_NET_IPV4_ADDR_LEN],
                                   uint32_t timeout_ms, int *out_result);
hal_status_t jh_rp2040_cyw43_provider_scan(uint32_t timeout_ms, int *out_count);
hal_status_t
jh_rp2040_cyw43_provider_get_scan_result(size_t index,
                                         hal_wifi_scan_result_t *out);

#ifdef __cplusplus
}
#endif
