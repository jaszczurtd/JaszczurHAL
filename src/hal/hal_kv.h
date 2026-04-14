#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifndef HAL_DISABLE_KV

/**
 * @file hal_kv.h
 * @brief Thread-safe append-only KV/record storage on top of hal_eeprom.
 */

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
 * Storage uses two banks within [base_addr, base_addr + size_bytes) and keeps
 * records append-only with periodic compaction (GC).
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
bool hal_kv_get_blob(uint16_t key, uint8_t *out, uint16_t out_size, uint16_t *out_len);

/** @brief Delete key from store. */
bool hal_kv_delete(uint16_t key);

/** @brief Force compaction into the alternate bank. */
bool hal_kv_gc(void);

/** @brief Return runtime statistics of active KV bank. */
bool hal_kv_get_stats(hal_kv_stats_t *out_stats);


#endif /* HAL_DISABLE_KV */
#ifdef __cplusplus
}
#endif
