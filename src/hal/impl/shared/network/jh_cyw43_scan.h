#pragma once

#include "../../../hal_wifi.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* cyw43-driver scan security bits: WEP=1, WPA=2, WPA2=4. */
static inline hal_wifi_encryption_t
jh_cyw43_scan_auth_to_hal(uint8_t auth_mode) {
  switch (auth_mode) {
  case 0u:
    return HAL_WIFI_ENC_NONE;
  case 3u:
    return HAL_WIFI_ENC_WPA;
  case 5u:
    return HAL_WIFI_ENC_WPA2;
  case 7u:
    return HAL_WIFI_ENC_AUTO;
  default:
    return HAL_WIFI_ENC_UNKNOWN;
  }
}

#ifdef __cplusplus
}
#endif
