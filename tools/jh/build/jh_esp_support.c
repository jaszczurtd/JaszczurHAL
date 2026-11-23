/* Strings, paths, files, JSON, regular expressions, environments and child
 * processes of the ESP-IDF backend, with the semantics of the Python runner
 * where its output depends on them. C11. */
#include "jh_build_support.h"
#include "jh_esp_internal.h"
#include "jh_json_check.h"
#include "jh_json_digest.h"
#include "jh_json_file.h"
#include "jh_process_support.h"
#include "jh_project_build.h"
#include "jh_python_repr.h"
#include <jb/filesystem.h>
#include <jb/process.h>
#include <jb/sha256.h>
#include <jb/utf8.h>
#include <stdlib.h>
#include <string.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
extern char **environ;
#endif

#ifdef _WIN32
#define ESP_SEPARATOR "\\"
#define ESP_PATH_LIST ';'
#else
#define ESP_SEPARATOR "/"
#define ESP_PATH_LIST ':'
#endif

bool esp_ok(const jh_esp_t *e) { return e->failure->status == HAL_OK; }

bool esp_storage(jh_esp_t *e, hal_status_t status) {
  if (status != HAL_OK && esp_ok(e))
    (void)jh_fail_storage(e->failure, status);
  return esp_ok(e);
}

bool esp_fail(jh_esp_t *e, const jb_json_span_t *parts, size_t count) {
  if (esp_ok(e))
    (void)jh_fail(e->failure, HAL_ECONFIG, 1, parts, count);
  return false;
}

const char *esp_text(jh_esp_t *e, const jb_json_span_t *parts, size_t count) {
  if (!esp_ok(e) ||
      !esp_storage(e, jh_list_push_parts(&e->strings, parts, count)))
    return "";
  return esp_at(&e->strings, jb_vector_size(&e->strings) - 1);
}

const char *esp_at(const jb_vector_t *list, size_t index) {
  return jb_string_data(jh_list_at(list, index));
}

static const char *own(jh_esp_t *e, jb_string_t *text) {
  const char *result = ESP_TEXT(e, jh_string_span(text));
  jb_string_destroy(text);
  return result;
}

jb_json_span_t esp_repr(jh_esp_t *e, jb_json_span_t text) {
  jb_string_t out = {0};
  if (esp_storage(e, jb_string_init(&out, jb_heap_allocator())))
    (void)esp_storage(e, jh_python_repr_append(&out, text));
  return jh_span(own(e, &out));
}

jb_json_span_t esp_repr_tree(jh_esp_t *e, jb_json_tree_view_t value) {
  jb_string_t out = {0};
  if (value.tree == NULL)
    return jh_span("None");
  if (esp_storage(e, jb_string_init(&out, jb_heap_allocator())))
    (void)esp_storage(e, jh_python_repr_json_append(&out, value));
  return jh_span(own(e, &out));
}

jb_json_span_t esp_repr_list(jh_esp_t *e, const jb_vector_t *list) {
  jb_string_t out = {0};
  hal_status_t status = jb_string_init(&out, jb_heap_allocator());
  jh_put(&out, &status, "[");
  for (size_t i = 0; i < jb_vector_size(list) && status == HAL_OK; ++i) {
    if (i != 0)
      jh_put(&out, &status, ", ");
    if (status == HAL_OK)
      status = jh_python_repr_append(&out, jh_string_span(jh_list_at(list, i)));
  }
  jh_put(&out, &status, "]");
  (void)esp_storage(e, status);
  return jh_span(own(e, &out));
}

const char *esp_decimal(jh_esp_t *e, uint64_t value) {
  char number[32];
  (void)snprintf(number, sizeof number, "%llu", (unsigned long long)value);
  return ESP_TEXT(e, jh_span(number));
}

const char *esp_hex(jh_esp_t *e, uint64_t value) {
  char number[32];
  (void)snprintf(number, sizeof number, "0x%llx", (unsigned long long)value);
  return ESP_TEXT(e, jh_span(number));
}

bool esp_list_init(jh_esp_t *e, jb_vector_t *list) {
  return esp_storage(e, jh_list_init(list));
}

