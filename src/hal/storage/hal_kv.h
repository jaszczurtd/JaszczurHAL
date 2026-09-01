#pragma once

#include "hal/core/hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_KV

/**
 * @file hal_kv.h
 * @brief Thread-safe, power-loss-safe KV storage on top of hal_eeprom.
 */

#include "hal/core/hal_status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t generation;
  uint16_t used_bytes;
  uint16_t capacity_bytes;
  uint16_t key_count;
  uint32_t next_sequence;
} hal_kv_stats_t;

/**
 * @brief Initialize KV storage inside a selected EEPROM address range.
 *
 * Storage splits [base_addr, base_addr + size_bytes) into two equal banks.
 * A mutation is assembled in RAM and replaces the inactive bank; its
 * publication header is written only after the complete bank body has been
 * written and verified. Startup validates both complete banks and selects the
 * newest valid generation.
 *
 * Flash-backed banks must start and end on independently erasable boundaries.
 * RP builds therefore reserve at least two 4096-byte sectors, while the
 * STM32G474 default uses two 2048-byte pages. Non-flash providers expose the
 * same publication behavior over two non-overlapping logical regions.
 */
bool hal_kv_init(uint16_t base_addr, uint16_t size_bytes);

/** @brief Store a 32-bit value for key. */
bool hal_kv_set_u32(uint16_t key, uint32_t value);

/** @brief Read a 32-bit value for key. */
bool hal_kv_get_u32(uint16_t key, uint32_t *out_value);

/** @brief Store a binary blob for key. */
bool hal_kv_set_blob(uint16_t key, const uint8_t *data, uint16_t len);

/**
 * @brief Read binary blob for key.
 *
 * If out is NULL, function only returns length via out_len.
 */
bool hal_kv_get_blob(uint16_t key, uint8_t *out, uint16_t out_size,
                     uint16_t *out_len);

/** @brief Delete key from store. */
bool hal_kv_delete(uint16_t key);

/** @brief Compact live records and publish them in the alternate bank. */
bool hal_kv_gc(void);

/** @brief Return runtime statistics of active KV bank. */
bool hal_kv_get_stats(hal_kv_stats_t *out_stats);

/**
 * @brief Switch the KV store between auto-commit and deferred-commit modes.
 *
 * By default every logical mutation publishes one complete inactive bank.
 * Switching to deferred mode (`enabled = false`) lets a caller coalesce
 * several mutations into one bank publication by calling hal_kv_commit() at
 * the end of the batch.
 *
 * Mode change itself does NOT flush pending writes; call hal_kv_commit()
 * explicitly if needed before disabling deferred mode.
 *
 * @param enabled true (default) for auto-commit, false to defer commits.
 * @return HAL_OK, or HAL_ENOMEM if the module mutex cannot be created.
 */
hal_status_t hal_kv_set_auto_commit(bool enabled);

/**
 * @brief Flush pending writes to non-volatile storage.
 *
 * Publishes the staged image when dirty. This also retries a publication that
 * previously failed in auto-commit mode.
 *
 * @return true on success or if nothing was dirty.
 */
bool hal_kv_commit(void);

/* ---- Status-returning APIs ---------------------------------------------- */
/*
 * Status-returning KV APIs own validation and EEPROM I/O. The historical bool
 * entry points are compatibility wrappers; the _ex variants return
 * hal_status_t so callers can
 * distinguish invalid arguments (HAL_EINVAL), a read miss (HAL_ENOENT), a
 * caller buffer too small for a stored blob (HAL_EOVERFLOW), statistics on a
 * store that is not ready (HAL_EUNINIT) and backend write/commit failures
 * (HAL_EIO). The legacy bool API cannot separate an uninitialised store from a
 * genuine miss; the status API reports them as HAL_EUNINIT and HAL_ENOENT.
 */
hal_status_t hal_kv_init_ex(uint16_t base_addr, uint16_t size_bytes);
hal_status_t hal_kv_set_u32_ex(uint16_t key, uint32_t value);
hal_status_t hal_kv_get_u32_ex(uint16_t key, uint32_t *out_value);
hal_status_t hal_kv_set_blob_ex(uint16_t key, const uint8_t *data,
                                uint16_t len);
hal_status_t hal_kv_get_blob_ex(uint16_t key, uint8_t *out, uint16_t out_size,
                                uint16_t *out_len);
hal_status_t hal_kv_delete_ex(uint16_t key);
hal_status_t hal_kv_gc_ex(void);
hal_status_t hal_kv_get_stats_ex(hal_kv_stats_t *out_stats);
hal_status_t hal_kv_commit_ex(void);

#endif /* HAL_ENABLE_KV */
#ifdef __cplusplus
}
#endif
