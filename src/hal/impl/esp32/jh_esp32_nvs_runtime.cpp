#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_mutex_once.h"
#include "hal/impl/esp32/jh_esp32_nvs_runtime.h"
#include "hal/impl/esp32/jh_esp32_status.h"
#include "hal/system/hal_sync.h"

#include <nvs_flash.h>

namespace {

hal_mutex_t s_mutex;
bool s_initialized;

} // namespace

extern "C" hal_status_t jh_esp32_nvs_initialize(void) {
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_mutex);
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_initialized) {
    hal_mutex_unlock(mutex);
    return HAL_OK;
  }
  const esp_err_t status = nvs_flash_init();
  if (status == ESP_ERR_NVS_NO_FREE_PAGES ||
      status == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    hal_mutex_unlock(mutex);
    return HAL_ECONFIG;
  }
  const hal_status_t translated = jh_esp32_status_from_esp_err(status);
  if (translated == HAL_OK) {
    s_initialized = true;
  }
  hal_mutex_unlock(mutex);
  return translated;
}

#endif /* HAL_TARGET_IS_ESP32_FAMILY */
