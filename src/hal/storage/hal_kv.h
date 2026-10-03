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
  uint32_t generation;     /**< Generation of the active bank; grows with every
                                compaction into the other bank. */
  uint16_t used_bytes;     /**< Active-bank bytes in use, log included. */
  uint16_t capacity_bytes; /**< Size of one bank. */
  uint16_t key_count;
  uint16_t
      key_capacity; /**< Distinct keys the index holds (HAL_KV_MAX_KEYS). */
  uint32_t next_sequence;
  bool spare_erased; /**< The other bank is erased, so the next compaction
                          needs no erase (see hal_kv_prepare_ex()). */
} hal_kv_stats_t;

/**
 * @brief Initialize KV storage inside a selected EEPROM address range.
 *
 * Storage splits [base_addr, base_addr + size_bytes) into two equal banks.
 * The active bank holds a compacted body and, after it, a log in its erased
 * tail. A commit programs the new records and a closing commit record (CRC of
 * the batch) into the tail without erasing anything; on flash that is one or
 * two page programs. When the tail runs out, the live records are compacted
 * into the other bank, whose header is written only after the complete body
 * has been written and verified; that step erases the other bank unless
 * hal_kv_prepare_ex() already did. Startup validates both banks, selects the
 * newest valid generation and replays the committed log batches; a batch cut
 * short by a power loss has no valid commit record and is ignored.
 *
 * Flash-backed banks must start and end on independently erasable boundaries.
 * RP builds therefore reserve at least two 4096-byte sectors, while the
 * STM32G474 default uses two 2048-byte pages. Non-flash providers expose the
 * same publication behavior over two non-overlapping logical regions.
 * Initialization checks the selected provider's erase/program geometry before
 * reading any bank or attempting a write, including when a valid bank exists.
 * An incompatible layout returns HAL_EINVAL from hal_kv_init_ex() and false
 * from hal_kv_init().
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

/**
 * @brief Erase the inactive bank ahead of the next compaction.
 *
 * Ordinary commits only program the active bank's erased tail. A compaction,
 * needed once that tail is full, writes the other bank; when that bank is
 * still erased it is only programmed. Erasing takes long (on RP flash about
 * 50 ms per 4 KiB sector with both cores stopped), so call this when such a
 * pause is harmless. Returns at once when the bank is already erased.
 *
 * @return HAL_OK when the inactive bank is erased afterwards, HAL_EUNINIT
 *         before hal_kv_init_ex(), HAL_ENOMEM when the module mutex cannot be
 *         created, or the EEPROM erase status (for example HAL_EIO, or the
 *         flash write callback's refusal).
 */
hal_status_t hal_kv_prepare_ex(void);

/** @brief Return runtime statistics of active KV bank. */
bool hal_kv_get_stats(hal_kv_stats_t *out_stats);

/**
 * @brief Switch the KV store between auto-commit and deferred-commit modes.
 *
 * By default every logical mutation is committed at once as a log batch of
 * its own. Switching to deferred mode (`enabled = false`) lets a caller
 * coalesce several mutations into one batch, which becomes visible after a
 * power loss only as a whole, by calling hal_kv_commit() at the end.
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
 * Commits the staged records when dirty: appends them as one log batch, or
 * compacts into the other bank when the log has no room or a previous write
 * failed. This also retries a commit that previously failed in auto-commit
 * mode.
 *
 * @return true on success or if nothing was dirty.
 */
bool hal_kv_commit(void);

/**
 * @brief Switch KV reads between RAM-cache (default) and read-through modes.
 *
 * The active bank is fully cached in RAM after hal_kv_init_ex() and after
 * every commit, so by default hal_kv_get_u32()/hal_kv_get_blob() never touch
 * the backing EEPROM: they are fast and immune to spurious media errors, but
 * a storage fault that develops *after* init (not caught at init or at the
 * next write) is invisible to a plain get.
 *
 * Enabling read-through (`enabled = true`) makes every get additionally
 * re-read the record's bytes live from the backing EEPROM before returning,
 * at the cost of one EEPROM read per get. A live medium fault then surfaces
 * as a real hal_status_t error from the get call instead of being served
 * from the (still valid) RAM copy. Callers that gate writes or other
 * decisions on "is storage currently healthy" should enable this; callers
 * that only care about the last successfully committed values should leave
 * it at the default.
 *
 * Read-through cannot address records in a dirty RAM image because their
 * offsets point at storage that has not been written yet. In that state,
 * get operations return HAL_EBUSY (and the bool wrappers return false).
 * Call hal_kv_commit_ex() first, or disable read-through to read the staged
 * values from RAM.
 *
 * The mode is a KV-wide setting, not per-call; it persists across
 * hal_kv_init_ex() the same way hal_kv_set_auto_commit() does.
 *
 * @param enabled true to read-verify against EEPROM on every get, false
 *                (default) to serve purely from the RAM cache.
 * @return HAL_OK, or HAL_ENOMEM if the module mutex cannot be created.
 */
hal_status_t hal_kv_set_read_through(bool enabled);

/**
 * @brief Check whether a candidate address holds a structurally valid KV
 *        bank header, without initializing or otherwise touching the active
 *        KV instance.
 *
 * Reads and validates only the publish header (magic, version, sizes, and
 * the header's own CRC) at @p bank_addr; it does not validate the bank body
 * and does not require hal_kv_init_ex() to have been called. Intended for
 * callers that must choose between multiple candidate base addresses (for
 * example detecting a bank left behind at a previous storage location by an
 * older firmware version) before calling hal_kv_init_ex() on the winner.
 *
 * This is the supported alternative to a caller hand-decoding hal_kv's
 * on-disk header layout: that layout is a private implementation detail and
 * may change between versions (it did between format versions 1 and 2).
 *
 * @param bank_addr   EEPROM address of the candidate bank.
 * @param bank_size   Expected bank size in bytes; a header whose own
 *                    recorded bank_size differs is reported absent.
 * @param out_present Set to true when a structurally valid header is found
 *                    at @p bank_addr, false otherwise (including on a read
 *                    error, in which case the return status explains why).
 * @return HAL_OK when the check completed (regardless of the outcome in
 *         @p out_present), HAL_EINVAL for a NULL @p out_present or a
 *         @p bank_size too small to hold a header, or an EEPROM I/O status.
 */
hal_status_t hal_kv_bank_looks_present_ex(uint16_t bank_addr,
                                          uint16_t bank_size,
                                          bool *out_present);

/** @brief Compatibility wrapper over hal_kv_bank_looks_present_ex(). */
bool hal_kv_bank_looks_present(uint16_t bank_addr, uint16_t bank_size);

/* ---- Status-returning APIs ---------------------------------------------- */
/*
 * Status-returning KV APIs own validation and EEPROM I/O. The historical bool
 * entry points are compatibility wrappers; the _ex variants return
 * hal_status_t so callers can
 * distinguish invalid arguments (HAL_EINVAL), a read miss (HAL_ENOENT), a
 * caller buffer too small for a stored blob (HAL_EOVERFLOW), statistics on a
 * store that is not ready (HAL_EUNINIT) and backend write/commit failures
 * (HAL_EIO). Read-through getters also report HAL_EBUSY while the RAM image
 * contains unpublished changes. The legacy bool API cannot separate an
 * uninitialised store from a genuine miss; the status API reports them as
 * HAL_EUNINIT and HAL_ENOENT.
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
