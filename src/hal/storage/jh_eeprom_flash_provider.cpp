#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_EEPROM

#include "hal/storage/jh_eeprom_provider.h"

#include <string.h>

namespace {

const jh_eeprom_flash_backend_t *s_backend = nullptr;
uint16_t s_storage_size = 0u;
uint16_t s_active_size = 0u;
bool s_ready = false;
bool s_dirty = false;

hal_status_t initialize(const jh_eeprom_provider_config_t *config,
                        jh_eeprom_provider_info_t *out_info) {
  if (config == nullptr || out_info == nullptr || s_backend == nullptr ||
      s_backend->mirror == nullptr || s_backend->mirror_capacity == 0u ||
      s_backend->load == nullptr || s_backend->store == nullptr) {
    return HAL_ECONFIG;
  }
  s_ready = false;
  s_dirty = false;
  s_storage_size = 0u;
  const hal_status_t status =
      s_backend->load(s_backend->context, s_backend->mirror,
                      s_backend->mirror_capacity, &s_storage_size);
  if (status != HAL_OK) {
    return status;
  }
  if (s_storage_size == 0u || s_storage_size > s_backend->mirror_capacity) {
    return HAL_ECONFIG;
  }
  if (config->requested_size > s_storage_size &&
      !s_backend->clamp_oversized_request) {
    return HAL_EINVAL;
  }
  s_active_size =
      config->requested_size == 0u || config->requested_size > s_storage_size
          ? s_storage_size
          : config->requested_size;
  s_ready = true;
  out_info->type = s_backend->type;
  out_info->size = s_active_size;
  return HAL_OK;
}

bool range_valid(uint16_t addr, uint16_t len) {
  return s_ready && addr <= s_active_size &&
         len <= static_cast<uint16_t>(s_active_size - addr);
}

hal_status_t read_bytes(uint16_t addr, uint8_t *out, uint16_t len) {
  if ((out == nullptr && len > 0u) || !range_valid(addr, len)) {
    return s_ready ? HAL_EINVAL : HAL_EUNINIT;
  }
  if (len > 0u) {
    memcpy(out, s_backend->mirror + addr, len);
  }
  return HAL_OK;
}

hal_status_t write_bytes(uint16_t addr, const uint8_t *data, uint16_t len,
                         hal_eeprom_progress_callback_t progress, void *ctx) {
  (void)progress;
  (void)ctx;
  if ((data == nullptr && len > 0u) || !range_valid(addr, len)) {
    return s_ready ? HAL_EINVAL : HAL_EUNINIT;
  }
  if (len > 0u) {
    memcpy(s_backend->mirror + addr, data, len);
    s_dirty = true;
  }
  return HAL_OK;
}

hal_status_t commit(hal_eeprom_progress_callback_t progress, void *ctx) {
  if (!s_ready) {
    return HAL_EUNINIT;
  }
  if (!s_dirty) {
    return HAL_OK;
  }
  const hal_status_t status = s_backend->store(
      s_backend->context, s_backend->mirror, s_storage_size, progress, ctx);
  if (status == HAL_OK) {
    s_dirty = false;
  }
  return status;
}

hal_status_t reset(hal_eeprom_progress_callback_t progress, void *ctx) {
  if (!s_ready) {
    return HAL_EUNINIT;
  }
  const uint16_t clear_size =
      s_backend->clear_full_storage_on_reset ? s_storage_size : s_active_size;
  memset(s_backend->mirror, 0, clear_size);
  s_dirty = true;
  return commit(progress, ctx);
}

const jh_eeprom_provider_ops_t kProvider = {initialize, read_bytes, write_bytes,
                                            commit, reset};

} // namespace

const jh_eeprom_provider_ops_t *
jh_eeprom_flash_provider_configure(const jh_eeprom_flash_backend_t *backend) {
  s_backend = backend;
  return backend != nullptr ? &kProvider : nullptr;
}

const jh_eeprom_provider_ops_t *jh_eeprom_hardware_provider_get_ops(
    hal_eeprom_type_t type, const jh_eeprom_flash_backend_t *flash_backend) {
  if (type == HAL_EEPROM_AT24C256) {
    return jh_at24c256_provider_get_ops();
  }
  const bool flash = type == HAL_EEPROM_DEFAULT ||
                     type == HAL_EEPROM_STM32_FLASH || type == HAL_EEPROM_FLASH;
  return flash ? jh_eeprom_flash_provider_configure(flash_backend) : nullptr;
}

#endif /* HAL_ENABLE_EEPROM */
