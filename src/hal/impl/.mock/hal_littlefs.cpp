#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_MOCK

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_LITTLEFS

#include "hal/storage/jh_littlefs_provider.h"
#include "hal_mock.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr size_t kMaxPaths = 16u;
constexpr size_t kPathBufferSize = 96u;

struct mock_littlefs_state_t {
  hal_status_t begin_status;
  hal_status_t end_status;
  hal_status_t format_status;
  size_t total_bytes;
  size_t used_bytes;
  bool path_used[kMaxPaths];
  char paths[kMaxPaths][kPathBufferSize];
  hal_littlefs_progress_callback_t progress;
  void *progress_ctx;
};

mock_littlefs_state_t s_state = {};

int find_path_index(const mock_littlefs_state_t *state, const char *path) {
  for (size_t i = 0u; i < kMaxPaths; ++i) {
    if (state->path_used[i] && strcmp(state->paths[i], path) == 0) {
      return (int)i;
    }
  }
  return -1;
}

int find_free_index(const mock_littlefs_state_t *state) {
  for (size_t i = 0u; i < kMaxPaths; ++i) {
    if (!state->path_used[i]) {
      return (int)i;
    }
  }
  return -1;
}

hal_status_t set_progress_callback(void *context,
                                   hal_littlefs_progress_callback_t callback,
                                   void *callback_ctx) {
  auto *state = static_cast<mock_littlefs_state_t *>(context);
  state->progress = callback;
  state->progress_ctx = callback_ctx;
  return HAL_OK;
}

hal_status_t mount(void *context) {
  auto *state = static_cast<mock_littlefs_state_t *>(context);
  return state->begin_status;
}

hal_status_t unmount(void *context) {
  auto *state = static_cast<mock_littlefs_state_t *>(context);
  return state->end_status;
}

hal_status_t format(void *context) {
  auto *state = static_cast<mock_littlefs_state_t *>(context);
  if (state->format_status != HAL_OK) {
    return state->format_status;
  }
  if (state->progress != nullptr) {
    state->progress(state->progress_ctx);
  }
  memset(state->path_used, 0, sizeof(state->path_used));
  memset(state->paths, 0, sizeof(state->paths));
  state->used_bytes = 0u;
  if (state->progress != nullptr) {
    state->progress(state->progress_ctx);
  }
  return HAL_OK;
}

hal_status_t exists(void *context, const char *path) {
  auto *state = static_cast<mock_littlefs_state_t *>(context);
  return find_path_index(state, path) >= 0 ? HAL_OK : HAL_ENOENT;
}

hal_status_t remove(void *context, const char *path) {
  auto *state = static_cast<mock_littlefs_state_t *>(context);
  const int index = find_path_index(state, path);
  if (index < 0) {
    return HAL_ENOENT;
  }
  state->path_used[index] = false;
  state->paths[index][0] = '\0';
  return HAL_OK;
}

hal_status_t total_bytes(void *context, size_t *out_bytes) {
  auto *state = static_cast<mock_littlefs_state_t *>(context);
  *out_bytes = state->total_bytes;
  return HAL_OK;
}

hal_status_t used_bytes(void *context, size_t *out_bytes) {
  auto *state = static_cast<mock_littlefs_state_t *>(context);
  *out_bytes = state->used_bytes;
  return HAL_OK;
}

const jh_littlefs_provider_ops_t kProviderOps = {
    set_progress_callback, mount,     unmount, format, exists, remove,
    total_bytes,           used_bytes};

const jh_littlefs_provider_t kProvider = {&kProviderOps, &s_state};

} // namespace

const jh_littlefs_provider_t *jh_littlefs_provider_get(void) {
  return &kProvider;
}

void hal_mock_littlefs_reset(void) {
  memset(&s_state, 0, sizeof(s_state));
  s_state.begin_status = HAL_OK;
  s_state.end_status = HAL_OK;
  s_state.format_status = HAL_OK;
  s_state.total_bytes = 2u * 1024u * 1024u;
  jh_littlefs_mock_reset_facade();
}

void hal_mock_littlefs_set_begin_result(bool result) {
  s_state.begin_status = result ? HAL_OK : HAL_EIO;
}

void hal_mock_littlefs_set_begin_status(hal_status_t status) {
  s_state.begin_status = status;
}

void hal_mock_littlefs_set_end_result(bool result) {
  s_state.end_status = result ? HAL_OK : HAL_EIO;
}

void hal_mock_littlefs_set_format_result(bool result) {
  s_state.format_status = result ? HAL_OK : HAL_EIO;
}

void hal_mock_littlefs_set_total_bytes(size_t total_bytes_value) {
  s_state.total_bytes = total_bytes_value;
  if (s_state.used_bytes > s_state.total_bytes) {
    s_state.used_bytes = s_state.total_bytes;
  }
}

void hal_mock_littlefs_set_used_bytes(size_t used_bytes_value) {
  s_state.used_bytes = used_bytes_value;
  if (s_state.used_bytes > s_state.total_bytes) {
    s_state.used_bytes = s_state.total_bytes;
  }
}

void hal_mock_littlefs_set_exists(const char *path, bool path_exists) {
  if (path == nullptr || path[0] == '\0') {
    return;
  }

  const int existing_index = find_path_index(&s_state, path);
  if (path_exists) {
    if (existing_index >= 0) {
      return;
    }
    const int free_index = find_free_index(&s_state);
    if (free_index < 0) {
      return;
    }
    s_state.path_used[free_index] = true;
    snprintf(s_state.paths[free_index], sizeof(s_state.paths[free_index]), "%s",
             path);
    return;
  }

  if (existing_index >= 0) {
    s_state.path_used[existing_index] = false;
    s_state.paths[existing_index][0] = '\0';
  }
}

#endif /* HAL_ENABLE_LITTLEFS */
#endif /* HAL_TARGET_IS_MOCK */
