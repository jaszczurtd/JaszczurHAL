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
      s_backend->load == nullptr || s_backend->store == nullptr ||
      s_backend->program == nullptr || s_backend->erase == nullptr ||
      s_backend->read == nullptr) {
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
  out_info->erase_size = s_backend->erase_size;
  out_info->program_size = s_backend->program_size;
  out_info->append_size = s_backend->append_size;
  return HAL_OK;
}

bool range_valid(uint16_t addr, uint16_t len) {
  return s_ready && addr <= s_active_size &&
         len <= static_cast<uint16_t>(s_active_size - addr);
}

bool storage_range_valid(uint16_t addr, uint16_t len) {
  return addr <= s_storage_size && len <= s_storage_size - addr;
}

bool mirror_erased(uint16_t addr, uint16_t len) {
  for (uint16_t index = 0u; index < len; index++) {
    if (s_backend->mirror[addr + index] != 0xFFu) {
      return false;
    }
  }
  return true;
}

/* A failed write may have changed the medium in part; the mirror must follow
 * it, or a later erased-range check would trust stale 0xFF bytes. */
void resync_mirror(uint16_t addr, uint16_t len) {
  if (s_backend->read(s_backend->context, addr, s_backend->mirror + addr,
                      len) != HAL_OK) {
    memset(s_backend->mirror + addr, 0, len);
  }
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
  const hal_status_t prepare = jh_eeprom_flash_write_begin();
  if (prepare != HAL_OK) {
    return prepare;
  }
  const hal_status_t status = s_backend->store(
      s_backend->context, s_backend->mirror, s_storage_size, progress, ctx);
  jh_eeprom_flash_write_end();
  if (status == HAL_OK) {
    s_dirty = false;
  }
  return status;
}

hal_status_t replace_region(uint16_t addr, const uint8_t *data, uint16_t len,
                            uint16_t publish_size,
                            hal_eeprom_progress_callback_t progress,
                            void *ctx) {
  if (!s_ready || s_backend->replace_region == nullptr) {
    return s_ready ? HAL_EUNSUPPORTED : HAL_EUNINIT;
  }
  if (data == nullptr || publish_size == 0u || publish_size >= len ||
      addr > s_storage_size || len > s_storage_size - addr) {
    return HAL_EINVAL;
  }
  const hal_status_t prepare = jh_eeprom_flash_write_begin();
  if (prepare != HAL_OK) {
    return prepare;
  }
  const bool reads_erased = mirror_erased(addr, len);
  hal_status_t status =
      s_backend->replace_region(s_backend->context, addr, data, len,
                                publish_size, reads_erased, progress, ctx);
  /* Reading 0xFF does not make flash erased: a programmer may have written
   * 0xFF there, and flash with ECC refuses a second write. Erase first. */
  if (status == HAL_EIO && reads_erased) {
    status = s_backend->replace_region(s_backend->context, addr, data, len,
                                       publish_size, false, progress, ctx);
  }
  jh_eeprom_flash_write_end();
  if (status == HAL_OK) {
    memcpy(s_backend->mirror + addr, data, len);
  } else {
    resync_mirror(addr, len);
  }
  return status;
}

hal_status_t append(uint16_t addr, const uint8_t *data, uint16_t len,
                    hal_eeprom_progress_callback_t progress, void *ctx) {
  if (!s_ready) {
    return HAL_EUNINIT;
  }
  if (data == nullptr || len == 0u || !storage_range_valid(addr, len)) {
    return HAL_EINVAL;
  }
  if (!mirror_erased(addr, len)) {
    return HAL_ESTATE;
  }
  const hal_status_t prepare = jh_eeprom_flash_write_begin();
  if (prepare != HAL_OK) {
    return prepare;
  }
  hal_status_t status =
      s_backend->program(s_backend->context, addr, data, len, progress, ctx);
  jh_eeprom_flash_write_end();
  if (status == HAL_OK) {
    memcpy(s_backend->mirror + addr, data, len);
  } else {
    resync_mirror(addr, len);
    /* Nothing reached the medium although it reads erased: flash that was
     * programmed with 0xFF refuses writes until erased, like a region that
     * is not erased at all. */
    if (status == HAL_EIO && mirror_erased(addr, len)) {
      status = HAL_ESTATE;
    }
  }
  return status;
}

hal_status_t erase(uint16_t addr, uint16_t len,
                   hal_eeprom_progress_callback_t progress, void *ctx) {
  if (!s_ready) {
    return HAL_EUNINIT;
  }
  if (!storage_range_valid(addr, len)) {
    return HAL_EINVAL;
  }
  const hal_status_t prepare = jh_eeprom_flash_write_begin();
  if (prepare != HAL_OK) {
    return prepare;
  }
  const hal_status_t status =
      s_backend->erase(s_backend->context, addr, len, progress, ctx);
  jh_eeprom_flash_write_end();
  if (status == HAL_OK) {
    memset(s_backend->mirror + addr, 0xFF, len);
  } else {
    resync_mirror(addr, len);
  }
  return status;
}

hal_status_t region_erased(uint16_t addr, uint16_t len, bool *out_erased) {
  if (!s_ready) {
    return HAL_EUNINIT;
  }
  if (out_erased == nullptr || !storage_range_valid(addr, len)) {
    return HAL_EINVAL;
  }
  *out_erased = mirror_erased(addr, len);
  return HAL_OK;
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

const jh_eeprom_provider_ops_t kProvider = {
    initialize, read_bytes, write_bytes, commit,       replace_region,
    reset,      append,     erase,       region_erased};

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
