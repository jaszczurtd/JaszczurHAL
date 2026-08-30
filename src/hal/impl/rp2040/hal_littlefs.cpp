#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_RP

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_LITTLEFS

#include "drivers/flash/rp_flash_storage.h"
#include "hal/storage/jh_littlefs_provider.h"

#include <hardware/flash.h>

#include <limits.h>
#include <stdint.h>

namespace {

constexpr uint32_t kReadSize = 16u;
constexpr uint32_t kProgSize = FLASH_PAGE_SIZE;
constexpr uint32_t kBlockSize = FLASH_SECTOR_SIZE;
constexpr uint32_t kCacheSize = FLASH_PAGE_SIZE;
constexpr uint32_t kLookaheadSize = 16u;
constexpr int32_t kBlockCycles = 500;

jh_rp_flash_partition_t s_partition = {};
uint8_t s_read_buffer[kCacheSize];
uint8_t s_prog_buffer[kCacheSize];
uint8_t s_lookahead_buffer[kLookaheadSize];

hal_status_t prepare(void *context, jh_littlefs_geometry_t *out_geometry) {
  (void)context;
  if (out_geometry == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t status = jh_rp_flash_storage_partition(
      JH_RP_FLASH_PARTITION_LITTLEFS, &s_partition);
  if (status != HAL_OK) {
    return status;
  }

  *out_geometry = {kReadSize,      kProgSize,
                   kBlockSize,     s_partition.size / kBlockSize,
                   kBlockCycles,   kCacheSize,
                   kLookaheadSize, s_read_buffer,
                   s_prog_buffer,  s_lookahead_buffer};
  return out_geometry->block_count > 0u ? HAL_OK : HAL_ECONFIG;
}

hal_status_t read(void *context, size_t offset, void *buffer, size_t size) {
  (void)context;
  if (offset > UINT32_MAX) {
    return HAL_EOVERFLOW;
  }
  return jh_rp_flash_storage_read(&s_partition, (uint32_t)offset, buffer, size);
}

hal_status_t program(void *context, size_t offset, const void *buffer,
                     size_t size, hal_littlefs_progress_callback_t progress,
                     void *progress_ctx) {
  (void)context;
  if (offset > UINT32_MAX || (offset % kProgSize) != 0u ||
      (size % kProgSize) != 0u) {
    return HAL_EINVAL;
  }
  const hal_status_t status =
      jh_rp_flash_storage_program(&s_partition, (uint32_t)offset, buffer, size);
  if (progress != nullptr) {
    progress(progress_ctx);
  }
  return status;
}

hal_status_t erase(void *context, size_t offset, size_t size,
                   hal_littlefs_progress_callback_t progress,
                   void *progress_ctx) {
  (void)context;
  if (offset > UINT32_MAX || (offset % kBlockSize) != 0u ||
      size != kBlockSize) {
    return HAL_EINVAL;
  }
  const hal_status_t status =
      jh_rp_flash_storage_erase(&s_partition, (uint32_t)offset, size);
  if (progress != nullptr) {
    progress(progress_ctx);
  }
  return status;
}

hal_status_t sync(void *context) {
  (void)context;
  return HAL_OK;
}

const jh_littlefs_block_backend_t kBackend = {nullptr, prepare, read,
                                              program, erase,   sync};

} // namespace

const jh_littlefs_provider_t *jh_littlefs_provider_get(void) {
  return jh_littlefs_lfs_provider_configure(&kBackend);
}

#endif /* HAL_ENABLE_LITTLEFS */
#endif /* HAL_TARGET_IS_RP */
