#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* readlink */
#endif
#include "jh_json_file.h"
#include <jb/array.h>
#include <jb/filesystem.h>
#include <jb/json_number.h>
#include <string.h>
#ifndef _WIN32
#include <glob.h>
#include <unistd.h>
#endif

/* Exit code 4 for an exhausted limit or allocation, otherwise the given one. */
static int exit_code_for(hal_status_t status, int otherwise) {
  return status == HAL_EOVERFLOW || status == HAL_ENOMEM ? 4 : otherwise;
}

static jb_json_span_t text_of(const jb_string_t *text) {
  const jb_json_span_t result = {jb_string_data(text), jb_string_size(text)};
  return result;
}

static bool has_byte(jb_json_span_t text, char byte) {
  return text.length != 0 && memchr(text.data, byte, text.length) != NULL;
}

/* Writes a NUL-terminated path into buffer, measuring with (NULL, 0) as the
 * jhbase path functions do. */
typedef hal_status_t (*path_write_fn)(const void *context, char *buffer,
                                      size_t capacity, size_t *needed);

typedef struct {
  const char *base, *path;
} resolve_args_t;

static hal_status_t resolve_into(const void *context, char *buffer,
                                 size_t capacity, size_t *needed) {
  const resolve_args_t *args = context;
  return jb_path_resolve(args->base, args->path, buffer, capacity, needed);
}

static hal_status_t parent_into(const void *context, char *buffer,
                                size_t capacity, size_t *needed) {
  return jb_fs_path_parent(context, buffer, capacity, needed);
}

/* Replace out with the path produce writes: measured first, then written
 * into a string of the reported size. The result is built aside, so the
 * inputs of produce may point into out. */
static hal_status_t path_result(path_write_fn produce, const void *context,
                                jb_string_t *out, jh_failure_t *failure) {
  size_t needed;
  const hal_status_t measured = produce(context, NULL, 0, &needed);
  hal_status_t status = jh_require(
      failure, measured, exit_code_for(measured, 3), "cannot resolve path");
  if (status != HAL_OK)
    return status;
  jb_string_t result = {0};
  status = jb_string_init(&result, jb_heap_allocator());
  if (status == HAL_OK && needed > 1)
    status = jb_string_resize(&result, needed - 1, '\0');
  if (status != HAL_OK)
    status = jh_fail_storage(failure, status);
  else if (needed > 1) {
    const hal_status_t written =
        produce(context, jb_string_mutable_data(&result), needed, &needed);
    status = jh_require(failure, written, exit_code_for(written, 3),
                        "cannot resolve path");
  }
  if (status != HAL_OK) {
    jb_string_destroy(&result);
    return status;
  }
  jb_string_destroy(out);
  *out = result;
  return HAL_OK;
}

hal_status_t jh_absolute_path(const char *path, const char *base,
                              jb_string_t *out, jh_failure_t *failure) {
  if (path == NULL || base == NULL || out == NULL)
    return jh_fail_arguments(failure);
  const resolve_args_t args = {base, path};
  return path_result(resolve_into, &args, out, failure);
}

hal_status_t jh_path_parent(const char *path, jb_string_t *out,
                            jh_failure_t *failure) {
  if (path == NULL || out == NULL)
    return jh_fail_arguments(failure);
  return path_result(parent_into, path, out, failure);
}

hal_status_t jh_append_posix(jb_string_t *out, jb_json_span_t path) {
#ifdef _WIN32
  const size_t start = jb_string_size(out);
#endif
  const hal_status_t status = jh_append_span(out, path);
#ifdef _WIN32
  if (status == HAL_OK) {
    char *bytes = jb_string_mutable_data(out);
    for (size_t i = start; i < jb_string_size(out); ++i)
      if (bytes[i] == '\\')
        bytes[i] = '/';
  }
#endif
  return status;
}

/* Append path to out with bytes outside the unreserved set and the path
 * separators percent-escaped; Windows separators become '/'. */
static hal_status_t schema_identity(const char *path, size_t length,
                                    jb_string_t *out) {
  static const char hex[] = "0123456789ABCDEF";
  hal_status_t status = HAL_OK;
  for (size_t index = 0; status == HAL_OK && index < length; ++index) {
    unsigned char c = (unsigned char)path[index];
#ifdef _WIN32
    if (c == '\\')
      c = '/';
#endif
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
        c == '~' || c == '/' || c == ':') {
      const char plain = (char)c;
      status = jb_string_append(out, &plain, 1);
    } else {
      const char escaped[] = {'%', hex[c >> 4], hex[c & 15]};
      status = jb_string_append(out, escaped, sizeof escaped);
    }
  }
  return status;
}

void jh_json_location_init(jh_json_location_t *where) {
  static const jh_json_location_t empty = {0};
  *where = empty;
  // Initialization only checks its arguments, which are valid here.
  (void)jb_string_init(&where->source, jb_heap_allocator());
}

void jh_json_location_destroy(jh_json_location_t *where) {
  static const jh_json_location_t empty = {0};
  jb_string_destroy(&where->source);
  *where = empty;
}

/* Parser limits of one file of at most limit bytes. */
static jb_json_limits_t file_limits(size_t limit) {
  const jb_json_limits_t limits = {
      limit, 1000000, 256, limit, 1048576, 100000, limit * 32 + 1048576};
  return limits;
}

hal_status_t jh_json_tree_from_text(jb_json_span_t text, jb_json_tree_t **out) {
  const size_t length = text.length;
  const jb_json_limits_t limits = {length,     length + 1,        8, length, 64,
                                   length + 1, length * 64 + 4096};
  const jb_json_tree_limits_t tree_limits = {length + 1, 8, length,
                                             length * 64 + 4096};
  return jb_json_tree_parse(text, &limits, &tree_limits, 0, out, NULL);
}

/* HAL_OK, or the parse failure located in bytes. */
static hal_status_t parsed(jh_json_location_t *where, jb_json_span_t bytes,
                           hal_status_t status, jh_failure_t *failure) {
  if (status == HAL_OK)
    return HAL_OK;
  where->located = true;
  (void)jb_json_locate(bytes, where->error.offset, &where->line,
                       &where->column);
  return jh_fail_text(failure, status, exit_code_for(status, 2),
                      "invalid JSON or parser limit exceeded");
}