bool esp_push(jh_esp_t *e, jb_vector_t *list, jb_json_span_t text) {
  return esp_ok(e) && esp_storage(e, jh_list_push(list, text));
}

bool esp_push_unique(jh_esp_t *e, jb_vector_t *list, jb_json_span_t text) {
  if (jh_list_contains(list, text))
    return esp_ok(e);
  return esp_push(e, list, text);
}

const char *esp_join_list(jh_esp_t *e, const jb_vector_t *list,
                          const char *separator) {
  jb_string_t out = {0};
  hal_status_t status = jb_string_init(&out, jb_heap_allocator());
  for (size_t i = 0; i < jb_vector_size(list) && status == HAL_OK; ++i) {
    if (i != 0)
      jh_put(&out, &status, separator);
    jh_put_span(&out, &status, jh_string_span(jh_list_at(list, i)));
  }
  (void)esp_storage(e, status);
  return own(e, &out);
}

/* ---- Paths ---- */

const char *esp_expand_user(jh_esp_t *e, const char *path) {
  if (!esp_ok(e) || path[0] != '~')
    return path;
  size_t end = 1;
  while (path[end] != '\0' && !jh_build_separator(path[end]))
    ++end;
  jb_string_t user = {0}, home = {0};
  bool found = false;
  const char *result = path;
  if (esp_storage(e, jb_string_init(&user, jb_heap_allocator())) &&
      esp_storage(e, jb_string_init(&home, jb_heap_allocator())) &&
      esp_storage(e, jb_string_assign(&user, path + 1, end - 1)) &&
      jh_process_user_home(jb_string_data(&user), &home, &found, e->failure) ==
          HAL_OK &&
      found)
    result = ESP_TEXT(e, jh_string_span(&home), jh_span(path + end));
  jb_string_destroy(&user);
  jb_string_destroy(&home);
  return result;
}

bool esp_absolute(const char *path) {
  bool result = false;
  return path[0] != '\0' && jb_fs_path_is_absolute(path, &result) == HAL_OK &&
         result;
}

const char *esp_resolve(jh_esp_t *e, const char *path, const char *base) {
  jb_string_t out = {0};
  if (!esp_ok(e) || !esp_storage(e, jb_string_init(&out, jb_heap_allocator())))
    return "";
  const char *result = "";
  if (jh_python_resolve(path[0] != '\0' ? path : ".", base, &out, e->failure) ==
      HAL_OK)
    result = ESP_TEXT(e, jh_string_span(&out));
  jb_string_destroy(&out);
  return result;
}

const char *esp_join(jh_esp_t *e, const char *base, const char *name) {
  if (esp_absolute(name))
    return ESP_TEXT(e, jh_span(name));
  size_t length = strlen(base);
  while (length > 1 && jh_build_separator(base[length - 1]))
    --length;
  const bool root = length > 0 && jh_build_separator(base[length - 1]);
  return ESP_TEXT(e, ((jb_json_span_t){base, length}),
                  jh_span(root ? "" : ESP_SEPARATOR), jh_span(name));
}

const char *esp_posix(jh_esp_t *e, const char *path) {
  jb_string_t out = {0};
  if (esp_storage(e, jb_string_init(&out, jb_heap_allocator())))
    (void)esp_storage(e, jh_append_posix(&out, jh_span(path)));
  return own(e, &out);
}

bool esp_inside(const char *path, const char *parent) {
  return jh_build_inside(path, parent);
}

const char *esp_relative(jh_esp_t *e, const char *path, const char *parent) {
  size_t length = jh_build_prefix_length(path, parent);
  if (length == SIZE_MAX)
    return NULL;
  while (jh_build_separator(path[length]))
    ++length;
  return path[length] == '\0' ? ESP_TEXT(e, jh_span("."))
                              : esp_posix(e, path + length);
}

const char *esp_parent(jh_esp_t *e, const char *path) {
  jb_string_t out = {0};
  if (esp_storage(e, jb_string_init(&out, jb_heap_allocator())))
    (void)jh_path_parent(path, &out, e->failure);
  return own(e, &out);
}

