#pragma once

#include "hal/storage/hal_littlefs.h"

#ifdef HAL_ENABLE_LITTLEFS

#include "hal/core/hal_status.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t read_size;
  uint32_t prog_size;
  uint32_t block_size;
  uint32_t block_count;
  int32_t block_cycles;
  uint32_t cache_size;
  uint32_t lookahead_size;
  void *read_buffer;
  void *prog_buffer;
  void *lookahead_buffer;
} jh_littlefs_geometry_t;

typedef struct {
  void *context;
  hal_status_t (*prepare)(void *context, jh_littlefs_geometry_t *out_geometry);
  hal_status_t (*read)(void *context, size_t offset, void *buffer, size_t size);
  hal_status_t (*program)(void *context, size_t offset, const void *buffer,
                          size_t size,
                          hal_littlefs_progress_callback_t progress,
                          void *progress_ctx);
  hal_status_t (*erase)(void *context, size_t offset, size_t size,
                        hal_littlefs_progress_callback_t progress,
                        void *progress_ctx);
  hal_status_t (*sync)(void *context);
} jh_littlefs_block_backend_t;

typedef struct {
  hal_status_t (*set_progress_callback)(
      void *context, hal_littlefs_progress_callback_t callback,
      void *callback_ctx);
  hal_status_t (*mount)(void *context);
  hal_status_t (*unmount)(void *context);
  hal_status_t (*format)(void *context);
  hal_status_t (*exists)(void *context, const char *path);
  hal_status_t (*remove)(void *context, const char *path);
  hal_status_t (*total_bytes)(void *context, size_t *out_bytes);
  hal_status_t (*used_bytes)(void *context, size_t *out_bytes);
} jh_littlefs_provider_ops_t;

typedef struct {
  const jh_littlefs_provider_ops_t *ops;
  void *context;
} jh_littlefs_provider_t;

/** Return the provider selected by the active target. */
const jh_littlefs_provider_t *jh_littlefs_provider_get(void);

/** Bind a target block backend to the shared littlefs provider. */
const jh_littlefs_provider_t *
jh_littlefs_lfs_provider_configure(const jh_littlefs_block_backend_t *backend);

/** Shared checked block adapters used by littlefs and direct provider tests. */
hal_status_t jh_littlefs_block_read(const jh_littlefs_block_backend_t *backend,
                                    const jh_littlefs_geometry_t *geometry,
                                    uint32_t block, uint32_t offset,
                                    void *buffer, size_t size);
hal_status_t jh_littlefs_block_program(
    const jh_littlefs_block_backend_t *backend,
    const jh_littlefs_geometry_t *geometry, uint32_t block, uint32_t offset,
    const void *buffer, size_t size, hal_littlefs_progress_callback_t progress,
    void *progress_ctx);
hal_status_t jh_littlefs_block_erase(const jh_littlefs_block_backend_t *backend,
                                     const jh_littlefs_geometry_t *geometry,
                                     uint32_t block,
                                     hal_littlefs_progress_callback_t progress,
                                     void *progress_ctx);
hal_status_t jh_littlefs_block_sync(const jh_littlefs_block_backend_t *backend);

/** Reset shared facade state after resetting the mock provider. */
void jh_littlefs_mock_reset_facade(void);

#endif /* HAL_ENABLE_LITTLEFS */