/* Resize a byte scratch vector to bytes zero bytes. */
static hal_status_t zeroed(jb_vector_t *scratch, size_t bytes,
                           jh_failure_t *failure) {
  jb_vector_clear(scratch);
  const hal_status_t status = jb_vector_resize(scratch, bytes);
  return jh_fail_storage(failure, status);
}

static jb_json_workspace_t workspace_of(jb_vector_t *scratch) {
  const jb_json_workspace_t work = {jb_vector_data(scratch),
                                    jb_vector_size(scratch)};
  return work;
}

/* The hardware loader keeps the exact token, but a decimal or exponent
 * spelling must still have a finite, nonzero magnitude in binary64, as the
 * Python loader required. Plain integers are exact at any size. */
static hal_status_t check_number(jh_json_location_t *where,
                                 const jb_json_doc_t *doc, size_t index,
                                 jb_vector_t *scratch, jh_failure_t *failure) {
  const jb_json_token_t *token = &doc->tokens[index];
  if (token->type != JB_JSON_NUMBER)
    return HAL_OK;
  const jb_json_span_t text = {doc->input.data + token->offset, token->length};
  if (!has_byte(text, '.') && !has_byte(text, 'e') && !has_byte(text, 'E'))
    return HAL_OK;
  const jb_json_node_t node = {doc, index};
  size_t scratch_bytes;
  hal_status_t status = jh_require(
      failure, jb_json_float_workspace_size(node, 64, &scratch_bytes), 4,
      "number workspace overflow");
  if (status == HAL_OK)
    status = zeroed(scratch, scratch_bytes, failure);
  if (status != HAL_OK)
    return status;
  const jb_json_float_options_t options = {true, false,
                                           64 * token->length + 1000000};
  uint64_t bits;
  if (jb_json_get_f64_bits(node, &options, workspace_of(scratch), &bits) ==
      HAL_OK)
    return HAL_OK;
  const jb_json_error_t error = {JB_JSON_ERROR_NONE, token->offset, SIZE_MAX,
                                 0};
  where->error = error;
  where->located = true;
  (void)jb_json_locate(doc->input, token->offset, &where->line, &where->column);
  const jb_json_span_t parts[] = {
      jh_span("number "), text,
      jh_span(" is outside the binary64 range or exceeds the conversion "
              "budget")};
  return jh_fail(failure, HAL_EPROTO, 2, parts, COUNTOF(parts));
}

static hal_status_t check_hardware_numbers(jh_json_location_t *where,
                                           const jb_json_doc_t *doc,
                                           jh_failure_t *failure) {
  jb_vector_t scratch = {0};
  hal_status_t status = jb_vector_init(&scratch, 1, jb_heap_allocator());
  for (size_t index = 0; status == HAL_OK && index < doc->token_count; ++index)
    status = check_number(where, doc, index, &scratch, failure);
  jb_vector_destroy(&scratch);
  return status;
}

/* Parse bytes into the tokens and document of out. */
static hal_status_t parse_document(jh_json_location_t *where,
                                   jb_json_span_t bytes, size_t limit,
                                   jh_json_document_t *out,
                                   jh_failure_t *failure) {
  const jb_json_limits_t limits = file_limits(limit);
  size_t scratch_bytes;
  hal_status_t status =
      jh_require(failure, jb_json_parse_workspace_size(&limits, &scratch_bytes),
                 4, "parser workspace overflow");
  if (status != HAL_OK)
    return status;
  jb_vector_t scratch = {0};
  status = jb_vector_init(&scratch, 1, jb_heap_allocator());
  if (status == HAL_OK)
    status = zeroed(&scratch, scratch_bytes, failure);
  jb_json_requirements_t needed = {0, 0};
  if (status == HAL_OK)
    status = parsed(where, bytes,
                    jb_json_measure(bytes, &limits, workspace_of(&scratch),
                                    &needed, &where->error),
                    failure);
  if (status == HAL_OK) {
    status = jb_vector_resize(&out->tokens, needed.node_count);
    if (status != HAL_OK)
      status = jh_fail_storage(failure, status);
  }
  if (status == HAL_OK)
    status =
        parsed(where, bytes,
               jb_json_parse(bytes, &limits, jb_vector_data(&out->tokens),
                             jb_vector_size(&out->tokens),
                             workspace_of(&scratch), &out->doc, &where->error),
               failure);
  jb_vector_destroy(&scratch);
  return status;
}

static hal_status_t read_document(jh_json_location_t *where, const char *path,
                                  size_t limit, bool hardware_numbers,
                                  jh_json_document_t *out,
                                  jh_failure_t *failure) {
  const size_t length = strlen(path);
  hal_status_t status = jb_string_assign(&out->uri, path, length);
  // path may point into where->source, which the assignment below moves:
  // everything after the first copy uses that copy.
  const char *stored = jb_string_data(&out->uri);
  if (status == HAL_OK)
    status = schema_identity(stored, length, &out->identity);
  if (status == HAL_OK)
    status = jb_string_assign(&where->source, stored, length);
  if (status != HAL_OK)
    return jh_fail_storage(failure, status);
  status = jb_file_read(stored, limit, &out->input);
  if (status != HAL_OK)
    return jh_fail_text(failure, status, exit_code_for(status, 3),
                        "cannot read input file");
  const jb_json_span_t bytes = {(const char *)out->input.data,
                                out->input.length};
  status = parse_document(where, bytes, limit, out, failure);
  if (status != HAL_OK)
    return status;
  where->located = false;
  return hardware_numbers ? check_hardware_numbers(where, &out->doc, failure)
                          : HAL_OK;
}

hal_status_t jh_path_join(const char *base, const char *name,
                          jb_string_t *out) {
  size_t required = 0;
  hal_status_t status = jb_fs_path_join(base, name, NULL, 0, &required);
  if (status != HAL_OK && status != HAL_EOVERFLOW)
    return status;
  status = jb_string_resize(out, required, '\0');
  if (status == HAL_OK)
    status = jb_fs_path_join(base, name, jb_string_mutable_data(out), required,
                             &required);
  return status == HAL_OK ? jb_string_resize(out, required - 1u, '\0') : status;
}

#ifdef _WIN32
static hal_status_t collect_json(void *context, const char *name,
                                 const jb_fs_info_t *info) {
  (void)info;
  const size_t length = strlen(name);
  if (length < 5 || strcmp(name + length - 5, ".json") != 0)
    return HAL_OK;
  return jh_list_push(context, jh_span(name));
}
#endif

