#include "jh_process_support.h"
#include "jh_json_file.h"
#include <jb/filesystem.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <pwd.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

static hal_status_t wide_text(const wchar_t *wide, jb_string_t *out) {
  if (wide == NULL)
    return HAL_OK;
  const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                                        NULL, 0, NULL, NULL);
  hal_status_t status =
      count > 0 ? jb_string_resize(out, (size_t)count - 1, '\0') : HAL_EPROTO;
  if (status == HAL_OK && count > 1 &&
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                          jb_string_mutable_data(out), count, NULL,
                          NULL) != count)
    status = HAL_EPROTO;
  return status;
}
#endif

hal_status_t jh_process_environment(const char *name, jb_string_t *out,
                                    bool *found) {
  if (name == NULL || out == NULL || found == NULL)
    return HAL_EINVAL;
#ifdef _WIN32
  const size_t length = strlen(name);
  if (length > (SIZE_MAX / sizeof(wchar_t)) - 1)
    return HAL_EOVERFLOW;
  wchar_t *key = malloc((length + 1) * sizeof *key);
  if (key == NULL)
    return HAL_ENOMEM;
  for (size_t i = 0; i <= length; ++i) {
    if ((unsigned char)name[i] >= 128) {
      free(key);
      return HAL_EINVAL;
    }
    key[i] = (wchar_t)name[i];
  }
  const wchar_t *value = _wgetenv(key);
  *found = value != NULL;
  const hal_status_t status = wide_text(value != NULL ? value : L"", out);
  free(key);
  return status;
#else
  const char *value = getenv(name);
  *found = value != NULL;
  return jb_string_assign(out, value != NULL ? value : "",
                          value != NULL ? strlen(value) : 0);
#endif
}

hal_status_t jh_process_user_home(const char *user, jb_string_t *out,
                                  bool *found, jh_failure_t *failure) {
  if (user == NULL || out == NULL || found == NULL || failure == NULL)
    return jh_fail_arguments(failure);
  *found = false;
  hal_status_t status = HAL_OK;
#ifdef _WIN32
  status = jh_process_environment("USERPROFILE", out, found);
  jb_string_t auxiliary = {0}, parent = {0};
  (void)jb_string_init(&auxiliary, jb_heap_allocator());
  (void)jb_string_init(&parent, jb_heap_allocator());
  if (status == HAL_OK && !*found) {
    status = jh_process_environment("HOMEPATH", &auxiliary, found);
    if (status == HAL_OK && *found) {
      bool drive_found = false;
      status = jh_process_environment("HOMEDRIVE", out, &drive_found);
      if (status == HAL_OK)
        status = jb_string_append(out, jb_string_data(&auxiliary),
                                  jb_string_size(&auxiliary));
    }
  }
  if (status == HAL_OK && *found && *user) {
    bool user_found = false;
    status = jh_process_environment("USERNAME", &auxiliary, &user_found);
    const char *name = jb_string_data(out);
    for (const char *p = name; *p; ++p)
      if (*p == '/' || *p == '\\')
        name = p + 1;
    if (!user_found || strcmp(name, jb_string_data(&auxiliary)) != 0)
      *found = false;
    else if (status == HAL_OK) {
      status = jh_path_parent(jb_string_data(out), &parent, failure);
      if (status == HAL_OK)
        status = jh_path_join(jb_string_data(&parent), user, out);
    }
  }
  jb_string_destroy(&parent);
  jb_string_destroy(&auxiliary);
#else
  if (!*user)
    status = jh_process_environment("HOME", out, found);
  if (status == HAL_OK && !*found) {
    const struct passwd *entry = *user ? getpwnam(user) : getpwuid(getuid());
    if (entry != NULL) {
      status = jb_string_assign(out, entry->pw_dir, strlen(entry->pw_dir));
      *found = status == HAL_OK;
    }
  }
#endif
  if (status != HAL_OK && failure->status == HAL_OK)
    status = jh_fail_storage(failure, status);
  return status;
}