const char *esp_name(const char *path) {
  const char *name = path;
  for (const char *c = path; *c != '\0'; ++c)
    if (jh_build_separator(*c) && c[1] != '\0')
      name = c + 1;
  return name;
}

static jb_fs_kind_t kind(const char *path) {
  jb_fs_info_t info = {0};
  return jb_fs_status(path, true, &info) == HAL_OK ? info.kind : 0;
}

bool esp_is_file(const char *path) { return kind(path) == JB_FS_REGULAR; }
bool esp_is_dir(const char *path) { return kind(path) == JB_FS_DIRECTORY; }

bool esp_exists(const char *path) {
  jb_fs_info_t info = {0};
  return jb_fs_status(path, true, &info) == HAL_OK;
}

bool esp_file_size(const char *path, uint64_t *size) {
  jb_fs_info_t info = {0};
  if (jb_fs_status(path, true, &info) != HAL_OK || info.kind != JB_FS_REGULAR)
    return false;
  *size = info.size;
  return true;
}

/* ---- Files ---- */

bool esp_read(jh_esp_t *e, const char *path, jb_file_data_t *out) {
  if (!esp_ok(e))
    return false;
  const hal_status_t status = jb_file_read(path, JH_ESP_MAX_BYTES, out);
  if (status != HAL_OK)
    (void)jh_fail(e->failure, status, 1,
                  JH_PARTS(jh_span("cannot read "), jh_span(path),
                           jh_span(": "),
                           jh_span(hal_status_to_string(status))));
  return esp_ok(e);
}

bool esp_write_text(jh_esp_t *e, const char *path, jb_json_span_t content) {
  bool changed = false;
  if (!esp_ok(e))
    return false;
#ifdef _WIN32
  /* Path.write_text() opens the file in text mode: every \n becomes the
   * \r\n of Windows, also one that already follows \r. */
  jb_string_t translated = {0};
  hal_status_t status = jb_string_init(&translated, jb_heap_allocator());
  for (size_t i = 0; i < content.length && status == HAL_OK; ++i)
    status = content.data[i] == '\n'
                 ? jb_string_append(&translated, "\r\n", 2)
                 : jb_string_append(&translated, content.data + i, 1);
  const bool written = esp_storage(e, status) &&
                       jh_project_build_write(path, jh_string_span(&translated),
                                              &changed, e->failure) == HAL_OK;
  jb_string_destroy(&translated);
  return written;
#else
  return jh_project_build_write(path, content, &changed, e->failure) == HAL_OK;
#endif
}

bool esp_sha256_file(jh_esp_t *e, const char *path, char out[65]) {
  jb_file_data_t data = {0};
  if (!esp_read(e, path, &data))
    return false;
  jh_sha256_data_hex(data.data, data.length, out);
  jb_file_release(&data);
  return true;
}

bool esp_remove(jh_esp_t *e, const char *path) {
  const hal_status_t status = jb_fs_remove(path, false);
  if (status != HAL_OK && status != HAL_ENOENT && esp_ok(e))
    (void)jh_fail(e->failure, status, 1,
                  JH_PARTS(jh_span("cannot remove "), jh_span(path),
                           jh_span(": "),
                           jh_span(hal_status_to_string(status))));
  return esp_ok(e);
}

/* ---- JSON ---- */

jb_json_tree_t *esp_load_json(jh_esp_t *e, const char *path, bool object,
                              jb_json_tree_view_t *root) {
  if (!esp_ok(e))
    return NULL;
  jh_json_location_t where;
  jh_json_location_init(&where);
  jh_failure_t error;
  jh_failure_init(&error);
  jb_json_tree_t *tree = NULL;
  if (jh_json_read_value(&where, path, JH_ESP_MAX_BYTES, &tree, &error) !=
      HAL_OK) {
    if (error.status == HAL_ENOMEM)
      (void)jh_fail_storage(e->failure, error.status);
    else
      (void)ESP_FAIL(e, jh_span("Cannot read JSON file "), jh_span(path),
                     jh_span(": "), jh_string_span(&error.message));
  } else if (jb_json_tree_root(tree, root) != HAL_OK ||
             (object && jh_json_type(*root) != JB_JSON_OBJECT)) {
    (void)ESP_FAIL(e, jh_span("JSON file must contain an object: "),
                   jh_span(path));
  }
  jh_failure_destroy(&error);
  jh_json_location_destroy(&where);
  if (!esp_ok(e)) {
    jb_json_tree_destroy(tree);
    return NULL;
  }
  return tree;
}