hal_status_t jh_json_names(const char *directory, jb_vector_t *names) {
#ifdef _WIN32
  const hal_status_t listed = jb_fs_list(directory, collect_json, names);
  if (listed != HAL_OK && listed != HAL_ENOENT)
    return listed;
#else
  /* POSIX names need not be UTF-8. Match the suffix before inspecting any
   * entry, so an unrelated unreadable name cannot reject the catalogue. */
  jb_fs_info_t info;
  hal_status_t status = jb_fs_status(directory, true, &info);
  if (status == HAL_ENOENT)
    return HAL_OK;
  if (status != HAL_OK)
    return status;
  if (info.kind != JB_FS_DIRECTORY)
    return HAL_ETYPE;
  jb_string_t pattern = {0};
  status = jb_string_init(&pattern, jb_heap_allocator());
  for (const char *p = directory; status == HAL_OK && *p != '\0'; ++p) {
    if (strchr("\\*?[", *p) != NULL)
      jh_put(&pattern, &status, "\\");
    jh_put_span(&pattern, &status, (jb_json_span_t){p, 1});
  }
  const size_t prefix = jb_string_size(&pattern);
  static const char *const suffixes[] = {"/*.json", "/.*.json", "/.json"};
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(suffixes); ++i) {
    status = jb_string_resize(&pattern, prefix, '\0');
    jh_put(&pattern, &status, suffixes[i]);
    if (status != HAL_OK)
      break;
    glob_t matches = {0};
    const int listed = glob(jb_string_data(&pattern), GLOB_ERR, NULL, &matches);
    if (listed == 0) {
      for (size_t j = 0; status == HAL_OK && j < matches.gl_pathc; ++j) {
        const char *name = strrchr(matches.gl_pathv[j], '/');
        status = jh_list_push(names, jh_span(name + 1));
      }
    } else if (listed != GLOB_NOMATCH) {
      status = listed == GLOB_NOSPACE ? HAL_ENOMEM : HAL_EIO;
    }
    globfree(&matches);
  }
  jb_string_destroy(&pattern);
  if (status != HAL_OK)
    return status;
#endif
  jh_list_sort(names);
  return HAL_OK;
}

static hal_status_t collect_entry(void *context, const char *name,
                                  const jb_fs_info_t *info) {
  (void)info;
  return jh_list_push(context, jh_span(name));
}

hal_status_t jh_walk_files(const char *directory, jb_json_span_t relative,
                           jh_walk_fn visit, void *user,
                           jh_failure_t *failure) {
  if (directory == NULL || visit == NULL ||
      (relative.data == NULL && relative.length != 0))
    return jh_fail_arguments(failure);
  jb_vector_t names = {0};
  jb_string_t path = {0}, child = {0};
  hal_status_t status = jh_fail_storage(failure, jh_list_init(&names));
  if (status == HAL_OK)
    status =
        jh_fail_storage(failure, jb_string_init(&path, jb_heap_allocator()));
  if (status == HAL_OK)
    status =
        jh_fail_storage(failure, jb_string_init(&child, jb_heap_allocator()));
  if (status == HAL_OK) {
    const hal_status_t listed = jb_fs_list(directory, collect_entry, &names);
    if (listed == HAL_ENOMEM || listed == HAL_EOVERFLOW)
      status = jh_fail_storage(failure, listed);
    else if (listed != HAL_OK && listed != HAL_ENOENT && listed != HAL_ETYPE)
      status = jh_fail(failure, listed, 3,
                       JH_PARTS(jh_span(directory), jh_span(": cannot list: "),
                                jh_span(hal_status_to_string(listed))));
    jh_list_sort(&names);
  }
#ifdef _WIN32
  static const char separator[] = "\\";
#else
  static const char separator[] = "/";
#endif
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&names); ++i) {
    const char *name = jb_string_data(jh_list_at(&names, i));
    status = jh_fail_storage(failure, jh_path_join(directory, name, &path));
    if (status == HAL_OK)
      status = jh_fail_storage(
          failure, jh_join(&child, JH_PARTS(relative, jh_span(separator),
                                            jh_span(name))));
    jb_fs_info_t entry;
    if (status != HAL_OK)
      break;
    const hal_status_t inspected =
        jb_fs_status(jb_string_data(&path), false, &entry);
    if (inspected == HAL_ENOENT)
      continue;
    if (inspected != HAL_OK) {
      status = jh_fail(failure, inspected, exit_code_for(inspected, 3),
                       JH_PARTS(jh_string_span(&path),
                                jh_span(": cannot inspect dependency")));
      break;
    }
    const bool link = entry.kind == JB_FS_LINK;
    jb_fs_info_t target = entry;
    if (link) {
      const hal_status_t followed =
          jb_fs_status(jb_string_data(&path), true, &target);
      if (followed == HAL_ENOMEM || followed == HAL_EOVERFLOW) {
        status =
            jh_fail(failure, followed, 4,
                    JH_PARTS(jh_string_span(&path),
                             jh_span(": cannot inspect dependency target")));
        break;
      }
      if (followed != HAL_OK)
        continue; /* An unresolvable target, including a link loop. */
    }
    if (target.kind == JB_FS_REGULAR)
      status = visit(user, jb_string_data(&path), jh_string_span(&child), name);
    else if (!link && entry.kind == JB_FS_DIRECTORY)
      status = jh_walk_files(jb_string_data(&path), jh_string_span(&child),
                             visit, user, failure);
  }
  jb_string_destroy(&child);
  jb_string_destroy(&path);
  jh_list_destroy(&names);
  return status;
}

hal_status_t jh_resolved_path(const char *root, jb_string_t *out) {
  size_t required = 0;
  hal_status_t status = jb_fs_realpath(root, NULL, 0, &required);
  if (status == HAL_ENOENT || (status == HAL_EOVERFLOW && required == 0))
    return jb_string_assign(out, root, strlen(root));
  if (status != HAL_OK && status != HAL_EOVERFLOW)
    return status;
  status = jb_string_resize(out, required, '\0');
  if (status == HAL_OK)
    status =
        jb_fs_realpath(root, jb_string_mutable_data(out), required, &required);
  if (status == HAL_OK)
    status = jb_string_resize(out, required - 1, '\0');
#ifdef _WIN32
  const char *text = jb_string_data(out);
  if (status == HAL_OK && jb_string_size(out) >= 7 &&
      memcmp(text, "\\\\?\\", 4) == 0 && text[5] == ':' && text[6] == '\\')
    status = jb_string_assign(out, text + 4, jb_string_size(out) - 4);
#endif
  return status;
}

