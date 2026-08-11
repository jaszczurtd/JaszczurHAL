#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474
#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_LITTLEFS

#include "drivers/stm32g474/stm32g474_flash.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/serial/hal_serial.h"
#include "hal/storage/hal_littlefs.h"
#include "hal/system/hal_sync.h"

#include <lfs.h>
#include <stdint.h>
#include <string.h>

extern "C" {
extern const uint8_t __hal_stm32_littlefs_flash_start[];
extern const uint8_t __hal_stm32_littlefs_flash_end[];
}

static constexpr lfs_size_t LITTLEFS_READ_SIZE = 8u;
static constexpr lfs_size_t LITTLEFS_PROG_SIZE = 8u;
static constexpr lfs_size_t LITTLEFS_CACHE_SIZE = 64u;
static constexpr lfs_size_t LITTLEFS_LOOKAHEAD_SIZE = 16u;
static constexpr int32_t LITTLEFS_BLOCK_CYCLES = 500;

static lfs_t s_lfs;
static struct lfs_config s_lfs_cfg;
static uint8_t s_read_buffer[LITTLEFS_CACHE_SIZE];
static uint8_t s_prog_buffer[LITTLEFS_CACHE_SIZE];
static uint8_t s_lookahead_buffer[LITTLEFS_LOOKAHEAD_SIZE];
static uintptr_t s_flash_start = 0u;
static uint32_t s_flash_size = 0u;
static bool s_cfg_ready = false;
static bool s_littlefs_mounted = false;
static hal_mutex_t s_littlefs_mutex = NULL;
static hal_littlefs_progress_callback_t s_progress_callback = NULL;
static void *s_progress_ctx = NULL;

static void littlefs_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_littlefs_mutex);
}

static void notify_progress(void) {
  if (s_progress_callback != NULL) {
    s_progress_callback(s_progress_ctx);
  }
}

static bool validate_non_empty(const char *value, const char *fn,
                               const char *name) {
  if (!value || value[0] == '\0') {
    hal_derr("%s: %s is NULL/empty", fn, name);
    return false;
  }
  return true;
}

static bool range_in_partition(lfs_block_t block, lfs_off_t off,
                               lfs_size_t size, uintptr_t *address) {
  if (!s_cfg_ready || s_lfs_cfg.block_size == 0u ||
      block >= s_lfs_cfg.block_count || off > s_lfs_cfg.block_size ||
      size > (s_lfs_cfg.block_size - off)) {
    return false;
  }

  *address = s_flash_start + ((uintptr_t)block * s_lfs_cfg.block_size) + off;
  return true;
}

static int stm32_lfs_read(const struct lfs_config *c, lfs_block_t block,
                          lfs_off_t off, void *buffer, lfs_size_t size) {
  (void)c;
  uintptr_t address = 0u;
  if (!buffer || !range_in_partition(block, off, size, &address)) {
    return LFS_ERR_IO;
  }

  memcpy(buffer, (const void *)address, size);
  return LFS_ERR_OK;
}

static int stm32_lfs_prog(const struct lfs_config *c, lfs_block_t block,
                          lfs_off_t off, const void *buffer, lfs_size_t size) {
  (void)c;
  uintptr_t address = 0u;
  if (!buffer || (off % LITTLEFS_PROG_SIZE) != 0u ||
      (size % LITTLEFS_PROG_SIZE) != 0u ||
      !range_in_partition(block, off, size, &address)) {
    return LFS_ERR_IO;
  }

  const uint8_t *src = (const uint8_t *)buffer;
  if (!jh_stm32g474_flash_unlock()) {
    return LFS_ERR_IO;
  }

  bool ok = true;
  for (lfs_size_t i = 0u; i < size; i += LITTLEFS_PROG_SIZE) {
    if (!jh_stm32g474_flash_program_doubleword(address + i, src + i)) {
      ok = false;
      break;
    }
    notify_progress();
  }

  jh_stm32g474_flash_lock();
  return ok ? LFS_ERR_OK : LFS_ERR_IO;
}

static int stm32_lfs_erase(const struct lfs_config *c, lfs_block_t block) {
  (void)c;
  uintptr_t address = 0u;
  if (!range_in_partition(block, 0u, s_lfs_cfg.block_size, &address)) {
    return LFS_ERR_IO;
  }

  if (!jh_stm32g474_flash_unlock()) {
    return LFS_ERR_IO;
  }

  const bool ok = jh_stm32g474_flash_erase_page(address);
  jh_stm32g474_flash_lock();
  notify_progress();
  return ok ? LFS_ERR_OK : LFS_ERR_IO;
}

static int stm32_lfs_sync(const struct lfs_config *c) {
  (void)c;
  return jh_stm32g474_flash_wait_ready() ? LFS_ERR_OK : LFS_ERR_IO;
}