jb_json_tree_t *esp_parse_text(jh_esp_t *e, jb_json_span_t text,
                               jb_json_tree_view_t *root) {
  const size_t length = text.length;
  const jb_json_limits_t limits = {
      length,     length + 1,        64, length, length + 1,
      length + 1, length * 64 + 4096};
  const jb_json_tree_limits_t tree_limits = {length + 1, 64, length,
                                             length * 64 + 4096};
  jb_json_tree_t *tree = NULL;
  if (esp_storage(
          e, jb_json_tree_parse(text, &limits, &tree_limits, 0, &tree, NULL)) &&
      esp_storage(e, jb_json_tree_root(tree, root)))
    return tree;
  jb_json_tree_destroy(tree);
  return NULL;
}

const char *esp_dumps(jh_esp_t *e, jb_json_span_t compact) {
  jb_json_tree_view_t root = {0};
  jb_json_tree_t *tree = esp_parse_text(e, compact, &root);
  jb_string_t out = {0};
  const jb_json_tree_format_t format = {SIZE_MAX / 2, 2, true, true};
  size_t written = 0;
  if (tree != NULL &&
      esp_storage(e, jb_string_init(&out, jb_heap_allocator())) &&
      esp_storage(e, jb_json_tree_format(root, &format, jh_string_sink, &out,
                                         &written)))
    (void)esp_storage(e, jh_append(&out, "\n"));
  jb_json_tree_destroy(tree);
  return own(e, &out);
}

jb_json_span_t esp_field_text(jb_json_tree_view_t object, const char *key,
                              bool *is_text) {
  jb_json_span_t text = {"", 0};
  const bool found = jh_json_string(jh_json_field(object, key), &text);
  if (is_text != NULL)
    *is_text = found;
  return found ? text : jh_span("");
}

static const char *canonical_json(jh_esp_t *e, jb_json_tree_view_t value) {
  jb_string_t out = {0};
  const jb_json_tree_format_t format = {SIZE_MAX / 2, 0, true, true};
  size_t written = 0;
  if (value.tree == NULL)
    return "";
  if (esp_storage(e, jb_string_init(&out, jb_heap_allocator())))
    (void)esp_storage(
        e, jb_json_tree_format(value, &format, jh_string_sink, &out, &written));
  return own(e, &out);
}

bool esp_tree_equal(jh_esp_t *e, jb_json_tree_view_t a, jb_json_tree_view_t b) {
  if ((a.tree == NULL) != (b.tree == NULL))
    return false;
  return strcmp(canonical_json(e, a), canonical_json(e, b)) == 0;
}

bool esp_json_begin(jh_esp_t *e, esp_json_t *json) {
  memset(json, 0, sizeof *json);
  json->writer = (jb_json_writer_t){NULL, HAL_NONE};
  const jb_json_writer_limits_t limits = {SIZE_MAX / 2, 64, 65536, 1048576,
                                          SIZE_MAX / 2};
  size_t bytes = 0;
  hal_status_t status =
      jb_vector_init(&json->workspace, 1, jb_heap_allocator());
  if (status == HAL_OK)
    status = jb_string_init(&json->text, jb_heap_allocator());
  if (status == HAL_OK)
    status = jb_json_writer_workspace_size(&limits, &bytes);
  if (status == HAL_OK)
    status = jb_vector_resize(&json->workspace, bytes);
  if (status == HAL_OK) {
    const jb_json_workspace_t work = {jb_vector_data(&json->workspace),
                                      jb_vector_size(&json->workspace)};
    status = jb_json_writer_init(&json->writer, &limits, work, jh_string_sink,
                                 &json->text);
  }
  if (status == HAL_OK)
    status = jb_json_writer_object_begin(&json->writer);
  json->status = status;
  return esp_storage(e, status);
}