hal_status_t jh_working_directory(jb_string_t *out, jh_failure_t *failure) {
  size_t needed = 0;
  const hal_status_t measured = jb_fs_cwd(NULL, 0, &needed);
  hal_status_t status = HAL_OK;
  if (measured != HAL_OK && measured != HAL_EOVERFLOW)
    status = jh_require(failure, measured, exit_code_for(measured, 3),
                        "cannot read the working directory");
  if (status == HAL_OK)
    status = jh_fail_storage(failure, jb_string_resize(out, needed, '\0'));
  if (status == HAL_OK) {
    const hal_status_t written =
        jb_fs_cwd(jb_string_mutable_data(out), needed, &needed);
    status = jh_require(failure, written, exit_code_for(written, 3),
                        "cannot read the working directory");
  }
  if (status == HAL_OK)
    status = jh_fail_storage(failure, jb_string_resize(out, needed - 1, '\0'));
  return status;
}

/* path joined to base, or to the working directory when base is NULL: on
 * POSIX as written, as pathlib joins; on Windows normalized lexically, as
 * ntpath.realpath does before it resolves anything. */
static hal_status_t joined_path(const char *path, const char *base,
                                jb_string_t *out, jh_failure_t *failure) {
  jb_string_t cwd = {0};
  hal_status_t status = HAL_OK;
  if (base == NULL) {
    status =
        jh_fail_storage(failure, jb_string_init(&cwd, jb_heap_allocator()));
    if (status == HAL_OK)
      status = jh_working_directory(&cwd, failure);
    base = jb_string_data(&cwd);
  }
#ifdef _WIN32
  if (status == HAL_OK)
    status = jh_absolute_path(path, base, out, failure);
#else
  bool absolute = false;
  if (status == HAL_OK)
    status = jh_require(failure, jb_fs_path_is_absolute(path, &absolute), 3,
                        "cannot resolve path");
  if (status == HAL_OK)
    status = jh_fail_storage(failure,
                             jb_string_assign(out, absolute ? path : base,
                                              strlen(absolute ? path : base)));
  if (status == HAL_OK && !absolute) {
    status = jh_fail_storage(failure, jb_string_append(out, "/", 1));
    if (status == HAL_OK)
      status =
          jh_fail_storage(failure, jb_string_append(out, path, strlen(path)));
  }
#endif
  jb_string_destroy(&cwd);
  return status;
}

#ifndef _WIN32
enum { LINK_LIMIT = 40, LINK_BYTES = 1024 * 1024 };

/* The target of the symbolic link path, as os.readlink returns it. */
static hal_status_t read_link(const char *path, jb_string_t *out) {
  for (size_t capacity = 256; capacity <= LINK_BYTES; capacity *= 2) {
    hal_status_t status = jb_string_resize(out, capacity, '\0');
    if (status != HAL_OK)
      return status;
    const ssize_t length =
        readlink(path, jb_string_mutable_data(out), capacity);
    if (length < 0)
      return HAL_EIO;
    if ((size_t)length < capacity)
      return jb_string_resize(out, (size_t)length, '\0');
  }
  return HAL_EOVERFLOW;
}

/* posixpath._joinrealpath: append the components of rest (length bytes) to
 * path, a resolved absolute path without links ("" for the root). A link
 * resolves in the directory that holds it; links counts every link followed. */
static hal_status_t join_real(jb_string_t *path, const char *rest,
                              size_t length, unsigned *links,
                              jh_failure_t *failure) {
  jb_string_t target = {0};
  hal_status_t status =
      jh_fail_storage(failure, jb_string_init(&target, jb_heap_allocator()));
  for (size_t at = 0; status == HAL_OK && at < length;) {
    size_t end = at;
    while (end < length && rest[end] != '/')
      ++end;
    const char *name = rest + at;
    const size_t size = end - at;
    at = end + 1;
    if (size == 0 || (size == 1 && name[0] == '.'))
      continue;
    const size_t before = jb_string_size(path);
    if (size == 2 && name[0] == '.' && name[1] == '.') {
      const char *text = jb_string_data(path);
      size_t cut = before;
      while (cut > 0 && text[cut - 1] != '/')
        --cut;
      status = jh_fail_storage(
          failure, jb_string_resize(path, cut > 0 ? cut - 1 : 0, '\0'));
      continue;
    }
    status = jh_fail_storage(failure, jb_string_append(path, "/", 1));
    if (status == HAL_OK)
      status = jh_fail_storage(failure, jb_string_append(path, name, size));
    jb_fs_info_t info = {0};
    const hal_status_t seen =
        status == HAL_OK ? jb_fs_status(jb_string_data(path), false, &info)
                         : status;
    if (status == HAL_OK && seen == HAL_ENOMEM)
      status = jh_fail_storage(failure, seen);
    /* Like Python, any other failure to inspect the entry means no link. */
    if (status != HAL_OK || seen != HAL_OK || info.kind != JB_FS_LINK)
      continue;
    if (++*links > LINK_LIMIT) {
      status =
          jh_fail(failure, HAL_EIO, 3,
                  JH_PARTS(jh_string_span(path),
                           jh_span(": too many levels of symbolic links")));
      continue;
    }
    const hal_status_t read = read_link(jb_string_data(path), &target);
    if (read == HAL_ENOMEM)
      status = jh_fail_storage(failure, read);
    else if (read != HAL_OK)
      status = jh_fail(failure, read, 3,
                       JH_PARTS(jh_string_span(path),
                                jh_span(": cannot read the symbolic link: "),
                                jh_span(hal_status_to_string(read))));
    if (status == HAL_OK)
      status = jh_fail_storage(
          failure, jb_string_resize(path,
                                    jb_string_size(&target) != 0 &&
                                            jb_string_data(&target)[0] == '/'
                                        ? 0
                                        : before,
                                    '\0'));
    if (status == HAL_OK)
      status = join_real(path, jb_string_data(&target), jb_string_size(&target),
                         links, failure);
  }
  jb_string_destroy(&target);
  return status;
}
#else
/* The real path of the longest existing prefix of the absolute, normalized
 * path followed by the rest. */
