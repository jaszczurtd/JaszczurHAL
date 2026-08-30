#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_LITTLEFS

#include "hal/storage/jh_littlefs_provider.h"

#include <lfs.h>

#include <limits.h>
#include <string.h>

namespace {

struct lfs_provider_state_t {
  const jh_littlefs_block_backend_t *backend;
  jh_littlefs_geometry_t geometry;
  lfs_t lfs;
  struct lfs_config config;
  bool config_ready;
  hal_littlefs_progress_callback_t progress;
  void *progress_ctx;
};

lfs_provider_state_t s_state = {};

bool backend_is_complete(const jh_littlefs_block_backend_t *backend) {
  return backend != nullptr && backend->prepare != nullptr &&
         backend->read != nullptr && backend->program != nullptr &&
         backend->erase != nullptr && backend->sync != nullptr;
}

bool geometry_is_valid(const jh_littlefs_geometry_t *geometry) {
  if (geometry == nullptr || geometry->read_size == 0u ||
      geometry->prog_size == 0u || geometry->block_size == 0u ||
      geometry->block_count < 2u || geometry->cache_size == 0u ||
      geometry->lookahead_size == 0u || geometry->read_buffer == nullptr ||
      geometry->prog_buffer == nullptr ||
      geometry->lookahead_buffer == nullptr || geometry->block_size < 128u ||
      geometry->block_cycles == 0) {
    return false;
  }
  if ((geometry->block_size % geometry->read_size) != 0u ||
      (geometry->block_size % geometry->prog_size) != 0u ||
      (geometry->cache_size % geometry->read_size) != 0u ||
      (geometry->cache_size % geometry->prog_size) != 0u ||
      (geometry->block_size % geometry->cache_size) != 0u ||
      (geometry->lookahead_size % 8u) != 0u) {
    return false;
  }
  const uint64_t partition_size =
      (uint64_t)geometry->block_size * geometry->block_count;
  return partition_size <= SIZE_MAX;
}

hal_status_t checked_offset(const jh_littlefs_geometry_t *geometry,
                            uint32_t block, uint32_t offset, size_t size,
                            size_t *out_offset) {
  if (geometry == nullptr || out_offset == nullptr ||
      geometry->block_size == 0u || block >= geometry->block_count ||
      offset > geometry->block_size ||
      size > (size_t)(geometry->block_size - offset)) {
    return HAL_EOVERFLOW;
  }
  const uint64_t absolute = (uint64_t)block * geometry->block_size + offset;
  if (absolute > SIZE_MAX) {
    return HAL_EOVERFLOW;
  }
  *out_offset = (size_t)absolute;
  return HAL_OK;
}

int lfs_read_adapter(const struct lfs_config *config, lfs_block_t block,
                     lfs_off_t offset, void *buffer, lfs_size_t size) {
  auto *state = static_cast<lfs_provider_state_t *>(config->context);
  return jh_littlefs_block_read(state->backend, &state->geometry, block, offset,
                                buffer, size) == HAL_OK
             ? LFS_ERR_OK
             : LFS_ERR_IO;
}

int lfs_program_adapter(const struct lfs_config *config, lfs_block_t block,
                        lfs_off_t offset, const void *buffer, lfs_size_t size) {
  auto *state = static_cast<lfs_provider_state_t *>(config->context);
  return jh_littlefs_block_program(state->backend, &state->geometry, block,
                                   offset, buffer, size, state->progress,
                                   state->progress_ctx) == HAL_OK
             ? LFS_ERR_OK
             : LFS_ERR_IO;
}

int lfs_erase_adapter(const struct lfs_config *config, lfs_block_t block) {
  auto *state = static_cast<lfs_provider_state_t *>(config->context);
  return jh_littlefs_block_erase(state->backend, &state->geometry, block,
                                 state->progress, state->progress_ctx) == HAL_OK
             ? LFS_ERR_OK
             : LFS_ERR_IO;
}

int lfs_sync_adapter(const struct lfs_config *config) {
  auto *state = static_cast<lfs_provider_state_t *>(config->context);
  return jh_littlefs_block_sync(state->backend) == HAL_OK ? LFS_ERR_OK
                                                          : LFS_ERR_IO;
}

hal_status_t prepare_config(lfs_provider_state_t *state) {
  if (state->config_ready) {
    return HAL_OK;
  }

  jh_littlefs_geometry_t geometry = {};
  const hal_status_t status =
      state->backend->prepare(state->backend->context, &geometry);
  if (status != HAL_OK) {
    return status;
  }
  if (!geometry_is_valid(&geometry)) {
    return HAL_ECONFIG;
  }

  state->geometry = geometry;
  memset(&state->config, 0, sizeof(state->config));
  state->config.context = state;
  state->config.read = lfs_read_adapter;
  state->config.prog = lfs_program_adapter;
  state->config.erase = lfs_erase_adapter;
  state->config.sync = lfs_sync_adapter;
  state->config.read_size = geometry.read_size;
  state->config.prog_size = geometry.prog_size;
  state->config.block_size = geometry.block_size;
  state->config.block_count = geometry.block_count;
  state->config.block_cycles = geometry.block_cycles;
  state->config.cache_size = geometry.cache_size;
  state->config.lookahead_size = geometry.lookahead_size;
  state->config.read_buffer = geometry.read_buffer;
  state->config.prog_buffer = geometry.prog_buffer;
  state->config.lookahead_buffer = geometry.lookahead_buffer;
  state->config_ready = true;
  return HAL_OK;
}

hal_status_t set_progress_callback(void *context,
                                   hal_littlefs_progress_callback_t callback,
                                   void *callback_ctx) {
  auto *state = static_cast<lfs_provider_state_t *>(context);
  state->progress = callback;
  state->progress_ctx = callback_ctx;
  return HAL_OK;
}

hal_status_t mount(void *context) {
  auto *state = static_cast<lfs_provider_state_t *>(context);
  const hal_status_t status = prepare_config(state);
  if (status != HAL_OK) {
    return status;
  }
  return lfs_mount(&state->lfs, &state->config) == LFS_ERR_OK ? HAL_OK
                                                              : HAL_EIO;
}

hal_status_t unmount(void *context) {
  auto *state = static_cast<lfs_provider_state_t *>(context);
  return lfs_unmount(&state->lfs) == LFS_ERR_OK ? HAL_OK : HAL_EIO;
}

hal_status_t format(void *context) {
  auto *state = static_cast<lfs_provider_state_t *>(context);
  const hal_status_t status = prepare_config(state);
  if (status != HAL_OK) {
    return status;
  }
  return lfs_format(&state->lfs, &state->config) == LFS_ERR_OK ? HAL_OK
                                                               : HAL_EIO;
}

hal_status_t exists(void *context, const char *path) {
  auto *state = static_cast<lfs_provider_state_t *>(context);
  struct lfs_info info = {};
  const int result = lfs_stat(&state->lfs, path, &info);
  if (result == LFS_ERR_OK) {
    return HAL_OK;
  }
  return result == LFS_ERR_NOENT ? HAL_ENOENT : HAL_EIO;
}

hal_status_t remove(void *context, const char *path) {
  auto *state = static_cast<lfs_provider_state_t *>(context);
  const int result = lfs_remove(&state->lfs, path);
  if (result == LFS_ERR_OK) {
    return HAL_OK;
  }
  return result == LFS_ERR_NOENT ? HAL_ENOENT : HAL_EIO;
}

hal_status_t total_bytes(void *context, size_t *out_bytes) {
  auto *state = static_cast<lfs_provider_state_t *>(context);
  const uint64_t total =
      (uint64_t)state->geometry.block_count * state->geometry.block_size;
  if (total > SIZE_MAX) {
    return HAL_EOVERFLOW;
  }
  *out_bytes = (size_t)total;
  return HAL_OK;
}

hal_status_t used_bytes(void *context, size_t *out_bytes) {
  auto *state = static_cast<lfs_provider_state_t *>(context);
  const lfs_ssize_t blocks = lfs_fs_size(&state->lfs);
  if (blocks < 0) {
    return HAL_EIO;
  }
  const uint64_t used = (uint64_t)blocks * state->geometry.block_size;
  if (used > SIZE_MAX) {
    return HAL_EOVERFLOW;
  }
  *out_bytes = (size_t)used;
  return HAL_OK;
}

const jh_littlefs_provider_ops_t kProviderOps = {
    set_progress_callback, mount,     unmount, format, exists, remove,
    total_bytes,           used_bytes};

const jh_littlefs_provider_t kProvider = {&kProviderOps, &s_state};

} // namespace