void esp_json_end(esp_json_t *json) {
  size_t written = 0;
  if (json->status == HAL_OK)
    json->status = jb_json_writer_object_end(&json->writer);
  if (json->status == HAL_OK)
    json->status = jb_json_writer_finish(&json->writer, &written);
  jb_vector_destroy(&json->workspace);
}

void esp_json_key(esp_json_t *json, const char *key) {
  if (json->status == HAL_OK && key != NULL)
    json->status = jb_json_writer_key(&json->writer, jh_span(key));
}

void esp_json_string(esp_json_t *json, const char *key, jb_json_span_t value) {
  esp_json_key(json, key);
  if (json->status == HAL_OK)
    json->status = jb_json_writer_string(&json->writer, value);
}

void esp_json_u64(esp_json_t *json, const char *key, uint64_t value) {
  esp_json_key(json, key);
  if (json->status == HAL_OK)
    json->status = jb_json_writer_u64(&json->writer, value);
}

void esp_json_null(esp_json_t *json, const char *key) {
  esp_json_key(json, key);
  if (json->status == HAL_OK)
    json->status = jb_json_writer_null(&json->writer);
}

void esp_json_list(esp_json_t *json, const char *key, const jb_vector_t *list) {
  esp_json_array(json, key);
  for (size_t i = 0; i < jb_vector_size(list) && json->status == HAL_OK; ++i)
    json->status = jb_json_writer_string(&json->writer,
                                         jh_string_span(jh_list_at(list, i)));
  esp_json_array_end(json);
}

void esp_json_tree(esp_json_t *json, const char *key,
                   jb_json_tree_view_t value) {
  esp_json_key(json, key);
  if (json->status == HAL_OK)
    json->status = value.tree == NULL
                       ? jb_json_writer_null(&json->writer)
                       : jh_json_write_tree(&json->writer, value);
}

void esp_json_object(esp_json_t *json, const char *key) {
  esp_json_key(json, key);
  if (json->status == HAL_OK)
    json->status = jb_json_writer_object_begin(&json->writer);
}

void esp_json_object_end(esp_json_t *json) {
  if (json->status == HAL_OK)
    json->status = jb_json_writer_object_end(&json->writer);
}

void esp_json_array(esp_json_t *json, const char *key) {
  esp_json_key(json, key);
  if (json->status == HAL_OK)
    json->status = jb_json_writer_array_begin(&json->writer);
}

void esp_json_array_end(esp_json_t *json) {
  if (json->status == HAL_OK)
    json->status = jb_json_writer_array_end(&json->writer);
}

/* ---- Regular expressions ---- */

bool esp_search(jh_esp_t *e, const char *pattern, jb_json_span_t subject,
                bool caseless, jb_json_span_t *group, size_t *groups) {
  if (!esp_ok(e))
    return false;
  int error = 0;
  PCRE2_SIZE offset = 0;
  const uint32_t options = PCRE2_UTF | PCRE2_UCP | PCRE2_MATCH_INVALID_UTF |
                           (caseless ? PCRE2_CASELESS : 0u);
  pcre2_code *code = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED,
                                   options, &error, &offset, NULL);
  if (code == NULL) {
    PCRE2_UCHAR message[256];
    (void)pcre2_get_error_message(error, message, sizeof message);
    (void)ESP_FAIL(e, jh_span("invalid regular expression "),
                   esp_repr(e, jh_span(pattern)), jh_span(": "),
                   jh_span((const char *)message));
    return false;
  }
  pcre2_match_data *data = pcre2_match_data_create_from_pattern(code, NULL);
  bool matched = false;
  if (data == NULL)
    (void)esp_storage(e, HAL_ENOMEM);
  else {
    const int count =
        pcre2_match(code, (PCRE2_SPTR)(subject.data ? subject.data : ""),
                    subject.length, 0, 0, data, NULL);
    uint32_t captures = 0;
    (void)pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &captures);
    if (groups != NULL)
      *groups = captures;
    if (count > 0) {
      matched = true;
      const PCRE2_SIZE *vector = pcre2_get_ovector_pointer(data);
      if (group != NULL) {
        *group = jh_span("");
        if (count > 1 && vector[2] != PCRE2_UNSET)
          *group = (jb_json_span_t){subject.data + vector[2],
                                    (size_t)(vector[3] - vector[2])};
      }
    } else if (count != PCRE2_ERROR_NOMATCH) {
      (void)ESP_FAIL(e, jh_span("regular expression "),
                     esp_repr(e, jh_span(pattern)),
                     jh_span(" failed to match"));
    }
  }
  pcre2_match_data_free(data);
  pcre2_code_free(code);
  return matched && esp_ok(e);
}

