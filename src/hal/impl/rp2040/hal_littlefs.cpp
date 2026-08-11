#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP
#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_LITTLEFS

#include "hal/core/hal_mutex_once.h"
#include "hal/serial/hal_serial.h"
#include "hal/storage/hal_littlefs.h"
#include "hal/system/hal_sync.h"

#include "drivers/flash/rp_flash_storage.h"

#include <hardware/flash.h>
#include <lfs.h>

#include <stdint.h>
#include <string.h>

namespace {

constexpr lfs_size_t kReadSize = 16u;
constexpr lfs_size_t kProgSize = FLASH_PAGE_SIZE;
constexpr lfs_size_t kBlockSize = FLASH_SECTOR_SIZE;
constexpr lfs_size_t kCacheSize = FLASH_PAGE_SIZE;
constexpr lfs_size_t kLookaheadSize = 16u;
constexpr int32_t kBlockCycles = 500;

lfs_t s_lfs;
struct lfs_config s_lfs_cfg;
uint8_t s_read_buffer[kCacheSize];
uint8_t s_prog_buffer[kCacheSize];
uint8_t s_lookahead_buffer[kLookaheadSize];
jh_rp_flash_partition_t s_partition = {};
bool s_cfg_ready = false;
bool s_littlefs_mounted = false;
hal_mutex_t s_littlefs_mutex = NULL;
hal_littlefs_progress_callback_t s_progress_callback = NULL;
void *s_progress_ctx = NULL;

void littlefs_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_littlefs_mutex);
}

void notify_progress(void) {
  if (s_progress_callback != NULL) {
    s_progress_callback(s_progress_ctx);
  }
}

bool validate_non_empty(const char *value, const char *fn, const char *name) {
  if (value == NULL || value[0] == '\0') {
    hal_derr("%s: %s is NULL/empty", fn, name);
    return false;
  }
  return true;
}

bool range_in_partition(lfs_block_t block, lfs_off_t off, lfs_size_t size,
                        uint32_t *partition_offset) {
  if (!s_cfg_ready || partition_offset == NULL ||
      block >= s_lfs_cfg.block_count || off > s_lfs_cfg.block_size ||
      size > s_lfs_cfg.block_size - off) {
    return false;
  }
  *partition_offset = (uint32_t)block * s_lfs_cfg.block_size + off;
  return true;
}

int rp_lfs_read(const struct lfs_config *config, lfs_block_t block,
                lfs_off_t off, void *buffer, lfs_size_t size) {
  (void)config;
  uint32_t partition_offset = 0u;
  if (buffer == NULL ||
      !range_in_partition(block, off, size, &partition_offset)) {
    return LFS_ERR_IO;
  }
  return jh_rp_flash_storage_read(&s_partition, partition_offset, buffer,
                                  size) == HAL_OK
             ? LFS_ERR_OK
             : LFS_ERR_IO;
}

int rp_lfs_prog(const struct lfs_config *config, lfs_block_t block,
                lfs_off_t off, const void *buffer, lfs_size_t size) {
  (void)config;
  uint32_t partition_offset = 0u;
  if (buffer == NULL || (off % kProgSize) != 0u || (size % kProgSize) != 0u ||
      !range_in_partition(block, off, size, &partition_offset)) {
    return LFS_ERR_IO;
  }
  const hal_status_t status =
      jh_rp_flash_storage_program(&s_partition, partition_offset, buffer, size);
  notify_progress();
  return status == HAL_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

int rp_lfs_erase(const struct lfs_config *config, lfs_block_t block) {
  (void)config;
  uint32_t partition_offset = 0u;
  if (!range_in_partition(block, 0u, kBlockSize, &partition_offset)) {
    return LFS_ERR_IO;
  }
  const hal_status_t status =
      jh_rp_flash_storage_erase(&s_partition, partition_offset, kBlockSize);
  notify_progress();
  return status == HAL_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

int rp_lfs_sync(const struct lfs_config *config) {
  (void)config;
  return LFS_ERR_OK;
}

bool littlefs_prepare_config(void) {
  if (s_cfg_ready) {
    return s_lfs_cfg.block_count > 0u;
  }
  if (jh_rp_flash_storage_partition(JH_RP_FLASH_PARTITION_LITTLEFS,
                                    &s_partition) != HAL_OK) {
    return false;
  }

  memset(&s_lfs_cfg, 0, sizeof(s_lfs_cfg));
  s_lfs_cfg.read = rp_lfs_read;
  s_lfs_cfg.prog = rp_lfs_prog;
  s_lfs_cfg.erase = rp_lfs_erase;
  s_lfs_cfg.sync = rp_lfs_sync;
  s_lfs_cfg.read_size = kReadSize;
  s_lfs_cfg.prog_size = kProgSize;
  s_lfs_cfg.block_size = kBlockSize;
  s_lfs_cfg.block_count = s_partition.size / kBlockSize;
  s_lfs_cfg.block_cycles = kBlockCycles;
  s_lfs_cfg.cache_size = kCacheSize;
  s_lfs_cfg.lookahead_size = kLookaheadSize;
  s_lfs_cfg.read_buffer = s_read_buffer;
  s_lfs_cfg.prog_buffer = s_prog_buffer;
  s_lfs_cfg.lookahead_buffer = s_lookahead_buffer;
  s_cfg_ready = true;
  return s_lfs_cfg.block_count > 0u;
}

} // namespace

hal_status_t
hal_littlefs_set_progress_callback(hal_littlefs_progress_callback_t callback,
                                   void *ctx) {
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);
  s_progress_callback = callback;
  s_progress_ctx = ctx;
  hal_mutex_unlock(s_littlefs_mutex);
  return HAL_OK;
}

