#include "hal_littlefs.h"

#ifdef HAL_ENABLE_LITTLEFS

/*
 * Backend-agnostic status adapter for the LittleFS lifecycle helpers:
 *
 *   - a NULL/empty path or NULL output pointer   -> HAL_EINVAL,
 *   - an operation issued while unmounted         -> HAL_EUNINIT,
 *   - a path lookup/removal that finds nothing    -> HAL_ENOENT,
 *   - a mount/format backend failure              -> HAL_EIO.
 *
 * hal_littlefs_is_mounted() stays a plain boolean state query; it reports
 * status rather than the outcome of a fallible operation, so it has no _ex
 * form (mirrors the SPI async-busy query).
 */

static hal_status_t littlefs_check_path(const char *path) {
  if (path == nullptr || path[0] == '\0') {
    return HAL_EINVAL;
  }
  return hal_littlefs_is_mounted() ? HAL_OK : HAL_EUNINIT;
}

hal_status_t
hal_littlefs_set_progress_callback_ex(hal_littlefs_progress_callback_t callback,
                                      void *ctx) {
  hal_littlefs_set_progress_callback(callback, ctx);
  return HAL_OK;
}

hal_status_t hal_littlefs_begin_ex(void) {
  return hal_status_from_bool(hal_littlefs_begin(), HAL_EIO);
}

hal_status_t hal_littlefs_end_ex(void) {
  hal_littlefs_end();
  return HAL_OK;
}

hal_status_t hal_littlefs_format_ex(void) {
  return hal_status_from_bool(hal_littlefs_format(), HAL_EIO);
}

hal_status_t hal_littlefs_exists_ex(const char *path) {
  const hal_status_t status = littlefs_check_path(path);
  if (hal_status_is_error(status)) {
    return status;
  }
  return hal_littlefs_exists(path) ? HAL_OK : HAL_ENOENT;
}

hal_status_t hal_littlefs_remove_ex(const char *path) {
  const hal_status_t status = littlefs_check_path(path);
  if (hal_status_is_error(status)) {
    return status;
  }
  return hal_status_from_bool(hal_littlefs_remove(path), HAL_ENOENT);
}

hal_status_t hal_littlefs_total_bytes_ex(size_t *out_bytes) {
  if (out_bytes == nullptr) {
    return HAL_EINVAL;
  }
  *out_bytes = hal_littlefs_total_bytes();
  return hal_littlefs_is_mounted() ? HAL_OK : HAL_EUNINIT;
}

hal_status_t hal_littlefs_used_bytes_ex(size_t *out_bytes) {
  if (out_bytes == nullptr) {
    return HAL_EINVAL;
  }
  *out_bytes = hal_littlefs_used_bytes();
  return hal_littlefs_is_mounted() ? HAL_OK : HAL_EUNINIT;
}

#endif /* HAL_ENABLE_LITTLEFS */
