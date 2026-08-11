#ifndef JH_CYW43_SCAN_RESULTS_H
#define JH_CYW43_SCAN_RESULTS_H

#include "hal/network/hal_wifi.h"
#include "hal/network/jh_cyw43_scan.h"
#include "jh_cyw43_driver.h"

#include <string.h>

static inline int
jh_cyw43_collect_scan_result(hal_wifi_scan_result_t *results, size_t capacity,
                             size_t *count, bool *overflow,
                             const cyw43_ev_scan_result_t *result) {
  if (result == nullptr) {
    return 0;
  }
  size_t index = 0u;
  while (index < *count &&
         memcmp(results[index].bssid, result->bssid, HAL_WIFI_BSSID_LEN) != 0) {
    ++index;
  }
  if (index == *count) {
    if (*count >= capacity) {
      *overflow = true;
      return 0;
    }
    ++*count;
  }
  hal_wifi_scan_result_t *destination = &results[index];
  memset(destination, 0, sizeof(*destination));
  size_t ssid_length = result->ssid_len;
  if (ssid_length >= sizeof(destination->ssid)) {
    ssid_length = sizeof(destination->ssid) - 1u;
  }
  memcpy(destination->ssid, result->ssid, ssid_length);
  memcpy(destination->bssid, result->bssid, HAL_WIFI_BSSID_LEN);
  destination->encryption = jh_cyw43_scan_auth_to_hal(result->auth_mode);
  destination->rssi = result->rssi;
  destination->channel = result->channel;
  return 0;
}

#endif
