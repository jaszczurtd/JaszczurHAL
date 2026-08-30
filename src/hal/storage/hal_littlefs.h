#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_LITTLEFS

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file hal_littlefs.h
 * @brief Thread-safe facade for LittleFS filesystem lifecycle helpers.
 */

#include "hal/core/hal_status.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Optional callback invoked during long filesystem flash operations.
 *
 * Configure the callback before concurrent access. It runs while the shared
 * LittleFS facade owns its mutex and therefore must not call any
 * hal_littlefs_* API, including the callback setter and mounted-state query.
 * Keep it short: feed an application-owned watchdog, update a counter, or set
 * a flag. The number of calls per operation is backend-dependent.
 * A callback may run during an operation that later reports failure; it is a
 * liveness/progress notification, not a success signal.
 */
typedef void (*hal_littlefs_progress_callback_t)(void *ctx);

/** @brief Register an optional callback for long LittleFS operations.
 *  @return HAL_OK when stored, HAL_ENOMEM when locking cannot be initialized,
 *          HAL_ECONFIG when no complete provider is available, or a provider
 *          error status.
 */
hal_status_t
hal_littlefs_set_progress_callback(hal_littlefs_progress_callback_t callback,
                                   void *ctx);

/** @brief Mount LittleFS.
 *  @return true on successful mount.
 */
bool hal_littlefs_begin(void);

/** @brief Unmount LittleFS.
 *
 *  The facade clears its mounted state even when the backend reports an
 *  unmount failure.
 *  @return HAL_OK, HAL_ENOMEM when locking cannot be initialized,
 *          HAL_ECONFIG when no complete provider is available, or HAL_EIO
 *          when the backend cannot unmount it.
 */
hal_status_t hal_littlefs_end(void);

/** @brief Destructively format the LittleFS partition.
 *
 *  Success leaves the filesystem unmounted. After an unmount/format failure
 *  on a previously mounted filesystem, the facade attempts one best-effort
 *  remount. Flash may already be partially modified; data preservation is not
 *  guaranteed.
 *  @return true on successful format.
 */
bool hal_littlefs_format(void);

/** @brief Return true when filesystem is mounted. */
bool hal_littlefs_is_mounted(void);

/** @brief Check whether a path exists.
 *  @param path Null-terminated path string.
 *  @return true when path exists and filesystem is mounted.
 */
bool hal_littlefs_exists(const char *path);

/** @brief Remove file at path.
 *  @param path Null-terminated path string.
 *  @return true when file was removed.
 */
bool hal_littlefs_remove(const char *path);

/** @brief Total filesystem size in bytes (0 when unmounted/unknown). */
size_t hal_littlefs_total_bytes(void);

/** @brief Used filesystem size in bytes (0 when unmounted/unknown). */
size_t hal_littlefs_used_bytes(void);

/* ---- Status-returning APIs ---------------------------------------------- */
/*
 * Status-returning LittleFS APIs own validation and backend I/O. Historical
 * bool/value entry points are compatibility wrappers; the _ex variants return
 * hal_status_t so callers can
 * distinguish a NULL/empty path or NULL output (HAL_EINVAL), unavailable or
 * invalid provider geometry (HAL_ECONFIG), lock allocation failure
 * (HAL_ENOMEM), an operation issued while unmounted (HAL_EUNINIT), a path
 * that does not exist (HAL_ENOENT), backend I/O failure (HAL_EIO), and a size
 * that cannot be represented (HAL_EOVERFLOW). Byte-count queries expose their
 * result through an output parameter. The plain state query
 * hal_littlefs_is_mounted() has no _ex form.
 */
hal_status_t hal_littlefs_begin_ex(void);
hal_status_t hal_littlefs_format_ex(void);
hal_status_t hal_littlefs_exists_ex(const char *path);
hal_status_t hal_littlefs_remove_ex(const char *path);
hal_status_t hal_littlefs_total_bytes_ex(size_t *out_bytes);
hal_status_t hal_littlefs_used_bytes_ex(size_t *out_bytes);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_LITTLEFS */