hal_status_t hal_littlefs_begin_ex(void) {
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);

  hal_status_t status = HAL_EIO;
  if (!littlefs_prepare_config()) {
    status = HAL_ECONFIG;
  } else if (s_littlefs_mounted) {
    status = HAL_OK;
  } else {
    s_littlefs_mounted = lfs_mount(&s_lfs, &s_lfs_cfg) == LFS_ERR_OK;
    status = s_littlefs_mounted ? HAL_OK : HAL_EIO;
  }

  hal_mutex_unlock(s_littlefs_mutex);
  if (status != HAL_OK) {
    hal_derr("hal_littlefs_begin: native RP lfs_mount() failed");
  }
  return status;
}

bool hal_littlefs_begin(void) {
  return hal_status_to_bool(hal_littlefs_begin_ex());
}

hal_status_t hal_littlefs_end(void) {
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);
  int result = LFS_ERR_OK;
  if (s_littlefs_mounted) {
    result = lfs_unmount(&s_lfs);
  }
  s_littlefs_mounted = false;
  hal_mutex_unlock(s_littlefs_mutex);
  return result == LFS_ERR_OK ? HAL_OK : HAL_EIO;
}

hal_status_t hal_littlefs_format_ex(void) {
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);

  hal_status_t status = HAL_EIO;
  const bool was_mounted = s_littlefs_mounted;
  if (!littlefs_prepare_config()) {
    status = HAL_ECONFIG;
  } else {
    int unmount_result = LFS_ERR_OK;
    if (was_mounted) {
      unmount_result = lfs_unmount(&s_lfs);
      s_littlefs_mounted = false;
    }
    if (unmount_result == LFS_ERR_OK &&
        lfs_format(&s_lfs, &s_lfs_cfg) == LFS_ERR_OK) {
      status = HAL_OK;
    } else if (was_mounted) {
      s_littlefs_mounted = lfs_mount(&s_lfs, &s_lfs_cfg) == LFS_ERR_OK;
    }
  }

  hal_mutex_unlock(s_littlefs_mutex);
  if (status != HAL_OK) {
    hal_derr("hal_littlefs_format: native RP lfs_format() failed");
  }
  return status;
}

bool hal_littlefs_format(void) {
  return hal_status_to_bool(hal_littlefs_format_ex());
}

bool hal_littlefs_is_mounted(void) {
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);
  const bool mounted = s_littlefs_mounted;
  hal_mutex_unlock(s_littlefs_mutex);
  return mounted;
}

hal_status_t hal_littlefs_exists_ex(const char *path) {
  if (!validate_non_empty(path, "hal_littlefs_exists", "path")) {
    return HAL_EINVAL;
  }
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);
  if (!s_littlefs_mounted) {
    hal_mutex_unlock(s_littlefs_mutex);
    return HAL_EUNINIT;
  }
  struct lfs_info info;
  const int result = lfs_stat(&s_lfs, path, &info);
  hal_mutex_unlock(s_littlefs_mutex);
  if (result == LFS_ERR_OK) {
    return HAL_OK;
  }
  return result == LFS_ERR_NOENT ? HAL_ENOENT : HAL_EIO;
}

bool hal_littlefs_exists(const char *path) {
  return hal_status_to_bool(hal_littlefs_exists_ex(path));
}

hal_status_t hal_littlefs_remove_ex(const char *path) {
  if (!validate_non_empty(path, "hal_littlefs_remove", "path")) {
    return HAL_EINVAL;
  }
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);
  if (!s_littlefs_mounted) {
    hal_mutex_unlock(s_littlefs_mutex);
    return HAL_EUNINIT;
  }
  const int result = lfs_remove(&s_lfs, path);
  hal_mutex_unlock(s_littlefs_mutex);
  if (result == LFS_ERR_OK) {
    return HAL_OK;
  }
  return result == LFS_ERR_NOENT ? HAL_ENOENT : HAL_EIO;
}

bool hal_littlefs_remove(const char *path) {
  return hal_status_to_bool(hal_littlefs_remove_ex(path));
}

hal_status_t hal_littlefs_total_bytes_ex(size_t *out_bytes) {
  if (out_bytes == NULL) {
    return HAL_EINVAL;
  }
  *out_bytes = 0u;
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);
  if (!s_littlefs_mounted) {
    hal_mutex_unlock(s_littlefs_mutex);
    return HAL_EUNINIT;
  }
  *out_bytes = (size_t)s_lfs_cfg.block_count * s_lfs_cfg.block_size;
  hal_mutex_unlock(s_littlefs_mutex);
  return HAL_OK;
}

size_t hal_littlefs_total_bytes(void) {
  size_t bytes = 0u;
  (void)hal_littlefs_total_bytes_ex(&bytes);
  return bytes;
}

hal_status_t hal_littlefs_used_bytes_ex(size_t *out_bytes) {
  if (out_bytes == NULL) {
    return HAL_EINVAL;
  }
  *out_bytes = 0u;
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);
  if (!s_littlefs_mounted) {
    hal_mutex_unlock(s_littlefs_mutex);
    return HAL_EUNINIT;
  }
  const lfs_ssize_t blocks = lfs_fs_size(&s_lfs);
  if (blocks >= 0) {
    *out_bytes = (size_t)blocks * s_lfs_cfg.block_size;
  }
  hal_mutex_unlock(s_littlefs_mutex);
  return blocks >= 0 ? HAL_OK : HAL_EIO;
}

size_t hal_littlefs_used_bytes(void) {
  size_t bytes = 0u;
  (void)hal_littlefs_used_bytes_ex(&bytes);
  return bytes;
}

#endif /* HAL_ENABLE_LITTLEFS */
#endif // HAL_TARGET_IS_RP
