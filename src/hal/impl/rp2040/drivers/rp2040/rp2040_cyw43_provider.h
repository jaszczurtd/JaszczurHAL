#pragma once

/* Transitional include for target code.  The provider contract and symbols
 * are backend-common; only the Pico bus/scheduler port lives under rp2040/. */
#include "../../../../impl/shared/network/jh_cyw43_provider.h"

#define jh_rp2040_cyw43_provider_init jh_cyw43_provider_init
#define jh_rp2040_cyw43_provider_deinit_for_baseline jh_cyw43_provider_deinit
#define jh_rp2040_cyw43_provider_join jh_cyw43_provider_join
#define jh_rp2040_cyw43_provider_leave jh_cyw43_provider_leave
#define jh_rp2040_cyw43_provider_link_status jh_cyw43_provider_link_status
#define jh_rp2040_cyw43_provider_get_mac jh_cyw43_provider_get_mac
#define jh_rp2040_cyw43_provider_get_rssi jh_cyw43_provider_get_rssi
#define jh_rp2040_cyw43_provider_set_hostname jh_cyw43_provider_set_hostname
#define jh_rp2040_cyw43_provider_lwip_begin jh_cyw43_provider_stack_enter
#define jh_rp2040_cyw43_provider_lwip_end jh_cyw43_provider_stack_leave
#define jh_rp2040_cyw43_provider_get_local_ipv4 jh_cyw43_provider_get_local_ipv4
#define jh_rp2040_cyw43_provider_get_dns_ipv4 jh_cyw43_provider_get_dns_ipv4
#define jh_rp2040_cyw43_provider_set_timeout_ms jh_cyw43_provider_set_timeout_ms
#define jh_rp2040_cyw43_provider_resolve_ipv4 jh_cyw43_provider_resolve_ipv4
#define jh_rp2040_cyw43_provider_ping_ipv4 jh_cyw43_provider_ping_ipv4
#define jh_rp2040_cyw43_provider_scan jh_cyw43_provider_scan
#define jh_rp2040_cyw43_provider_get_scan_result                               \
  jh_cyw43_provider_get_scan_result