static hal_status_t existing_prefix(const char *path, jb_string_t *out) {
  jb_string_t head = {0}, tail = {0}, name = {0};
  hal_status_t status = jb_string_init(&head, jb_heap_allocator());
  if (status == HAL_OK)
    status = jb_string_init(&tail, jb_heap_allocator());
  if (status == HAL_OK)
    status = jb_string_init(&name, jb_heap_allocator());
  if (status == HAL_OK)
    status = jb_string_assign(&head, path, strlen(path));
  while (status == HAL_OK) {
    size_t required = 0;
    const hal_status_t real =
        jb_fs_realpath(jb_string_data(&head), NULL, 0, &required);
    if (real != HAL_ENOENT)
      break;
    size_t parent_size = 0, name_size = 0;
    status = jb_fs_path_parent(jb_string_data(&head), NULL, 0, &parent_size);
    if (status == HAL_EOVERFLOW)
      status = HAL_OK;
    (void)jb_fs_path_filename(jb_string_data(&head), NULL, 0, &name_size);
    if (status != HAL_OK || name_size <= 1)
      break;
    status = jb_string_resize(&name, name_size, '\0');
    if (status == HAL_OK)
      status = jb_fs_path_filename(jb_string_data(&head),
                                   jb_string_mutable_data(&name), name_size,
                                   &name_size);
    if (status == HAL_OK)
      status = jb_string_resize(&name, name_size - 1, '\0');
    if (status == HAL_OK && jb_string_size(&tail) != 0) {
      status = jb_string_append(&name, "/", 1);
      if (status == HAL_OK)
        status = jb_string_append(&name, jb_string_data(&tail),
                                  jb_string_size(&tail));
    }
    if (status == HAL_OK)
      status =
          jb_string_assign(&tail, jb_string_data(&name), jb_string_size(&name));
    jb_string_t parent = {0};
    if (status == HAL_OK)
      status = jb_string_init(&parent, jb_heap_allocator());
    if (status == HAL_OK)
      status = jb_string_resize(&parent, parent_size, '\0');
    if (status == HAL_OK)
      status = jb_fs_path_parent(jb_string_data(&head),
                                 jb_string_mutable_data(&parent), parent_size,
                                 &parent_size);
    if (status == HAL_OK)
      status = jb_string_resize(&parent, parent_size - 1, '\0');
    if (status == HAL_OK &&
        jh_span_equal(jh_string_span(&parent), jh_string_span(&head)))
      status = HAL_ENOENT;
    if (status == HAL_OK)
      status = jb_string_assign(&head, jb_string_data(&parent),
                                jb_string_size(&parent));
    jb_string_destroy(&parent);
  }
  if (status == HAL_ENOENT)
    status = HAL_OK;
  if (status == HAL_OK)
    status = jh_resolved_path(jb_string_data(&head), out);
  if (status == HAL_OK && jb_string_size(&tail) != 0) {
    jb_string_t joined = {0};
    status = jb_string_init(&joined, jb_heap_allocator());
    if (status == HAL_OK)
      status =
          jh_path_join(jb_string_data(out), jb_string_data(&tail), &joined);
    if (status == HAL_OK)
      status = jb_string_assign(out, jb_string_data(&joined),
                                jb_string_size(&joined));
    jb_string_destroy(&joined);
  }
  jb_string_destroy(&name);
  jb_string_destroy(&tail);
  jb_string_destroy(&head);
  return status;
}
#endif

hal_status_t jh_python_resolve(const char *path, const char *base,
                               jb_string_t *out, jh_failure_t *failure) {
  if (path == NULL || out == NULL)
    return jh_fail_arguments(failure);
  jb_string_t joined = {0}, resolved = {0};
  hal_status_t status =
      jh_fail_storage(failure, jb_string_init(&joined, jb_heap_allocator()));
  if (status == HAL_OK)
    status = jh_fail_storage(failure,
                             jb_string_init(&resolved, jb_heap_allocator()));
  if (status == HAL_OK)
    status = joined_path(path, base, &joined, failure);
#ifdef _WIN32
  if (status == HAL_OK) {
    const hal_status_t real =
        existing_prefix(jb_string_data(&joined), &resolved);
    if (real == HAL_ENOMEM)
      status = jh_fail_storage(failure, real);
    else if (real != HAL_OK)
      status = jh_fail(failure, real, 3,
                       JH_PARTS(jh_string_span(&joined),
                                jh_span(": cannot resolve: "),
                                jh_span(hal_status_to_string(real))));
  }
#else
  unsigned links = 0;
  if (status == HAL_OK)
    status = join_real(&resolved, jb_string_data(&joined),
                       jb_string_size(&joined), &links, failure);
  if (status == HAL_OK && jb_string_size(&resolved) == 0)
    status = jh_fail_storage(failure, jb_string_assign(&resolved, "/", 1));
#endif
  if (status == HAL_OK) {
    jb_string_destroy(out);
    *out = resolved;
    resolved = (jb_string_t){0};
  }
  jb_string_destroy(&resolved);
  jb_string_destroy(&joined);
  return status;
}

hal_status_t jh_json_read_document(jh_json_location_t *where, const char *path,
                                   size_t limit, bool hardware_numbers,
                                   jh_json_document_t *out,
                                   jh_failure_t *failure) {
  const jb_allocator_t *heap = jb_heap_allocator();
  // Initialization fails only for a document that is not zero, which this
  // call then leaves to its owner.
  if (where == NULL || path == NULL || out == NULL ||
      jb_string_init(&out->uri, heap) != HAL_OK ||
      jb_string_init(&out->identity, heap) != HAL_OK ||
      jb_vector_init(&out->tokens, sizeof(jb_json_token_t), heap) != HAL_OK)
    return jh_fail_arguments(failure);
  const hal_status_t status =
      read_document(where, path, limit, hardware_numbers, out, failure);
  if (status != HAL_OK)
    jh_json_document_destroy(out);
  return status;
}