hal_status_t jh_process_executable(const char *name, const char *base,
                                   bool absolute_entries_only, jb_string_t *out,
                                   jh_failure_t *failure) {
  if (name == NULL || *name == '\0' || base == NULL || out == NULL ||
      failure == NULL)
    return jh_fail_arguments(failure);
  bool absolute = false;
  hal_status_t status = jb_fs_path_is_absolute(name, &absolute);
  if (status != HAL_OK)
    return jh_fail_storage(failure, status);
  if (absolute || strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
    status = absolute_entries_only
                 ? jb_string_assign(out, name, strlen(name))
                 : jh_unresolved_path(name, base, out, failure);
    if (status != HAL_OK && failure->status == HAL_OK)
      status = jh_fail_storage(failure, status);
    return status;
  }
  jb_string_t environment = {0}, candidate = {0}, executable = {0},
              directory = {0};
  (void)jb_string_init(&environment, jb_heap_allocator());
  (void)jb_string_init(&candidate, jb_heap_allocator());
  (void)jb_string_init(&executable, jb_heap_allocator());
  (void)jb_string_init(&directory, jb_heap_allocator());
  status = jh_append(&executable, name);
  bool path_set = false;
  if (status == HAL_OK)
    status = jh_process_environment("PATH", &environment, &path_set);
#ifdef _WIN32
  const char separator = ';';
  const size_t length = strlen(name);
  if (status == HAL_OK &&
      (length < 4 || _stricmp(name + length - 4, ".exe") != 0))
    status = jh_append(&executable, ".exe");
#else
  if (status == HAL_OK && !path_set && !absolute_entries_only)
    status = jh_append(&environment, "/bin:/usr/bin");
  const char separator = ':';
#endif
  jb_json_span_t remaining = jh_string_span(&environment);
  bool found = false;
  while (status == HAL_OK) {
    const char *end = memchr(remaining.data, separator, remaining.length);
    jb_json_span_t entry = {remaining.data, end != NULL
                                                ? (size_t)(end - remaining.data)
                                                : remaining.length};
#ifdef _WIN32
    if (entry.length >= 2 && entry.data[0] == '"' &&
        entry.data[entry.length - 1] == '"') {
      ++entry.data;
      entry.length -= 2;
    }
#endif
    status = jb_string_assign(&candidate, entry.data, entry.length);
    bool use = true;
    if (status == HAL_OK && absolute_entries_only)
      use = entry.length != 0 &&
            jb_fs_path_is_absolute(jb_string_data(&candidate), &absolute) ==
                HAL_OK &&
            absolute;
    if (status == HAL_OK && use)
      status = jh_unresolved_path(jb_string_data(&candidate), base, &directory,
                                  failure);
    if (status == HAL_OK && use)
      status =
          jh_unresolved_path(jb_string_data(&executable),
                             jb_string_data(&directory), &candidate, failure);
    jb_fs_info_t info;
    bool executable_file =
        status == HAL_OK && use &&
        jb_fs_status(jb_string_data(&candidate), true, &info) == HAL_OK &&
        info.kind == JB_FS_REGULAR;
#ifndef _WIN32
    if (executable_file && !absolute_entries_only)
      executable_file = access(jb_string_data(&candidate), X_OK) == 0;
#endif
    if (executable_file) {
      found = true;
      status = jb_string_assign(out, jb_string_data(&candidate),
                                jb_string_size(&candidate));
      break;
    }
    if (end == NULL)
      break;
    const size_t consumed = (size_t)(end - remaining.data) + 1;
    remaining.data += consumed;
    remaining.length -= consumed;
  }
  if (status == HAL_OK && !found)
    status = absolute_entries_only
                 ? jb_string_assign(out, name, strlen(name))
                 : jh_fail(failure, HAL_ENOENT, 3,
                           JH_PARTS(jh_span("executable not found: "),
                                    jh_span(name)));
  if (status != HAL_OK && failure->status == HAL_OK)
    status = jh_fail_storage(failure, status);
  jb_string_destroy(&executable);
  jb_string_destroy(&directory);
  jb_string_destroy(&candidate);
  jb_string_destroy(&environment);
  return status;
}

hal_status_t jh_process_scratch(jb_temp_dir_t **out, jh_failure_t *failure) {
  if (out == NULL || *out != NULL || failure == NULL)
    return jh_fail_arguments(failure);
  jb_string_t parent = {0};
  (void)jb_string_init(&parent, jb_heap_allocator());
  hal_status_t status = HAL_OK;
#ifdef _WIN32
  const DWORD capacity = GetTempPathW(0, NULL);
  wchar_t *wide =
      capacity != 0 ? calloc((size_t)capacity + 1, sizeof *wide) : NULL;
  if (wide == NULL)
    status = capacity == 0 ? HAL_EIO : HAL_ENOMEM;
  if (status == HAL_OK) {
    const DWORD length = GetTempPathW(capacity + 1, wide);
    status =
        length == 0 || length > capacity ? HAL_EIO : wide_text(wide, &parent);
  }
  free(wide);
#else
  const char *tmp = getenv("TMPDIR");
  status = jh_working_directory(&parent, failure);
  if (status == HAL_OK)
    status = jh_absolute_path(tmp != NULL && *tmp != '\0' ? tmp : "/tmp",
                              jb_string_data(&parent), &parent, failure);
#endif
  if (status == HAL_OK)
    status = jb_temp_dir_create(jb_string_data(&parent), out);
#ifndef _WIN32
  if (status != HAL_OK && status != HAL_ENOMEM && status != HAL_EOVERFLOW &&
      failure->status == HAL_OK && tmp != NULL && *tmp != '\0' &&
      strcmp(tmp, "/tmp") != 0) {
    status = jb_string_assign(&parent, "/tmp", 4);
    if (status == HAL_OK)
      status = jb_temp_dir_create(jb_string_data(&parent), out);
  }
#endif
  if (status != HAL_OK && failure->status == HAL_OK)
    status = jh_fail(failure, status, status == HAL_EOVERFLOW ? 4 : 3,
                     JH_PARTS(jh_string_span(&parent),
                              jh_span(": cannot create scratch directory: "),
                              jh_span(hal_status_to_string(status))));
  jb_string_destroy(&parent);
  return status;
}
