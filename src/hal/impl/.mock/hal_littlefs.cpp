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

void hal_littlefs_set_progress_callback(
    hal_littlefs_progress_callback_t callback, void *ctx) {
  s_progress_callback = callback;
  s_progress_ctx = ctx;
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

bool hal_littlefs_begin(void) {
  s_littlefs.mounted = s_littlefs.begin_result;
  return s_littlefs.begin_result;
}

void hal_littlefs_end(void) { s_littlefs.mounted = false; }

bool hal_littlefs_format(void) {
  if (!s_littlefs.format_result) {
    return false;
  }

  notify_progress();
  memset(s_littlefs.path_used, 0, sizeof(s_littlefs.path_used));
  memset(s_littlefs.paths, 0, sizeof(s_littlefs.paths));
  s_littlefs.used_bytes = 0u;
  s_littlefs.mounted = false;
  notify_progress();
  return true;
}

bool hal_littlefs_is_mounted(void) { return s_littlefs.mounted; }

bool hal_littlefs_exists(const char *path) {
  if (!validate_non_empty(path, "hal_littlefs_exists", "path")) {
    return false;
  }
  if (!s_littlefs.mounted) {
    hal_derr("hal_littlefs_exists: filesystem is not mounted");
    return false;
  }

  return find_path_index(path) >= 0;
}

bool hal_littlefs_remove(const char *path) {
  if (!validate_non_empty(path, "hal_littlefs_remove", "path")) {
    return false;
  }
  if (!s_littlefs.mounted) {
    hal_derr("hal_littlefs_remove: filesystem is not mounted");
    return false;
  }

  const int idx = find_path_index(path);
  if (idx < 0) {
    return false;
  }

  s_littlefs.path_used[idx] = false;
  s_littlefs.paths[idx][0] = '\0';
  return true;
}

size_t hal_littlefs_total_bytes(void) {
  if (!s_littlefs.mounted) {
    return 0u;
  }
  return s_littlefs.total_bytes;
}

size_t hal_littlefs_used_bytes(void) {
  if (!s_littlefs.mounted) {
    return 0u;
  }
  return s_littlefs.used_bytes;
}

#endif /* HAL_ENABLE_LITTLEFS */
#endif // HAL_TARGET_IS_MOCK
