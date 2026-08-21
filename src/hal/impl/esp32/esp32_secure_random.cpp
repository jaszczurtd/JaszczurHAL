#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_status.h"
#include "hal/security/jh_secure_random.h"

#include <esp_random.h>

#include <stddef.h>

extern "C" hal_status_t jh_secure_random_bytes(void *buffer, size_t length) {
  if (buffer == nullptr || length == 0u) {
    return HAL_EINVAL;
  }
  esp_fill_random(buffer, length);
  return HAL_OK;
}

#endif /* HAL_TARGET_IS_ESP32_FAMILY */
