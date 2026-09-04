#include "hal/network/hal_network_utils.h"

#include "hal/core/hal_config.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_system.h"

#ifdef HAL_ENABLE_WIFI
#include "hal/network/hal_wifi.h"
#endif

#include <string.h>

hal_status_t hal_network_format_mac_ex(const uint8_t mac[6], char *buffer,
                                       size_t buffer_size) {
  return hal_text_format_mac_ex(mac, buffer, buffer_size);
}

hal_status_t hal_wifi_scan_for_ssid_ex(const char *ssid_prefix,
                                       bool log_results, bool *out_found) {
  if (out_found == nullptr) {
    return HAL_EINVAL;
  }
  *out_found = false;
#ifdef HAL_ENABLE_WIFI
  if (log_results) {
    deb("Beginning scan at %lu\n", (unsigned long)hal_millis());
  }
  int count = 0;
  hal_status_t status = hal_wifi_scan_networks_ex(&count);
  if (status != HAL_OK) {
    if (log_results) {
      deb("WiFi scan failed");
    }
    return status;
  }
  if (log_results && count == 0) {
    deb("No WiFi networks found");
  } else if (log_results) {
    deb("Found %d networks\n", count);
    deb("%32s %5s %17s %2s %4s", "SSID", "ENC", "BSSID        ", "CH", "RSSI");
  }

  const size_t prefix_length =
      ssid_prefix == nullptr ? 0u : strlen(ssid_prefix);
  for (int i = 0; i < count; ++i) {
    hal_wifi_scan_result_t network = {};
    status = hal_wifi_get_scan_result_ex((size_t)i, &network);
    if (status != HAL_OK) {
      continue;
    }
    if (log_results) {
      char mac[HAL_NETWORK_MAC_STRING_SIZE] = {};
      (void)hal_network_format_mac_ex(network.bssid, mac, sizeof(mac));
      deb("%32s %5s %17s %2d %4ld", network.ssid,
          hal_wifi_encryption_to_string(network.encryption), mac,
          (int)network.channel, (long)network.rssi);
    }
    if (prefix_length != 0u &&
        strncmp(network.ssid, ssid_prefix, prefix_length) == 0) {
      *out_found = true;
    }
  }
  if (log_results) {
    if (*out_found) {
      deb("network %s is available", ssid_prefix);
    }
    deb("\n--- END --- at %lu\n", (unsigned long)hal_millis());
  }
  return HAL_OK;
#else
  (void)ssid_prefix;
  if (log_results) {
    deb("HAL WiFi disabled");
  }
  return HAL_EUNSUPPORTED;
#endif
}

bool hal_wifi_scan_for_ssid(const char *ssid_prefix) {
  bool found = false;
  (void)hal_wifi_scan_for_ssid_ex(ssid_prefix, true, &found);
  return found;
}