hal_status_t jh_json_parse_document(jh_json_location_t *where, const char *uri,
                                    jb_json_span_t text, size_t limit,
                                    jh_json_document_t *out,
                                    jh_failure_t *failure) {
  const jb_allocator_t *heap = jb_heap_allocator();
  if (where == NULL || uri == NULL || out == NULL ||
      (text.data == NULL && text.length != 0) ||
      jb_string_init(&out->uri, heap) != HAL_OK ||
      jb_string_init(&out->identity, heap) != HAL_OK ||
      jb_string_init(&out->text, heap) != HAL_OK ||
      jb_vector_init(&out->tokens, sizeof(jb_json_token_t), heap) != HAL_OK)
    return jh_fail_arguments(failure);
  const size_t length = strlen(uri);
  hal_status_t status = jb_string_assign(&out->uri, uri, length);
  if (status == HAL_OK)
    status = schema_identity(jb_string_data(&out->uri), length, &out->identity);
  if (status == HAL_OK)
    status =
        jb_string_assign(&where->source, jb_string_data(&out->uri), length);
  if (status == HAL_OK && text.length != 0)
    status = jb_string_assign(&out->text, text.data, text.length);
  if (status != HAL_OK)
    status = jh_fail_storage(failure, status);
  if (status == HAL_OK)
    status =
        parse_document(where, jh_string_span(&out->text), limit, out, failure);
  if (status == HAL_OK)
    where->located = false;
  else
    jh_json_document_destroy(out);
  return status;
}

void jh_json_document_destroy(jh_json_document_t *document) {
  static const jh_json_document_t empty = {0};
  jb_file_release(&document->input);
  jb_string_destroy(&document->text);
  jb_vector_destroy(&document->tokens);
  jb_string_destroy(&document->uri);
  jb_string_destroy(&document->identity);
  *document = empty;
}

/* The root of a settings tree must be an object. */
static hal_status_t settings_root(const char *path, const jb_json_tree_t *tree,
                                  jh_failure_t *failure) {
  jb_json_tree_view_t root = {0};
  jb_json_type_t type = JB_JSON_NULL;
  hal_status_t status = jh_require(failure, jb_json_tree_root(tree, &root), 2,
                                   "cannot read the settings root");
  if (status == HAL_OK)
    status = jh_require(failure, jb_json_tree_type(root, &type), 2,
                        "cannot read the settings root");
  if (status != HAL_OK || type == JB_JSON_OBJECT)
    return status;
  const jb_json_span_t parts[] = {jh_span(path),
                                  jh_span(" must contain a JSON object")};
  return jh_fail(failure, HAL_ETYPE, 2, parts, COUNTOF(parts));
}

/* Read the file at path into a tree with the reading flags. A missing file
 * reads as an empty object when missing_as_empty is set, otherwise it fails;
 * exists reports whether the file was there. */
static hal_status_t read_tree(jh_json_location_t *where, const char *path,
                              size_t limit, uint32_t flags,
                              bool missing_as_empty, jb_json_tree_t **out,
                              bool *exists, jh_failure_t *failure) {
  *out = NULL;
  *exists = false;
  hal_status_t status = jb_string_assign(&where->source, path, strlen(path));
  if (status != HAL_OK)
    return jh_fail_storage(failure, status);
  // path may point into where->source, which the assignment moved: the read
  // and the diagnostics use the stored copy.
  const char *stored = jb_string_data(&where->source);
  where->located = false;
  jb_file_data_t input = {0};
  const hal_status_t read = jb_file_read(stored, limit, &input);
  if (read != HAL_OK && (read != HAL_ENOENT || !missing_as_empty))
    return jh_fail_text(failure, read, exit_code_for(read, 3),
                        "cannot read input file");
  const jb_json_span_t file_bytes = {(const char *)input.data, input.length};
  const jb_json_span_t bytes = read == HAL_ENOENT ? jh_span("{}") : file_bytes;
  const jb_json_limits_t limits = file_limits(limit);
  const jb_json_tree_limits_t tree_limits = {
      limits.max_nodes, limits.max_depth, limit, limits.max_key_compare_bytes};
  jb_json_tree_t *tree = NULL;
  status = jb_json_tree_parse(bytes, &limits, &tree_limits, flags, &tree,
                              &where->error);
  if (status != HAL_OK) {
    // The tree limits leave the code NONE and have no place in the file.
    if (where->error.code != JB_JSON_ERROR_NONE) {
      where->located = true;
      (void)jb_json_locate(bytes, where->error.offset, &where->line,
                           &where->column);
    }
    status = jh_fail_text(failure, status, exit_code_for(status, 2),
                          "invalid JSON or parser limit exceeded");
  }
  jb_file_release(&input);
  if (status != HAL_OK) {
    if (tree != NULL)
      jb_json_tree_destroy(tree);
    return status;
  }
  *out = tree;
  *exists = read == HAL_OK;
  return HAL_OK;
}

hal_status_t jh_json_read_settings(jh_json_location_t *where, const char *path,
                                   size_t limit, jb_json_tree_t **out,
                                   bool *exists, jh_failure_t *failure) {
  if (where == NULL || path == NULL || out == NULL || exists == NULL)
    return jh_fail_arguments(failure);
  // load_json_file reads a missing file as an empty object.
  const uint32_t python_reading = JB_JSON_TREE_COMMENTS |
                                  JB_JSON_TREE_LAST_DUPLICATE |
                                  JB_JSON_TREE_NONFINITE;
  hal_status_t status =
      read_tree(where, path, limit, python_reading, true, out, exists, failure);
  if (status == HAL_OK)
    status = settings_root(jb_string_data(&where->source), *out, failure);
  if (status != HAL_OK && *out != NULL) {
    jb_json_tree_destroy(*out);
    *out = NULL;
    *exists = false;
  }
  return status;
}

hal_status_t jh_json_read_value(jh_json_location_t *where, const char *path,
                                size_t limit, jb_json_tree_t **out,
                                jh_failure_t *failure) {
  if (where == NULL || path == NULL || out == NULL)
    return jh_fail_arguments(failure);
  bool exists = false;
  return read_tree(where, path, limit,
                   JB_JSON_TREE_LAST_DUPLICATE | JB_JSON_TREE_NONFINITE, false,
                   out, &exists, failure);
}

static void report_destroy(jh_schema_report_t *report) {
  jb_string_destroy(&report->pointer);
  jb_string_destroy(&report->uri);
  jb_string_destroy(&report->rule);
  jb_string_destroy(&report->reason);
  jb_string_destroy(&report->message);
}

