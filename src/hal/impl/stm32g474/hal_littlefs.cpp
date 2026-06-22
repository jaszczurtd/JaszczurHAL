#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474
#include "../../hal_config.h"

#ifdef HAL_ENABLE_LITTLEFS

#include "../../hal_littlefs.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "../shared/hal_mutex_once.h"
#include "drivers/littlefs/lfs.h"
#include "drivers/stm32g474/stm32g474_flash.h"

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

void hal_littlefs_set_progress_callback(
    hal_littlefs_progress_callback_t callback, void *ctx) {
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);
  s_progress_callback = callback;
  s_progress_ctx = ctx;
  hal_mutex_unlock(s_littlefs_mutex);
}

bool hal_littlefs_begin(void) {
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);

  bool ok = false;
  if (!littlefs_prepare_config()) {
    hal_derr("hal_littlefs_begin: STM32 LittleFS flash partition is empty");
  } else if (s_littlefs_mounted) {
    ok = true;
  } else {
    ok = (lfs_mount(&s_lfs, &s_lfs_cfg) == LFS_ERR_OK);
    s_littlefs_mounted = ok;
  }

  hal_mutex_unlock(s_littlefs_mutex);

  if (!ok) {
    hal_derr("hal_littlefs_begin: lfs_mount() failed");
  }
  return ok;
}

void hal_littlefs_end(void) {
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);

  if (s_littlefs_mounted) {
    (void)lfs_unmount(&s_lfs);
  }
  s_littlefs_mounted = false;

  hal_mutex_unlock(s_littlefs_mutex);
}

bool hal_littlefs_format(void) {
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);

  bool ok = false;
  const bool was_mounted = s_littlefs_mounted;
  if (!littlefs_prepare_config()) {
    hal_derr("hal_littlefs_format: STM32 LittleFS flash partition is empty");
  } else {
    if (was_mounted) {
      (void)lfs_unmount(&s_lfs);
      s_littlefs_mounted = false;
    }

    ok = (lfs_format(&s_lfs, &s_lfs_cfg) == LFS_ERR_OK);
    if (!ok && was_mounted) {
      s_littlefs_mounted = (lfs_mount(&s_lfs, &s_lfs_cfg) == LFS_ERR_OK);
    }
  }

  hal_mutex_unlock(s_littlefs_mutex);

  if (!ok) {
    hal_derr("hal_littlefs_format: lfs_format() failed");
  }
  return ok;
}

bool hal_littlefs_is_mounted(void) {
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);

  const bool mounted = s_littlefs_mounted;

  hal_mutex_unlock(s_littlefs_mutex);
  return mounted;
}

bool hal_littlefs_exists(const char *path) {
  if (!validate_non_empty(path, "hal_littlefs_exists", "path")) {
    return false;
  }

  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);

  if (!s_littlefs_mounted) {
    hal_mutex_unlock(s_littlefs_mutex);
    hal_derr("hal_littlefs_exists: filesystem is not mounted");
    return false;
  }

  struct lfs_info info;
  const bool exists = (lfs_stat(&s_lfs, path, &info) == LFS_ERR_OK);

  hal_mutex_unlock(s_littlefs_mutex);
  return exists;
}

bool hal_littlefs_remove(const char *path) {
  if (!validate_non_empty(path, "hal_littlefs_remove", "path")) {
    return false;
  }

  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);

  if (!s_littlefs_mounted) {
    hal_mutex_unlock(s_littlefs_mutex);
    hal_derr("hal_littlefs_remove: filesystem is not mounted");
    return false;
  }

  const bool ok = (lfs_remove(&s_lfs, path) == LFS_ERR_OK);

  hal_mutex_unlock(s_littlefs_mutex);
  return ok;
}

size_t hal_littlefs_total_bytes(void) {
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);

  size_t total = 0u;
  if (s_littlefs_mounted) {
    total = (size_t)s_lfs_cfg.block_count * (size_t)s_lfs_cfg.block_size;
  }

  hal_mutex_unlock(s_littlefs_mutex);
  return total;
}

size_t hal_littlefs_used_bytes(void) {
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);

  size_t used = 0u;
  if (s_littlefs_mounted) {
    const lfs_ssize_t blocks = lfs_fs_size(&s_lfs);
    if (blocks > 0) {
      used = (size_t)blocks * (size_t)s_lfs_cfg.block_size;
    }
  }

  hal_mutex_unlock(s_littlefs_mutex);
  return used;
}

#endif /* HAL_ENABLE_LITTLEFS */
#endif /* HAL_TARGET_IS_STM32G474 */
