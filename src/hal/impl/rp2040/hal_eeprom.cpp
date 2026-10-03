#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_RP

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_EEPROM

#include "hal/impl/rp2040/drivers/flash/rp_flash_storage.h"
#include "hal/storage/jh_eeprom_provider.h"
#include <hardware/flash.h>

namespace {

uint8_t s_mirror[HAL_RP_FLASH_EEPROM_SIZE] = {};
jh_rp_flash_partition_t s_partition = {};

hal_status_t load(void *context, uint8_t *mirror, uint16_t mirror_capacity,
                  uint16_t *out_storage_size) {
  (void)context;
  if (out_storage_size == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t partition_status =
      jh_rp_flash_storage_partition(JH_RP_FLASH_PARTITION_EEPROM, &s_partition);
  if (partition_status != HAL_OK || s_partition.size > mirror_capacity ||
      s_partition.size > UINT16_MAX) {
    return partition_status != HAL_OK ? partition_status : HAL_ECONFIG;
  }
  const hal_status_t read_status =
      jh_rp_flash_storage_read(&s_partition, 0u, mirror, s_partition.size);
  if (read_status == HAL_OK) {
    *out_storage_size = static_cast<uint16_t>(s_partition.size);
  }
  return read_status;
}

void notify(hal_eeprom_progress_callback_t progress, void *ctx) {
  if (progress != nullptr) {
    progress(ctx);
  }
}

hal_status_t store(void *context, const uint8_t *mirror, uint16_t storage_size,
                   hal_eeprom_progress_callback_t progress, void *ctx) {
  (void)context;
  if (storage_size != s_partition.size) {
    return HAL_ECONFIG;
  }
  notify(progress, ctx);
  const hal_status_t status =
      jh_rp_flash_storage_replace(&s_partition, mirror, s_partition.size);
  notify(progress, ctx);
  return status;
}

hal_status_t replace_region(void *context, uint16_t addr, const uint8_t *data,
                            uint16_t len, uint16_t publish_size,
                            bool destination_erased,
                            hal_eeprom_progress_callback_t progress,
                            void *ctx) {
  (void)context;
  if ((uint32_t)addr + len > s_partition.size) {
    return HAL_EOVERFLOW;
  }
  notify(progress, ctx);
  const hal_status_t status = jh_rp_flash_storage_replace_published(
      &s_partition, addr, data, len, publish_size, destination_erased);
  notify(progress, ctx);
  return status;
}

hal_status_t program(void *context, uint16_t addr, const uint8_t *data,
                     uint16_t len, hal_eeprom_progress_callback_t progress,
                     void *ctx) {
  (void)context;
  notify(progress, ctx);
  const hal_status_t status =
      jh_rp_flash_storage_program_bytes(&s_partition, addr, data, len);
  notify(progress, ctx);
  return status;
}

hal_status_t erase(void *context, uint16_t addr, uint16_t len,
                   hal_eeprom_progress_callback_t progress, void *ctx) {
  (void)context;
  notify(progress, ctx);
  const hal_status_t status =
      jh_rp_flash_storage_erase(&s_partition, addr, len);
  notify(progress, ctx);
  return status;
}

hal_status_t read(void *context, uint16_t addr, uint8_t *out, uint16_t len) {
  (void)context;
  return jh_rp_flash_storage_read(&s_partition, addr, out, len);
}

/* NOR flash programs a byte range of a page and leaves 0xFF bytes untouched,
 * so appends need no alignment beyond a byte. */
const jh_eeprom_flash_backend_t kFlashBackend = {HAL_EEPROM_FLASH,
                                                 s_mirror,
                                                 sizeof(s_mirror),
                                                 false,
                                                 false,
                                                 nullptr,
                                                 load,
                                                 store,
                                                 replace_region,
                                                 program,
                                                 erase,
                                                 read,
                                                 FLASH_SECTOR_SIZE,
                                                 FLASH_PAGE_SIZE,
                                                 1u};

} // namespace

const jh_eeprom_provider_ops_t *
jh_eeprom_provider_get_ops(hal_eeprom_type_t type) {
  return jh_eeprom_hardware_provider_get_ops(type, &kFlashBackend);
}

#endif /* HAL_ENABLE_EEPROM */
#endif /* HAL_TARGET_IS_RP */
