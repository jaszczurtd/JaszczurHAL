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

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_LITTLEFS */
