#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"

#ifdef HAL_ENABLE_LITTLEFS

#include "../../hal_littlefs.h"
#include "../../hal_serial.h"
#include "hal_mock.h"

#include <stdio.h>
#include <string.h>

#define MOCK_LITTLEFS_MAX_PATHS 16u
#define MOCK_LITTLEFS_PATH_BUF_SIZE 96u

static struct {
  bool mounted;
  bool begin_result;
  bool format_result;
  size_t total_bytes;
  size_t used_bytes;

  bool path_used[MOCK_LITTLEFS_MAX_PATHS];
  char paths[MOCK_LITTLEFS_MAX_PATHS][MOCK_LITTLEFS_PATH_BUF_SIZE];
} s_littlefs;

static hal_littlefs_progress_callback_t s_progress_callback = NULL;
static void *s_progress_ctx = NULL;

static bool validate_non_empty(const char *value, const char *fn,
                               const char *name) {
  if (!value || value[0] == '\0') {
    hal_derr("%s: %s is NULL/empty", fn, name);
    return false;
  }
  return true;
}

static int find_path_index(const char *path) {
  for (uint8_t i = 0u; i < MOCK_LITTLEFS_MAX_PATHS; ++i) {
    if (s_littlefs.path_used[i] && strcmp(s_littlefs.paths[i], path) == 0) {
      return (int)i;
    }
  }
  return -1;
}

static int find_free_index(void) {
  for (uint8_t i = 0u; i < MOCK_LITTLEFS_MAX_PATHS; ++i) {
    if (!s_littlefs.path_used[i]) {
      return (int)i;
    }
  }
  return -1;
}

static void notify_progress(void) {
  if (s_progress_callback != NULL) {
    s_progress_callback(s_progress_ctx);
  }
}

hal_status_t
hal_littlefs_set_progress_callback(hal_littlefs_progress_callback_t callback,
                                   void *ctx) {
  s_progress_callback = callback;
  s_progress_ctx = ctx;
  return HAL_OK;
}

void hal_mock_littlefs_reset(void) {
  memset(&s_littlefs, 0, sizeof(s_littlefs));
  s_littlefs.begin_result = true;
  s_littlefs.format_result = true;
  s_littlefs.total_bytes = 2u * 1024u * 1024u;
  s_progress_callback = NULL;
  s_progress_ctx = NULL;
}

void hal_mock_littlefs_set_begin_result(bool result) {
  s_littlefs.begin_result = result;
}

void hal_mock_littlefs_set_format_result(bool result) {
  s_littlefs.format_result = result;
}

void hal_mock_littlefs_set_total_bytes(size_t total_bytes) {
  s_littlefs.total_bytes = total_bytes;
  if (s_littlefs.used_bytes > s_littlefs.total_bytes) {
    s_littlefs.used_bytes = s_littlefs.total_bytes;
  }
}

void hal_mock_littlefs_set_used_bytes(size_t used_bytes) {
  s_littlefs.used_bytes = used_bytes;
  if (s_littlefs.used_bytes > s_littlefs.total_bytes) {
    s_littlefs.used_bytes = s_littlefs.total_bytes;
  }
}

void hal_mock_littlefs_set_exists(const char *path, bool exists) {
  if (!path || path[0] == '\0') {
    return;
  }

  const int existing_idx = find_path_index(path);
  if (exists) {
    if (existing_idx >= 0) {
      return;
    }

    const int free_idx = find_free_index();
    if (free_idx < 0) {
      return;
    }

    s_littlefs.path_used[free_idx] = true;
    snprintf(s_littlefs.paths[free_idx], sizeof(s_littlefs.paths[free_idx]),
             "%s", path);
    return;
  }

  if (existing_idx >= 0) {
    s_littlefs.path_used[existing_idx] = false;
    s_littlefs.paths[existing_idx][0] = '\0';
  }
}

hal_status_t hal_littlefs_begin_ex(void) {
  s_littlefs.mounted = s_littlefs.begin_result;
  return s_littlefs.begin_result ? HAL_OK : HAL_EIO;
}

bool hal_littlefs_begin(void) {
  return hal_status_to_bool(hal_littlefs_begin_ex());
}

hal_status_t hal_littlefs_end(void) {
  s_littlefs.mounted = false;
  return HAL_OK;
}

hal_status_t hal_littlefs_format_ex(void) {
  if (!s_littlefs.format_result) {
    return HAL_EIO;
  }

  notify_progress();
  memset(s_littlefs.path_used, 0, sizeof(s_littlefs.path_used));
  memset(s_littlefs.paths, 0, sizeof(s_littlefs.paths));
  s_littlefs.used_bytes = 0u;
  s_littlefs.mounted = false;
  notify_progress();
  return HAL_OK;
}

bool hal_littlefs_format(void) {
  return hal_status_to_bool(hal_littlefs_format_ex());
}

bool hal_littlefs_is_mounted(void) { return s_littlefs.mounted; }

hal_status_t hal_littlefs_exists_ex(const char *path) {
  if (!validate_non_empty(path, "hal_littlefs_exists", "path")) {
    return HAL_EINVAL;
  }
  if (!s_littlefs.mounted) {
    hal_derr("hal_littlefs_exists: filesystem is not mounted");
    return HAL_EUNINIT;
  }

  return find_path_index(path) >= 0 ? HAL_OK : HAL_ENOENT;
}

bool hal_littlefs_exists(const char *path) {
  return hal_status_to_bool(hal_littlefs_exists_ex(path));
}

hal_status_t hal_littlefs_remove_ex(const char *path) {
  if (!validate_non_empty(path, "hal_littlefs_remove", "path")) {
    return HAL_EINVAL;
  }
  if (!s_littlefs.mounted) {
    hal_derr("hal_littlefs_remove: filesystem is not mounted");
    return HAL_EUNINIT;
  }

  const int idx = find_path_index(path);
  if (idx < 0) {
    return HAL_ENOENT;
  }

  s_littlefs.path_used[idx] = false;
  s_littlefs.paths[idx][0] = '\0';
  return HAL_OK;
}

bool hal_littlefs_remove(const char *path) {
  return hal_status_to_bool(hal_littlefs_remove_ex(path));
}

hal_status_t hal_littlefs_total_bytes_ex(size_t *out_bytes) {
  if (!out_bytes) {
    return HAL_EINVAL;
  }
  *out_bytes = 0u;
  if (!s_littlefs.mounted) {
    return HAL_EUNINIT;
  }
  *out_bytes = s_littlefs.total_bytes;
  return HAL_OK;
}

size_t hal_littlefs_total_bytes(void) {
  size_t bytes = 0u;
  (void)hal_littlefs_total_bytes_ex(&bytes);
  return bytes;
}

hal_status_t hal_littlefs_used_bytes_ex(size_t *out_bytes) {
  if (!out_bytes) {
    return HAL_EINVAL;
  }
  *out_bytes = 0u;
  if (!s_littlefs.mounted) {
    return HAL_EUNINIT;
  }
  *out_bytes = s_littlefs.used_bytes;
  return HAL_OK;
}

size_t hal_littlefs_used_bytes(void) {
  size_t bytes = 0u;
  (void)hal_littlefs_used_bytes_ex(&bytes);
  return bytes;
}

#endif /* HAL_ENABLE_LITTLEFS */
#endif // HAL_TARGET_IS_MOCK
