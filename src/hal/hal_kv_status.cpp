#include "hal_kv.h"

#ifdef HAL_ENABLE_KV

/*
 * Backend-agnostic status adapter for the KV store. Each wrapper validates the
 * arguments it can check locally (returning HAL_EINVAL before touching the
 * backend), then delegates to the legacy entry point and maps a residual
 * failure to a representative status code:
 *
 *   - read misses (missing key / wrong type / not ready)   -> HAL_ENOENT,
 *   - a caller buffer too small for a stored blob           -> HAL_EOVERFLOW,
 *   - statistics on a store that is not ready               -> HAL_EUNINIT,
 *   - write/compaction/commit backend failures              -> HAL_EIO.
 *
 * Because the legacy bool API does not distinguish an uninitialised store from
 * a genuine miss, HAL_ENOENT covers both for the read helpers.
 */

hal_status_t hal_kv_init_ex(uint16_t base_addr, uint16_t size_bytes) {
  return hal_status_from_bool(hal_kv_init(base_addr, size_bytes), HAL_EIO);
}

hal_status_t hal_kv_set_u32_ex(uint16_t key, uint32_t value) {
  return hal_status_from_bool(hal_kv_set_u32(key, value), HAL_EIO);
}

hal_status_t hal_kv_get_u32_ex(uint16_t key, uint32_t *out_value) {
  if (out_value == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_kv_get_u32(key, out_value), HAL_ENOENT);
}

hal_status_t hal_kv_set_blob_ex(uint16_t key, const uint8_t *data,
                                uint16_t len) {
  if (len > 0u && data == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_kv_set_blob(key, data, len), HAL_EIO);
}

hal_status_t hal_kv_get_blob_ex(uint16_t key, uint8_t *out, uint16_t out_size,
                                uint16_t *out_len) {
  /* Length-only query: no destination buffer to validate. */
  if (out == nullptr) {
    return hal_status_from_bool(hal_kv_get_blob(key, nullptr, 0u, out_len),
                                HAL_ENOENT);
  }

  /* Probe the stored length first so a too-small buffer reports HAL_EOVERFLOW
   * instead of being mistaken for a missing key. */
  uint16_t stored_len = 0u;
  if (!hal_kv_get_blob(key, nullptr, 0u, &stored_len)) {
    return HAL_ENOENT;
  }
  if (out_len != nullptr) {
    *out_len = stored_len;
  }
  if (out_size < stored_len) {
    return HAL_EOVERFLOW;
  }
  return hal_status_from_bool(hal_kv_get_blob(key, out, out_size, out_len),
                              HAL_EIO);
}

hal_status_t hal_kv_delete_ex(uint16_t key) {
  return hal_status_from_bool(hal_kv_delete(key), HAL_EIO);
}

hal_status_t hal_kv_gc_ex(void) {
  return hal_status_from_bool(hal_kv_gc(), HAL_EIO);
}

hal_status_t hal_kv_get_stats_ex(hal_kv_stats_t *out_stats) {
  if (out_stats == nullptr) {
    return HAL_EINVAL;
  }
  return hal_status_from_bool(hal_kv_get_stats(out_stats), HAL_EUNINIT);
}

hal_status_t hal_kv_set_auto_commit_ex(bool enabled) {
  hal_kv_set_auto_commit(enabled);
  return HAL_OK;
}

hal_status_t hal_kv_commit_ex(void) {
  return hal_status_from_bool(hal_kv_commit(), HAL_EIO);
}

#endif /* HAL_ENABLE_KV */
