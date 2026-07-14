#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"

#ifdef HAL_ENABLE_LITTLEFS

#include "../../hal_littlefs.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "../shared/hal_mutex_once.h"

#include <FS.h>
#include <LittleFS.h>
#include <stdio.h>

static hal_mutex_t s_littlefs_mutex = NULL;
static bool s_littlefs_mounted = false;
static hal_littlefs_progress_callback_t s_progress_callback = NULL;
static void *s_progress_ctx = NULL;

static inline void littlefs_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_littlefs_mutex);
}

static bool validate_non_empty(const char *value, const char *fn,
                               const char *name) {
  if (!value || value[0] == '\0') {
    hal_derr("%s: %s is NULL/empty", fn, name);
    return false;
  }
  return true;
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

  const bool ok = LittleFS.begin();
  s_littlefs_mounted = ok;

  hal_mutex_unlock(s_littlefs_mutex);

  if (!ok) {
    hal_derr("hal_littlefs_begin: LittleFS.begin() failed");
  }
  return ok ? HAL_OK : HAL_EIO;
}

bool hal_littlefs_begin(void) {
  return hal_status_to_bool(hal_littlefs_begin_ex());
}

hal_status_t hal_littlefs_end(void) {
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);

  if (s_littlefs_mounted) {
    LittleFS.end();
  }
  s_littlefs_mounted = false;

  hal_mutex_unlock(s_littlefs_mutex);
  return HAL_OK;
}

hal_status_t hal_littlefs_format_ex(void) {
  littlefs_ensure_mutex();
  hal_mutex_lock(s_littlefs_mutex);

  if (s_progress_callback != NULL) {
    s_progress_callback(s_progress_ctx);
  }
  const bool ok = LittleFS.format();
  if (s_progress_callback != NULL) {
    s_progress_callback(s_progress_ctx);
  }
  if (ok) {
    s_littlefs_mounted = false;
  }

  hal_mutex_unlock(s_littlefs_mutex);

  if (!ok) {
    hal_derr("hal_littlefs_format: LittleFS.format() failed");
  }
  return ok ? HAL_OK : HAL_EIO;
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

  const bool exists = LittleFS.exists(path);

  hal_mutex_unlock(s_littlefs_mutex);
  return exists ? HAL_OK : HAL_ENOENT;
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

  if (!LittleFS.exists(path)) {
    hal_mutex_unlock(s_littlefs_mutex);
    return HAL_ENOENT;
  }

  const bool ok = LittleFS.remove(path);

  hal_mutex_unlock(s_littlefs_mutex);
  return ok ? HAL_OK : HAL_EIO;
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
  FSInfo info{};
  const bool ok = LittleFS.info(info);
  if (ok) {
    *out_bytes = (size_t)info.totalBytes;
  }

  hal_mutex_unlock(s_littlefs_mutex);
  return ok ? HAL_OK : HAL_EIO;
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
  FSInfo info{};
  const bool ok = LittleFS.info(info);
  if (ok) {
    *out_bytes = (size_t)info.usedBytes;
  }

  hal_mutex_unlock(s_littlefs_mutex);
  return ok ? HAL_OK : HAL_EIO;
}

size_t hal_littlefs_used_bytes(void) {
  size_t bytes = 0u;
  (void)hal_littlefs_used_bytes_ex(&bytes);
  return bytes;
}

#endif /* HAL_ENABLE_LITTLEFS */
#endif // HAL_TARGET_IS_RP2040
