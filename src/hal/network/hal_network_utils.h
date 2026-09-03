#pragma once

/** @file Formatting and scan helpers shared by network applications. */

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_NETWORK_MAC_STRING_SIZE 18u

hal_status_t hal_network_format_mac_ex(const uint8_t mac[6], char *buffer,
                                       size_t buffer_size);

/** Scan, optionally log results, and match an SSID prefix. */
hal_status_t hal_wifi_scan_for_ssid_ex(const char *ssid_prefix,
                                       bool log_results, bool *out_found);

/** Scan with logging enabled and return whether the SSID prefix was found. */
bool hal_wifi_scan_for_ssid(const char *ssid_prefix);

#ifdef __cplusplus
}
#endif