/* ---- Environments ---- */

static bool name_matches(const char *entry, const char *name) {
  const size_t length = strlen(name);
#ifdef _WIN32
  for (size_t i = 0; i < length; ++i)
    if (entry[i] == '\0' ||
        jh_ascii_to_upper(entry[i]) != jh_ascii_to_upper(name[i]))
      return false;
  return entry[length] == '=';
#else
  return strncmp(entry, name, length) == 0 && entry[length] == '=';
#endif
}

bool esp_environment_capture(jh_esp_t *e, jb_vector_t *environment) {
  if (!esp_list_init(e, environment))
    return false;
#ifdef _WIN32
  wchar_t *block = GetEnvironmentStringsW();
  if (block == NULL)
    return esp_storage(e, HAL_ENOMEM);
  for (const wchar_t *item = block; *item != L'\0' && esp_ok(e);
       item += wcslen(item) + 1) {
    /* os.environ leaves out the per-drive "=C:" entries. */
    if (item[0] == L'=')
      continue;
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, item, -1, NULL, 0, NULL, NULL);
    jb_string_t text = {0};
    if (length <= 0 ||
        !esp_storage(e, jb_string_init(&text, jb_heap_allocator())) ||
        !esp_storage(e, jb_string_resize(&text, (size_t)length, '\0')) ||
        WideCharToMultiByte(CP_UTF8, 0, item, -1, jb_string_mutable_data(&text),
                            length, NULL, NULL) != length ||
        !esp_storage(e, jb_string_resize(&text, (size_t)length - 1, '\0'))) {
      jb_string_destroy(&text);
      continue;
    }
    (void)esp_push(e, environment, jh_string_span(&text));
    jb_string_destroy(&text);
  }
  FreeEnvironmentStringsW(block);
#else
  for (char **item = environ; item != NULL && *item != NULL && esp_ok(e);
       ++item) {
    const char *equal = strchr(*item, '=');
    size_t bad = 0;
    if (equal == NULL || equal == *item ||
        jb_utf8_validate(*item, strlen(*item), &bad) != HAL_OK)
      continue;
    (void)esp_push(e, environment, jh_span(*item));
  }
#endif
  return esp_ok(e);
}

const char *esp_environment_get(const jb_vector_t *environment,
                                const char *name) {
  for (size_t i = 0; i < jb_vector_size(environment); ++i) {
    const char *entry = esp_at(environment, i);
    if (name_matches(entry, name))
      return entry + strlen(name) + 1;
  }
  return NULL;
}

bool esp_environment_set(jh_esp_t *e, jb_vector_t *environment,
                         const char *name, const char *value) {
  for (size_t i = 0; i < jb_vector_size(environment); ++i) {
    jb_string_t *entry = jb_vector_at(environment, i);
    if (!name_matches(jb_string_data(entry), name))
      continue;
    /* Keep the spelling of an existing name, as os.environ does. */
    const size_t length = strlen(name) + 1;
    return esp_storage(e, jb_string_resize(entry, length, '\0')) &&
           esp_storage(e, jh_append(entry, value));
  }
  return esp_push(
      e, environment,
      jh_span(ESP_TEXT(e, jh_span(name), jh_span("="), jh_span(value))));
}

static bool runnable(const char *path) {
  if (!esp_is_file(path))
    return false;
#ifdef _WIN32
  return true;
#else
  return access(path, X_OK) == 0;
#endif
}