static void clear_reports(jh_schema_files_t *files) {
  for (size_t index = 0; index < jb_vector_size(&files->reports); ++index)
    report_destroy(jb_vector_at(&files->reports, index));
  jb_vector_clear(&files->reports);
}

static jh_json_document_t *document_at(const jh_schema_files_t *files,
                                       size_t index) {
  return *(jh_json_document_t *const *)jb_vector_const_at(&files->documents,
                                                          index);
}

static void document_free(jh_json_document_t *document) {
  const jb_allocator_t *heap = jb_heap_allocator();
  jh_json_document_destroy(document);
  heap->release(heap->context, document);
}

void jh_schema_files_init(jh_schema_files_t *files, jh_json_location_t *where,
                          size_t max_bytes) {
  static const jh_schema_files_t empty = {0};
  const jb_allocator_t *heap = jb_heap_allocator();
  *files = empty;
  files->where = where;
  files->max_bytes = max_bytes;
  // Initialization only checks its arguments, which are valid here.
  (void)jb_vector_init(&files->documents, sizeof(jh_json_document_t *), heap);
  (void)jb_vector_init(&files->reports, sizeof(jh_schema_report_t), heap);
  (void)jb_vector_init(&files->scratch, 1, heap);
}

void jh_schema_files_destroy(jh_schema_files_t *files) {
  static const jh_schema_files_t empty = {0};
  clear_reports(files);
  for (size_t index = 0; index < jb_vector_size(&files->documents); ++index)
    document_free(document_at(files, index));
  jb_vector_destroy(&files->documents);
  jb_vector_destroy(&files->reports);
  jb_vector_destroy(&files->scratch);
  *files = empty;
}

/* Read one schema file of at most limit bytes and append it to files. */
static hal_status_t load_document(jh_schema_files_t *files, const char *path,
                                  size_t limit, jh_json_document_t **out,
                                  jh_failure_t *failure) {
  static const jh_json_document_t empty = {0};
  const jb_allocator_t *heap = jb_heap_allocator();
  jh_json_document_t *document =
      heap->allocate(heap->context, sizeof *document);
  if (document == NULL)
    return jh_fail_storage(failure, HAL_ENOMEM);
  *document = empty;
  hal_status_t status = jh_json_read_document(files->where, path, limit, false,
                                              document, failure);
  if (status == HAL_OK) {
    status = jb_vector_append(&files->documents, &document, 1);
    if (status != HAL_OK)
      status = jh_fail_storage(failure, status);
  }
  if (status != HAL_OK) {
    document_free(document);
    return status;
  }
  *out = document;
  return HAL_OK;
}

hal_status_t jh_schema_files_root(jh_schema_files_t *files, const char *path,
                                  const jh_json_document_t **out,
                                  jh_failure_t *failure) {
  if (files == NULL || path == NULL)
    return jh_fail_arguments(failure);
  jh_json_document_t *loaded;
  const hal_status_t status =
      load_document(files, path, files->max_bytes, &loaded, failure);
  if (status != HAL_OK)
    return status;
  const jh_json_document_t *root = document_at(files, 0);
  files->schema_bytes = root->doc.input.length;
  if (out != NULL)
    *out = root;
  return HAL_OK;
}

size_t jh_schema_files_report_count(const jh_schema_files_t *files) {
  return jb_vector_size(&files->reports);
}

const jh_schema_report_t *jh_schema_files_report(const jh_schema_files_t *files,
                                                 size_t index) {
  return jb_vector_const_at(&files->reports, index);
}

static jh_json_schema_source_t source_of(const jh_json_document_t *document) {
  const jh_json_schema_source_t source = {text_of(&document->identity),
                                          &document->doc};
  return source;
}

/* The loaded document at path, read on first use within the file and byte
 * limits. */
static hal_status_t find_or_load(jh_schema_files_t *files, const char *path,
                                 jh_json_schema_source_t *out,
                                 jh_failure_t *failure) {
  const jb_json_span_t wanted = jh_span(path);
  const size_t count = jb_vector_size(&files->documents);
  for (size_t index = 0; index < count; ++index) {
    const jh_json_document_t *document = document_at(files, index);
    // An entry that cannot be compared (missing, unreadable) is another file.
    bool same = false;
    if (jh_span_equal(text_of(&document->uri), wanted) ||
        (jb_fs_equivalent(jb_string_data(&document->uri), path, &same) ==
             HAL_OK &&
         same)) {
      *out = source_of(document);
      return HAL_OK;
    }
  }
  const size_t total_limit = 64 * 1024 * 1024;
  if (count >= 256 || files->schema_bytes >= total_limit)
    return HAL_EOVERFLOW;
  const size_t room = total_limit - files->schema_bytes;
  jh_json_document_t *document;
  const hal_status_t status = load_document(
      files, path, files->max_bytes < room ? files->max_bytes : room, &document,
      failure);
  if (status != HAL_OK)
    return status;
  files->schema_bytes += document->doc.input.length;
  *out = source_of(document);
  return HAL_OK;
}

/* jh_json_schema_load_fn over the files at user: a relative reference to a
 * local file, resolved against the directory of the referring document. The
 * failure of a read is reported by its status; where keeps its place. */
static hal_status_t load_schema(void *user, jb_json_span_t base_uri,
                                jb_json_span_t reference,
                                jh_json_schema_source_t *out) {
  jh_schema_files_t *files = user;
  if (has_byte(reference, ':') || has_byte(reference, '\\') ||
      has_byte(reference, '\0') ||
      (reference.length >= 2 && reference.data[0] == '/' &&
       reference.data[1] == '/'))
    return HAL_EUNSUPPORTED;
  const jh_json_document_t *base = NULL;
  for (size_t index = 0;
       base == NULL && index < jb_vector_size(&files->documents); ++index)
    if (jh_span_equal(text_of(&document_at(files, index)->identity), base_uri))
      base = document_at(files, index);
  if (base == NULL)
    return HAL_EINTERNAL;
  const jb_allocator_t *heap = jb_heap_allocator();
  jh_failure_t failure;
  jh_failure_init(&failure);
  jb_string_t parent = {0}, relative = {0}, path = {0};
  hal_status_t status = jb_string_init(&parent, heap);
  if (status == HAL_OK)
    status = jb_string_init(&relative, heap);
  if (status == HAL_OK)
    status = jb_string_init(&path, heap);
  if (status == HAL_OK)
    status =
        path_result(parent_into, jb_string_data(&base->uri), &parent, &failure);
  // A reference without NUL bytes (checked above) is a C string.
  if (status == HAL_OK)
    status = jb_string_assign(&relative, reference.data, reference.length);
  if (status == HAL_OK)
    status = jh_absolute_path(jb_string_data(&relative),
                              jb_string_data(&parent), &path, &failure);
  if (status == HAL_OK)
    status = find_or_load(files, jb_string_data(&path), out, &failure);
  jb_string_destroy(&path);
  jb_string_destroy(&relative);
  jb_string_destroy(&parent);
  jh_failure_destroy(&failure);
  return status;
}