static bool littlefs_prepare_config(void) {
  if (s_cfg_ready) {
    return s_lfs_cfg.block_count > 0u;
  }

  s_flash_start = (uintptr_t)&__hal_stm32_littlefs_flash_start[0];
  const uintptr_t flash_end = (uintptr_t)&__hal_stm32_littlefs_flash_end[0];
  s_flash_size =
      (flash_end > s_flash_start) ? (uint32_t)(flash_end - s_flash_start) : 0u;

  memset(&s_lfs_cfg, 0, sizeof(s_lfs_cfg));
  s_lfs_cfg.read = stm32_lfs_read;
  s_lfs_cfg.prog = stm32_lfs_prog;
  s_lfs_cfg.erase = stm32_lfs_erase;
  s_lfs_cfg.sync = stm32_lfs_sync;
  s_lfs_cfg.read_size = LITTLEFS_READ_SIZE;
  s_lfs_cfg.prog_size = LITTLEFS_PROG_SIZE;
  s_lfs_cfg.block_size = HAL_STM32_FLASH_PAGE_SIZE;
  s_lfs_cfg.block_count = s_flash_size / HAL_STM32_FLASH_PAGE_SIZE;
  s_lfs_cfg.block_cycles = LITTLEFS_BLOCK_CYCLES;
  s_lfs_cfg.cache_size = LITTLEFS_CACHE_SIZE;
  s_lfs_cfg.lookahead_size = LITTLEFS_LOOKAHEAD_SIZE;
  s_lfs_cfg.read_buffer = s_read_buffer;
  s_lfs_cfg.prog_buffer = s_prog_buffer;
  s_lfs_cfg.lookahead_buffer = s_lookahead_buffer;
  s_cfg_ready = true;

  return s_lfs_cfg.block_count > 0u;
}

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
    hal_derr("hal_littlefs_begin: STM32 LittleFS flash partition is empty");
    status = HAL_ECONFIG;
  } else if (s_littlefs_mounted) {
    status = HAL_OK;
  } else {
    const int rc = lfs_mount(&s_lfs, &s_lfs_cfg);
    s_littlefs_mounted = (rc == LFS_ERR_OK);
    status = s_littlefs_mounted ? HAL_OK : HAL_EIO;
  }

  hal_mutex_unlock(s_littlefs_mutex);

  if (hal_status_is_error(status)) {
    hal_derr("hal_littlefs_begin: lfs_mount() failed");
  }
  return status;
}

bool hal_littlefs_begin(void) {
  return hal_status_to_bool(hal_littlefs_begin_ex());
}

hal_status_t hal_littlefs_end(void) {
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);

  int rc = LFS_ERR_OK;
  if (s_littlefs_mounted) {
    rc = lfs_unmount(&s_lfs);
  }
  s_littlefs_mounted = false;

  hal_mutex_unlock(s_littlefs_mutex);
  return rc == LFS_ERR_OK ? HAL_OK : HAL_EIO;
}

hal_status_t hal_littlefs_format_ex(void) {
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);

  hal_status_t status = HAL_EIO;
  const bool was_mounted = s_littlefs_mounted;
  if (!littlefs_prepare_config()) {
    hal_derr("hal_littlefs_format: STM32 LittleFS flash partition is empty");
    status = HAL_ECONFIG;
  } else {
    int unmount_rc = LFS_ERR_OK;
    if (was_mounted) {
      unmount_rc = lfs_unmount(&s_lfs);
      s_littlefs_mounted = false;
    }

    if (unmount_rc == LFS_ERR_OK &&
        lfs_format(&s_lfs, &s_lfs_cfg) == LFS_ERR_OK) {
      status = HAL_OK;
    } else if (was_mounted) {
      s_littlefs_mounted = (lfs_mount(&s_lfs, &s_lfs_cfg) == LFS_ERR_OK);
    }
  }

  hal_mutex_unlock(s_littlefs_mutex);

  if (hal_status_is_error(status)) {
    hal_derr("hal_littlefs_format: lfs_format() failed");
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
    hal_derr("hal_littlefs_exists: filesystem is not mounted");
    return HAL_EUNINIT;
  }

  struct lfs_info info;
  const int rc = lfs_stat(&s_lfs, path, &info);

  hal_mutex_unlock(s_littlefs_mutex);
  if (rc == LFS_ERR_OK) {
    return HAL_OK;
  }
  return rc == LFS_ERR_NOENT ? HAL_ENOENT : HAL_EIO;
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
    hal_derr("hal_littlefs_remove: filesystem is not mounted");
    return HAL_EUNINIT;
  }

  const int rc = lfs_remove(&s_lfs, path);

  hal_mutex_unlock(s_littlefs_mutex);
  if (rc == LFS_ERR_OK) {
    return HAL_OK;
  }
  return rc == LFS_ERR_NOENT ? HAL_ENOENT : HAL_EIO;
}

bool hal_littlefs_remove(const char *path) {
  return hal_status_to_bool(hal_littlefs_remove_ex(path));
}

hal_status_t hal_littlefs_total_bytes_ex(size_t *out_bytes) {
  if (!out_bytes) {
    return HAL_EINVAL;
  }
  *out_bytes = 0u;
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);

  if (!s_littlefs_mounted) {
    hal_mutex_unlock(s_littlefs_mutex);
    return HAL_EUNINIT;
  }
  *out_bytes = (size_t)s_lfs_cfg.block_count * (size_t)s_lfs_cfg.block_size;

  hal_mutex_unlock(s_littlefs_mutex);
  return HAL_OK;
}

size_t hal_littlefs_total_bytes(void) {
  size_t bytes = 0u;
  (void)hal_littlefs_total_bytes_ex(&bytes);
  return bytes;
}

hal_status_t hal_littlefs_used_bytes_ex(size_t *out_bytes) {
  if (!out_bytes) {
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
    *out_bytes = (size_t)blocks * (size_t)s_lfs_cfg.block_size;
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
#endif /* HAL_TARGET_IS_STM32G474 */