const char *esp_find_program(jh_esp_t *e, const jb_vector_t *environment,
                             const char *name) {
  const char *path = esp_environment_get(environment, "PATH");
  if (path == NULL)
    path = "";
#ifdef _WIN32
  const char *suffix = strchr(name, '.') == NULL ? ".exe" : "";
#else
  const char *suffix = "";
#endif
  while (esp_ok(e)) {
    const char *end = strchr(path, ESP_PATH_LIST);
    const size_t length = end != NULL ? (size_t)(end - path) : strlen(path);
    const char *directory =
        length == 0 ? "." : ESP_TEXT(e, ((jb_json_span_t){path, length}));
    if (esp_absolute(directory)) {
      const char *candidate =
          ESP_TEXT(e, jh_span(esp_join(e, directory, name)), jh_span(suffix));
      if (runnable(candidate))
        return candidate;
    }
    if (end == NULL)
      break;
    path = end + 1;
  }
  return NULL;
}

const char *esp_host_python(jh_esp_t *e) {
  jb_string_t value = {0}, found = {0};
  bool present = false;
#ifdef _WIN32
  const char *name = "python";
#else
  const char *name = "python3";
#endif
  if (esp_storage(e, jb_string_init(&value, jb_heap_allocator())) &&
      esp_storage(
          e, jh_process_environment("JH_COMPONENT_PYTHON", &value, &present)) &&
      present && jb_string_size(&value) != 0)
    name = ESP_TEXT(e, jh_string_span(&value));
  jb_string_destroy(&value);
  const char *result = "";
  if (esp_storage(e, jb_string_init(&found, jb_heap_allocator())) &&
      jh_process_executable(name, e->options->base, false, &found,
                            e->failure) == HAL_OK)
    result = ESP_TEXT(e, jh_string_span(&found));
  jb_string_destroy(&found);
  return result;
}

/* ---- Processes ---- */

typedef struct {
  jh_esp_t *e;
  const esp_process_t *process;
} sink_t;

static hal_status_t append_limited(jb_string_t *out, const unsigned char *data,
                                   size_t length) {
  if (jb_string_size(out) + length > 512u * 1024u * 1024u)
    return HAL_EOVERFLOW;
  return jb_string_append(out, (const char *)data, length);
}

static hal_status_t write_stream(FILE *file, const unsigned char *data,
                                 size_t length) {
  if (fwrite(data, 1, length, file) != length || fflush(file) != 0)
    return HAL_EIO;
  return HAL_OK;
}

static hal_status_t process_output(void *context, jb_process_stream_t stream,
                                   const unsigned char *data, size_t length) {
  const sink_t *sink = context;
  const esp_process_t *p = sink->process;
  if (p->mode == ESP_RUN_FORWARD)
    return write_stream(stream == JB_PROCESS_STDERR ? stderr : sink->e->log,
                        data, length);
  if (p->mode == ESP_RUN_CAPTURE)
    return append_limited(stream == JB_PROCESS_STDERR ? p->err : p->out, data,
                          length);
  hal_status_t status = append_limited(p->out, data, length);
  if (status == HAL_OK && p->echo)
    status = write_stream(sink->e->log, data, length);
  return status;
}

static bool process_canceled(void *context) {
  const sink_t *sink = context;
  const jh_esp_options_t *o = sink->e->options;
  return o->canceled != NULL && o->canceled(o->cancel_context);
}

