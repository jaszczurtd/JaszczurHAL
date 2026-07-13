#pragma once

#include "hal_config.h"

#ifdef HAL_ENABLE_LITTLEFS

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file hal_littlefs.h
 * @brief Thread-safe wrapper for LittleFS filesystem lifecycle helpers.
 */

#include "hal_status.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Optional callback invoked during long filesystem flash operations.
 *
 * The callback runs while the LittleFS mutex is held. Keep it short: feed an
 * application-owned watchdog, update a counter, or set a flag.
 */
typedef void (*hal_littlefs_progress_callback_t)(void *ctx);

/** @brief Register an optional callback for long LittleFS operations. */
void hal_littlefs_set_progress_callback(
    hal_littlefs_progress_callback_t callback, void *ctx);

/** @brief Mount LittleFS.
 *  @return true on successful mount.
 */
bool hal_littlefs_begin(void);

/** @brief Unmount LittleFS. */
void hal_littlefs_end(void);

/** @brief Format LittleFS partition.
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
 * Status-returning LittleFS APIs. Every legacy entry point above remains a
 * compatibility wrapper; the _ex variants return hal_status_t so callers can
 * distinguish a NULL/empty path or NULL output (HAL_EINVAL), an operation
 * issued while unmounted (HAL_EUNINIT), a path that does not exist
 * (HAL_ENOENT) and a mount/format backend failure (HAL_EIO). Byte-count
 * queries expose their result through an output parameter. The plain state
 * query hal_littlefs_is_mounted() has no _ex form.
 */
hal_status_t
hal_littlefs_set_progress_callback_ex(hal_littlefs_progress_callback_t callback,
                                      void *ctx);
hal_status_t hal_littlefs_begin_ex(void);
hal_status_t hal_littlefs_end_ex(void);
hal_status_t hal_littlefs_format_ex(void);
hal_status_t hal_littlefs_exists_ex(const char *path);
hal_status_t hal_littlefs_remove_ex(const char *path);
hal_status_t hal_littlefs_total_bytes_ex(size_t *out_bytes);
hal_status_t hal_littlefs_used_bytes_ex(size_t *out_bytes);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_LITTLEFS */