hal_status_t jh_littlefs_block_read(const jh_littlefs_block_backend_t *backend,
                                    const jh_littlefs_geometry_t *geometry,
                                    uint32_t block, uint32_t offset,
                                    void *buffer, size_t size) {
  if (!backend_is_complete(backend) || geometry == nullptr ||
      buffer == nullptr || geometry->read_size == 0u ||
      (offset % geometry->read_size) != 0u ||
      (size % geometry->read_size) != 0u) {
    return HAL_EINVAL;
  }
  size_t absolute = 0u;
  const hal_status_t status =
      checked_offset(geometry, block, offset, size, &absolute);
  return status == HAL_OK
             ? backend->read(backend->context, absolute, buffer, size)
             : status;
}

hal_status_t jh_littlefs_block_program(
    const jh_littlefs_block_backend_t *backend,
    const jh_littlefs_geometry_t *geometry, uint32_t block, uint32_t offset,
    const void *buffer, size_t size, hal_littlefs_progress_callback_t progress,
    void *progress_ctx) {
  if (!backend_is_complete(backend) || geometry == nullptr ||
      buffer == nullptr || geometry->prog_size == 0u ||
      (offset % geometry->prog_size) != 0u ||
      (size % geometry->prog_size) != 0u) {
    return HAL_EINVAL;
  }
  size_t absolute = 0u;
  const hal_status_t status =
      checked_offset(geometry, block, offset, size, &absolute);
  return status == HAL_OK ? backend->program(backend->context, absolute, buffer,
                                             size, progress, progress_ctx)
                          : status;
}

hal_status_t jh_littlefs_block_erase(const jh_littlefs_block_backend_t *backend,
                                     const jh_littlefs_geometry_t *geometry,
                                     uint32_t block,
                                     hal_littlefs_progress_callback_t progress,
                                     void *progress_ctx) {
  if (!backend_is_complete(backend) || geometry == nullptr) {
    return HAL_EINVAL;
  }
  size_t absolute = 0u;
  const hal_status_t status =
      checked_offset(geometry, block, 0u, geometry->block_size, &absolute);
  return status == HAL_OK
             ? backend->erase(backend->context, absolute, geometry->block_size,
                              progress, progress_ctx)
             : status;
}

hal_status_t
jh_littlefs_block_sync(const jh_littlefs_block_backend_t *backend) {
  return backend_is_complete(backend) ? backend->sync(backend->context)
                                      : HAL_EINVAL;
}

const jh_littlefs_provider_t *
jh_littlefs_lfs_provider_configure(const jh_littlefs_block_backend_t *backend) {
  if (!backend_is_complete(backend)) {
    return nullptr;
  }
  if (s_state.backend != nullptr && s_state.backend != backend) {
    return nullptr;
  }
  s_state.backend = backend;
  return &kProvider;
}

#endif /* HAL_ENABLE_LITTLEFS */
