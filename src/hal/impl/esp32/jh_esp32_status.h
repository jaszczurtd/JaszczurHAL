#pragma once

/**
 * @file jh_esp32_status.h
 * @brief Shared ESP-IDF to JaszczurHAL status translation.
 */

#include "hal/core/hal_status.h"

#include <esp_err.h>

static inline hal_status_t
jh_esp32_status_from_esp_err_with_fallback(esp_err_t status,
                                           hal_status_t fallback) {
  switch (status) {
  case ESP_OK:
    return HAL_OK;
  case ESP_ERR_NO_MEM:
    return HAL_ENOMEM;
  case ESP_ERR_INVALID_ARG:
  case ESP_ERR_INVALID_MAC:
    return HAL_EINVAL;
  case ESP_ERR_INVALID_STATE:
    return HAL_ESTATE;
  case ESP_ERR_INVALID_SIZE:
    return HAL_EOVERFLOW;
  case ESP_ERR_NOT_FOUND:
    return HAL_ENOENT;
  case ESP_ERR_NOT_SUPPORTED:
    return HAL_EUNSUPPORTED;
  case ESP_ERR_TIMEOUT:
    return HAL_ETIMEOUT;
  case ESP_ERR_INVALID_RESPONSE:
  case ESP_ERR_INVALID_CRC:
    return HAL_EPROTO;
  case ESP_ERR_INVALID_VERSION:
    return HAL_ECONFIG;
  case ESP_ERR_NOT_FINISHED:
    return HAL_EAGAIN;
  case ESP_ERR_NOT_ALLOWED:
    return HAL_EPERM;
  case ESP_FAIL:
  default:
    return fallback;
  }
}

static inline hal_status_t jh_esp32_status_from_esp_err(esp_err_t status) {
  return jh_esp32_status_from_esp_err_with_fallback(status, HAL_EIO);
}
