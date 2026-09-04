#pragma once

/** @file Formatting and scan helpers shared by network applications. */

#include "hal/core/hal_status.h"
#include "hal/core/hal_text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bytes required for a terminated `XX:XX:XX:XX:XX:XX` string. */
#define HAL_NETWORK_MAC_STRING_SIZE HAL_TEXT_MAC_STRING_SIZE

/**
 * @brief Format a six-byte MAC address using uppercase hexadecimal digits.
 * @param mac Six input bytes in display order.
 * @param buffer Destination buffer.
 * @param buffer_size Destination capacity including the terminator.
 * @return HAL_OK, HAL_EINVAL for a NULL pointer, HAL_EOVERFLOW for a short
 * buffer, or HAL_EIO when formatting fails.
 */
hal_status_t hal_network_format_mac_ex(const uint8_t mac[6], char *buffer,
                                       size_t buffer_size);

/**
 * @brief Scan Wi-Fi networks and match an SSID prefix.
 * @param ssid_prefix Prefix to find; NULL or an empty string matches nothing.
 * @param log_results Log the scan table and result through `deb` when true.
 * @param out_found Receives whether a matching network was found.
 * @return HAL_OK after a completed scan, a Wi-Fi scan error,
 * HAL_EUNSUPPORTED when Wi-Fi is disabled, or HAL_EINVAL for a NULL output.
 */
hal_status_t hal_wifi_scan_for_ssid_ex(const char *ssid_prefix,
                                       bool log_results, bool *out_found);

/**
 * @brief Scan with logging enabled and find an SSID prefix.
 * @param ssid_prefix Prefix to find.
 * @return true when a matching network was found; false on no match or error.
 */
bool hal_wifi_scan_for_ssid(const char *ssid_prefix);

#ifdef __cplusplus
}
#endif