hal_status_t esp_process(jh_esp_t *e, const esp_process_t *p, int *exit_code) {
  *exit_code = -1;
  if (!esp_ok(e))
    return e->failure->status;
  jb_vector_t arguments = {0}, environment = {0};
  hal_status_t status =
      jb_vector_init(&arguments, sizeof(const char *), jb_heap_allocator());
  if (status == HAL_OK)
    status =
        jb_vector_init(&environment, sizeof(const char *), jb_heap_allocator());
  for (size_t i = 0; p->arguments != NULL && i < jb_vector_size(p->arguments) &&
                     status == HAL_OK;
       ++i) {
    const char *item = esp_at(p->arguments, i);
    status = jb_vector_append(&arguments, &item, 1);
  }
  const char *terminator = NULL;
  if (status == HAL_OK)
    status = jb_vector_append(&arguments, &terminator, 1);
  for (size_t i = 0; p->environment != NULL &&
                     i < jb_vector_size(p->environment) && status == HAL_OK;
       ++i) {
    const char *item = esp_at(p->environment, i);
    status = jb_vector_append(&environment, &item, 1);
  }
  if (status == HAL_OK && p->environment != NULL)
    status = jb_vector_append(&environment, &terminator, 1);
  if (!esp_storage(e, status)) {
    jb_vector_destroy(&environment);
    jb_vector_destroy(&arguments);
    return e->failure->status;
  }
  sink_t sink = {e, p};
  jb_process_options_t options = {0};
  options.executable = p->program;
  options.arguments = jb_vector_const_data(&arguments);
  options.working_directory =
      p->directory != NULL ? p->directory : e->options->base;
  options.environment =
      p->environment != NULL ? jb_vector_const_data(&environment) : NULL;
  options.timeout_ms = p->timeout_ms;
  options.output = process_output;
  options.canceled = process_canceled;
  options.context = &sink;
  jb_process_result_t result = {0};
  status = jb_process_run(&options, &result);
  jb_vector_destroy(&environment);
  jb_vector_destroy(&arguments);
  if (status == HAL_ECANCELED)
    (void)jh_fail_text(e->failure, status, 130, "canceled");
  else if (status == HAL_ENOMEM)
    (void)esp_storage(e, status);
  else if (status == HAL_OK)
    *exit_code = jh_build_exit_code(&result);
  return status;
}

bool esp_decode_text(jh_esp_t *e, jb_json_span_t input, jb_string_t *out) {
  hal_status_t status = HAL_OK;
  jb_string_clear(out);
  for (size_t i = 0; i < input.length && status == HAL_OK;) {
    const char c = input.data[i];
    if (c == '\r') {
      /* Universal newlines: \r\n and a lone \r both read as \n. */
      status = jh_append(out, "\n");
      i += (i + 1 < input.length && input.data[i + 1] == '\n') ? 2 : 1;
      continue;
    }
    uint32_t scalar = 0;
    size_t consumed = 0;
    if (jb_utf8_decode(input.data + i, input.length - i, &scalar, &consumed) ==
        HAL_OK) {
      status = jb_string_append(out, input.data + i, consumed);
      i += consumed;
      continue;
    }
    /* errors="replace": one U+FFFD for each maximal invalid subpart. */
    size_t skip = 1;
    const unsigned char lead = (unsigned char)c;
    size_t need = lead >= 0xF0 && lead <= 0xF4   ? 3
                  : lead >= 0xE0 && lead <= 0xEF ? 2
                  : lead >= 0xC2 && lead <= 0xDF ? 1
                                                 : 0;
    for (size_t k = 1; k <= need && i + k < input.length; ++k) {
      const unsigned char next = (unsigned char)input.data[i + k];
      unsigned char low = 0x80, high = 0xBF;
      if (k == 1 && lead == 0xE0)
        low = 0xA0;
      else if (k == 1 && lead == 0xED)
        high = 0x9F;
      else if (k == 1 && lead == 0xF0)
        low = 0x90;
      else if (k == 1 && lead == 0xF4)
        high = 0x8F;
      if (next < low || next > high)
        break;
      skip = k + 1;
    }
    status = jh_append(out, "\xEF\xBF\xBD");
    i += skip;
  }
  return esp_storage(e, status);
}

jb_json_span_t esp_strip(jb_json_span_t text) {
  jb_json_span_t out = text;
  (void)jh_python_strip(text, &out);
  return out;
}

jb_json_span_t esp_first_line(jb_json_span_t text) {
  /* The line boundaries of str.splitlines(). */
  size_t i = 0;
  while (i < text.length) {
    uint32_t scalar = (unsigned char)text.data[i];
    size_t width = 1;
    if (scalar >= 0x80 && jb_utf8_decode(text.data + i, text.length - i,
                                         &scalar, &width) != HAL_OK) {
      scalar = 0xFFFD;
      width = 1;
    }
    if (scalar == '\n' || scalar == '\r' || scalar == 0x0B || scalar == 0x0C ||
        (scalar >= 0x1C && scalar <= 0x1E) || scalar == 0x85 ||
        scalar == 0x2028 || scalar == 0x2029)
      break;
    i += width;
  }
  return (jb_json_span_t){text.data, i};
}