static hal_status_t copy_text(jb_string_t *out, jb_json_span_t text) {
  const hal_status_t status = jb_string_init(out, jb_heap_allocator());
  return status != HAL_OK || text.length == 0
             ? status
             : jb_string_assign(out, text.data, text.length);
}

/* jh_json_schema_report_fn appending to the reports of the files at user. */
static hal_status_t collect_report(void *user,
                                   const jh_json_schema_error_t *error) {
  static const jh_schema_report_t empty = {0};
  jh_schema_files_t *files = user;
  jh_schema_report_t report = empty;
  hal_status_t status = copy_text(&report.pointer, error->instance_pointer);
  if (status == HAL_OK)
    status = copy_text(&report.uri, error->schema_uri);
  if (status == HAL_OK)
    status = copy_text(&report.rule, error->schema_pointer);
  if (status == HAL_OK)
    status = copy_text(&report.reason, error->reason_code);
  if (status == HAL_OK)
    status = copy_text(&report.message, error->message);
  if (status == HAL_OK)
    status = jb_vector_append(&files->reports, &report, 1);
  if (status != HAL_OK)
    report_destroy(&report);
  return status;
}

/* The validation limits every jh command uses. */
static const jh_json_schema_limits_t schema_limits = {
    .max_work = 50000000,
    .max_ref_depth = 64,
    .max_reports = 1024,
    .max_pointer_bytes = 8192,
    .max_uri_bytes = 32768,
    .max_report_bytes = 1024 * 1024,
    .max_schema_nodes = 100000,
    .max_instance_nodes = 1000000,
    .max_value_bytes = 16384,
    .max_pattern_bytes = 4096,
    .max_regex_bytes = 2 * 1024 * 1024,
    .regex_match_limit = 1000000,
    .regex_depth_limit = 128,
    .regex_heap_limit_kib = 1024};

hal_status_t jh_schema_files_validate(jh_schema_files_t *files,
                                      const jh_json_document_t *instance,
                                      jh_failure_t *failure) {
  if (files == NULL || instance == NULL)
    return jh_fail_arguments(failure);
  if (jb_vector_size(&files->documents) == 0)
    return jh_fail_text(failure, HAL_ESTATE, 3, "no root schema is loaded");
  if (jb_vector_size(&files->scratch) == 0) {
    size_t needed;
    hal_status_t status = jh_require(
        failure, jh_json_schema_workspace_size(&schema_limits, &needed), 4,
        "schema workspace overflow");
    if (status == HAL_OK)
      status = zeroed(&files->scratch, needed, failure);
    if (status != HAL_OK)
      return status;
  }
  clear_reports(files);
  const jh_json_schema_source_t source = source_of(document_at(files, 0));
  const hal_status_t status = jh_json_schema_validate(
      &instance->doc, &source, load_schema, files, collect_report, files,
      workspace_of(&files->scratch), &schema_limits);
  if (status == HAL_OK || status == HAL_ECONFIG)
    return status;
  const bool file_error =
      status == HAL_ENOENT || status == HAL_EIO || status == HAL_EPERM;
  return jh_fail_text(failure, status,
                      exit_code_for(status, file_error ? 3 : 2),
                      "cannot validate schema or validation limit exceeded");
}

hal_status_t jh_json_write_tree(jb_json_writer_t *writer,
                                jb_json_tree_view_t value) {
  jb_json_type_t type = JB_JSON_NULL;
  hal_status_t status = jb_json_tree_type(value, &type);
  jb_json_span_t text = {NULL, 0};
  bool flag = false;
  size_t count = 0;
  if (status != HAL_OK)
    return status;
  switch (type) {
  case JB_JSON_NULL:
    return jb_json_writer_null(writer);
  case JB_JSON_BOOL:
    status = jb_json_tree_get_bool(value, &flag);
    return status == HAL_OK ? jb_json_writer_bool(writer, flag) : status;
  case JB_JSON_NUMBER:
  case JB_JSON_STRING:
    status = jb_json_tree_text(value, &text);
    if (status != HAL_OK)
      return status;
    return type == JB_JSON_NUMBER ? jb_json_writer_number(writer, text)
                                  : jb_json_writer_string(writer, text);
  default:
    break;
  }
  const bool object = type == JB_JSON_OBJECT;
  status = object ? jb_json_writer_object_begin(writer)
                  : jb_json_writer_array_begin(writer);
  if (status == HAL_OK)
    status = jb_json_tree_count(value, &count);
  for (size_t index = 0; status == HAL_OK && index < count; ++index) {
    jb_json_tree_view_t item = {0};
    status = jb_json_tree_at(value, index, &item);
    if (status == HAL_OK && object) {
      status = jb_json_tree_key(item, &text);
      if (status == HAL_OK)
        status = jb_json_writer_key(writer, text);
    }
    if (status == HAL_OK)
      status = jh_json_write_tree(writer, item);
  }
  if (status == HAL_OK)
    status = object ? jb_json_writer_object_end(writer)
                    : jb_json_writer_array_end(writer);
  return status;
}

jb_json_type_t jh_json_type(jb_json_tree_view_t value) {
  jb_json_type_t type = JB_JSON_NULL;
  (void)jb_json_tree_type(value, &type);
  return type;
}

bool jh_json_member(jb_json_tree_view_t object, const char *key,
                    jb_json_tree_view_t *out) {
  return jh_json_type(object) == JB_JSON_OBJECT &&
         jb_json_tree_find(object, jh_span(key), out) == HAL_OK;
}

jb_json_tree_view_t jh_json_field(jb_json_tree_view_t object, const char *key) {
  jb_json_tree_view_t out = {0};
  (void)jh_json_member(object, key, &out);
  return out;
}
