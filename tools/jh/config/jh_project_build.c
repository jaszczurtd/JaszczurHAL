#include "jh_project_build.h"
#include "jh_generated_output.h"
#include "jh_json_check.h"
#include "jh_project_config.h"
#include "jh_python_repr.h"
#include <jb/array.h>
#include <jb/filesystem.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

static jb_json_span_t text(jb_json_tree_view_t value) {
  jb_json_span_t result = {"", 0};
  (void)jh_json_string(value, &result);
  return result;
}

static bool flag(jb_json_tree_view_t object, const char *name) {
  bool value = false;
  (void)jb_json_tree_get_bool(jh_json_field(object, name), &value);
  return value;
}

static bool equals(jb_json_tree_view_t value, const char *expected) {
  return jh_span_equal(text(value), jh_span(expected));
}

static hal_status_t list_copy(jb_vector_t *out, const jb_vector_t *input) {
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(input); ++i)
    status = jh_list_push(out, jh_string_span(jh_list_at(input, i)));
  return status;
}

hal_status_t jh_project_build_write(const char *path, jb_json_span_t content,
                                    bool *changed, jh_failure_t *failure) {
#ifndef _WIN32
  jb_fs_info_t info;
  /* The CLI is single-threaded: read and restore its process umask before
   * publishing. Existing files retain their permissions in the atomic API. */
  if (path != NULL && jb_fs_status(path, false, &info) == HAL_ENOENT) {
    const mode_t mask = umask(0);
    (void)umask(mask);
    return jh_write_generated_mode(path, content, (uint32_t)(0666 & ~mask),
                                   changed, failure);
  }
#endif
  return jh_write_generated(path, content, changed, failure);
}

static hal_status_t array_list(jb_vector_t *out, jb_json_tree_view_t input) {
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(input); ++i)
    status = jh_list_push(out, text(jh_json_item(input, i)));
  return status;
}

static hal_status_t join_list(jb_string_t *out, const jb_vector_t *items,
                              const char *separator) {
  hal_status_t status = HAL_OK;
  jb_string_clear(out);
  for (size_t i = 0; i < jb_vector_size(items); ++i) {
    if (i != 0)
      jh_put(out, &status, separator);
    jh_put_span(out, &status, jh_string_span(jh_list_at(items, i)));
  }
  return status;
}

void jh_project_build_init(jh_project_build_t *build) {
  *build = (jh_project_build_t){0};
  (void)jh_list_init(&build->definitions);
  (void)jh_list_init(&build->effective_definitions);
  (void)jh_list_init(&build->sources);
  (void)jh_list_init(&build->requested);
  (void)jb_vector_init(&build->requests, sizeof(jh_feature_request_t),
                       jb_heap_allocator());
  (void)jb_string_init(&build->label, jb_heap_allocator());
}

void jh_project_build_destroy(jh_project_build_t *build) {
  jb_json_tree_destroy(build->declarations);
  jb_json_tree_destroy(build->config);
  jh_list_destroy(&build->definitions);
  jh_list_destroy(&build->effective_definitions);
  jh_list_destroy(&build->sources);
  jh_list_destroy(&build->requested);
  jb_vector_destroy(&build->requests);
  jb_string_destroy(&build->label);
  *build = (jh_project_build_t){0};
}

static hal_status_t select_variant(const jh_project_build_options_t *options,
                                   jh_project_build_t *build,
                                   jh_failure_t *failure) {
  const jb_json_tree_view_t root = build->declarations_root;
  const jb_json_tree_view_t targets = jh_json_field(root, "targets");
  jb_vector_t names = {0};
  jb_string_t joined = {0}, quoted = {0};
  (void)jh_list_init(&names);
  (void)jb_string_init(&joined, jb_heap_allocator());
  (void)jb_string_init(&quoted, jb_heap_allocator());
  hal_status_t status = array_list(&names, targets);
  const char *header = options->header[0] ? options->header : "None";
  if (status == HAL_OK && jb_vector_size(&names) != 0 &&
      !jh_list_contains(&names, jh_span(options->selector))) {
    status = join_list(&joined, &names, ", ");
    if (status == HAL_OK)
      status = jh_check_fail(
          failure,
          JH_PARTS(jh_span(header),
                   jh_span(": [JH-CFG-TARGET] the project builds for "),
                   jh_string_span(&joined), jh_span(", not "),
                   jh_span(options->selector)));
  }
  if (status == HAL_OK && options->variant[0]) {
    jh_list_destroy(&names);
    (void)jh_list_init(&names);
    const jb_json_tree_view_t variants = jh_json_field(root, "variants");
    for (size_t i = 0; status == HAL_OK && i < jh_json_count(variants); ++i) {
      const jb_json_tree_view_t item = jh_json_item(variants, i);
      const jb_json_span_t id = text(jh_json_field(item, "id"));
      if (jh_span_equal(id, jh_span(options->variant)) &&
          build->variant.tree == NULL)
        build->variant = item;
      status = jh_list_push(&names, id);
    }
    if (status == HAL_OK && build->variant.tree == NULL) {
      status = join_list(&joined, &names, ", ");
      if (status == HAL_OK)
        status = jh_python_repr_append(&quoted, jh_span(options->variant));
      if (status == HAL_OK)
        status = jh_check_fail(
            failure, JH_PARTS(jh_span(header),
                              jh_span(": [JH-CFG-VARIANT] unknown variant "),
                              jh_string_span(&quoted), jh_span("; declared: "),
                              jb_vector_size(&names) ? jh_string_span(&joined)
                                                     : jh_span("none")));
    }
  }
  jb_string_destroy(&quoted);
  jb_string_destroy(&joined);
  jh_list_destroy(&names);
  return status;
}

/* The first dependency whose name is a feature symbol, or a zero view. */
static jb_json_tree_view_t
first_feature_dependency(jb_json_tree_view_t dependencies) {
  for (size_t i = 0; i < jh_json_count(dependencies); ++i) {
    const jb_json_tree_view_t entry = jh_json_item(dependencies, i);
    if (jh_feature_symbol(text(jh_json_field(entry, "name")), false))
      return entry;
  }
  return (jb_json_tree_view_t){0};
}

static hal_status_t check_build(const jh_project_build_options_t *options,
                                jh_project_build_t *build,
                                jh_failure_t *failure) {
  const jb_json_tree_view_t root = build->config_root;
  const jb_json_tree_view_t error = jh_json_field(root, "error");
  if (jh_json_type(error) != JB_JSON_NULL) {
    build->rejected = true;
    return jh_check_fail(
        failure, JH_PARTS(text(jh_json_field(error, "source")),
                          jh_span(": [JH-CFG-ERROR] "),
                          text(jh_json_field(error, "message")), jh_span(" ("),
                          jh_string_span(&build->label), jh_span(")")));
  }
  /* Python raised for the first feature name in sorted order; the reader
   * stores dependencies and each unknown-name list in name order. */
  const jb_json_tree_view_t entry =
      first_feature_dependency(jh_json_field(root, "dependencies"));
  if (entry.tree == NULL)
    return HAL_OK;
  jb_vector_t names = {0};
  jb_string_t joined = {0};
  (void)jh_list_init(&names);
  (void)jb_string_init(&joined, jb_heap_allocator());
  hal_status_t status = array_list(&names, jh_json_field(entry, "unknown"));
  if (status == HAL_OK)
    status = join_list(&joined, &names, ", ");
  if (status == HAL_OK)
    status = jh_check_fail(
        failure,
        JH_PARTS(jh_span(options->header[0] ? options->header : "None"),
                 jh_span(": [JH-CFG-SCOPE] "),
                 text(jh_json_field(entry, "name")), jh_span(" depends on "),
                 jh_string_span(&joined),
                 jh_span(", which the build does not pass "
                         "to the compiler before this header")));
  jb_string_destroy(&joined);
  jh_list_destroy(&names);
  return status;
}

static bool macro_defined(jb_json_tree_view_t macros, jb_json_span_t name) {
  for (size_t i = 0; i < jh_json_count(macros); ++i)
    if (jh_span_equal(text(jh_json_field(jh_json_item(macros, i), "name")),
                      name))
      return true;
  return false;
}

static hal_status_t add_request(jh_project_build_t *build,
                                jh_feature_request_t request) {
  hal_status_t status = jb_vector_append(&build->requests, &request, 1);
  if (status == HAL_OK && !jh_list_contains(&build->requested, request.symbol))
    status = jh_list_push(&build->requested, request.symbol);
  return status;
}

static hal_status_t feature_requests(const jh_project_build_options_t *options,
                                     jh_project_build_t *build) {
  const jb_json_tree_view_t macros =
      jh_json_field(build->config_root, "macros");
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(macros); ++i) {
    const jb_json_tree_view_t item = jh_json_item(macros, i);
    const jb_json_span_t name = text(jh_json_field(item, "name"));
    if (!jh_feature_symbol(name, false) ||
        equals(jh_json_field(item, "source"), "predefined"))
      continue;
    const jb_json_span_t value = text(jh_json_field(item, "body"));
    const jh_feature_request_t request = {
        name, value, text(jh_json_field(item, "source")), value.length != 0};
    status = add_request(build, request);
  }
  jb_string_t origin = {0};
  (void)jb_string_init(&origin, jb_heap_allocator());
  for (size_t i = 0;
       status == HAL_OK && i < jb_vector_size(&build->definitions); ++i) {
    const jb_json_span_t definition =
        jh_string_span(jh_list_at(&build->definitions, i));
    const char *separator = memchr(definition.data, '=', definition.length);
    const jb_json_span_t name = {
        definition.data,
        separator ? (size_t)(separator - definition.data) : definition.length};
    const bool feature = jh_feature_symbol(name, false);
    if (feature && !macro_defined(macros, name))
      continue;
    jb_string_clear(&origin);
    if (i < jb_vector_size(options->extra)) {
      status = jh_append(&origin, "command-line:--define[");
      jh_put_int64(&origin, &status, (int64_t)i);
      jh_put(&origin, &status, "]");
    } else {
      status = jh_join(
          &origin,
          JH_PARTS(jh_span(options->header[0] ? options->header : "None"),
                   jh_span(":JH_PROJECT_VARIANTS("), jh_span(options->variant),
                   jh_span(")")));
    }
    if (status == HAL_OK)
      status = jh_list_push(&build->effective_definitions, definition);
    if (status == HAL_OK)
      status = jh_list_push(&build->sources, jh_string_span(&origin));
    if (status == HAL_OK && feature) {
      const jb_json_span_t value = {
          separator ? separator + 1 : "",
          separator ? definition.length - name.length - 1 : 0};
      const jh_feature_request_t request = {
          name, value,
          jh_string_span(
              jh_list_at(&build->sources, jb_vector_size(&build->sources) - 1)),
          separator != NULL};
      status = add_request(build, request);
    }
  }
  jb_string_destroy(&origin);
  return status;
}

/* Read the header for one set of definitions into the tree of its
 * config.inspect result. */
static hal_status_t read_model(const jh_project_build_options_t *options,
                               const jb_vector_t *definitions,
                               jb_json_tree_t **out, size_t *bytes,
                               jh_failure_t *failure) {
  jh_project_config_t config = {0};
  jb_string_t json = {0};
  jh_project_config_init(&config);
  hal_status_t status = jb_string_init(&json, jb_heap_allocator());
  if (status == HAL_OK)
    status =
        jh_project_config_read(options->header, definitions,
                               options->preinclude, NULL, &config, failure);
  if (status == HAL_OK)
    status = jh_project_config_json(&config, options->header,
                                    options->preinclude, &json, failure);
  if (status == HAL_OK && jb_string_size(&json) > SIZE_MAX - *bytes)
    status = HAL_EOVERFLOW;
  if (status == HAL_OK) {
    *bytes += jb_string_size(&json);
    status = jh_json_tree_from_text(jh_string_span(&json), out);
  }
  jb_string_destroy(&json);
  jh_project_config_destroy(&config);
  // The build reports a rejected header by its message alone.
  jb_string_clear(&failure->code);
  jb_string_clear(&failure->source);
  return status;
}

hal_status_t
jh_project_build_evaluate(const jh_project_build_options_t *options,
                          jh_project_build_t *result, jh_failure_t *failure) {
  if (options == NULL || result == NULL || failure == NULL ||
      options->header == NULL || options->preinclude == NULL ||
      options->target == NULL || options->selector == NULL ||
      options->variant == NULL || options->required == NULL ||
      options->extra == NULL)
    return jh_fail_arguments(failure);
  jb_vector_t predefined = {0};
  (void)jh_list_init(&predefined);
  hal_status_t status = HAL_OK;
  if (options->selector[0])
    status = jh_list_push(&predefined, jh_span(options->selector));
  if (status == HAL_OK)
    status = read_model(options, &predefined, &result->declarations,
                        &result->model_bytes, failure);
  if (status == HAL_OK)
    status =
        jb_json_tree_root(result->declarations, &result->declarations_root);
  if (status == HAL_OK)
    status = select_variant(options, result, failure);
  if (status == HAL_OK)
    status = list_copy(&result->definitions, options->extra);
  if (status == HAL_OK)
    status = array_list(&result->definitions,
                        jh_json_field(result->variant, "definitions"));
  if (status == HAL_OK)
    status = list_copy(&predefined, options->required);
  if (status == HAL_OK)
    status = list_copy(&predefined, &result->definitions);
  if (status == HAL_OK)
    status = read_model(options, &predefined, &result->config,
                        &result->model_bytes, failure);
  if (status == HAL_OK)
    status = jb_json_tree_root(result->config, &result->config_root);
  if (status == HAL_OK)
    status = jh_join(
        &result->label,
        JH_PARTS(jh_span("target="),
                 jh_span(options->target[0] ? options->target : "default"),
                 jh_span(", variant="),
                 jh_span(options->variant[0] ? options->variant : "base")));
  if (status == HAL_OK)
    status = check_build(options, result, failure);
  if (status == HAL_OK)
    status = feature_requests(options, result);
  jh_list_destroy(&predefined);
  return status != HAL_OK && failure->status == HAL_OK
             ? jh_fail_storage(failure, status)
             : status;
}

hal_status_t jh_project_build_validate(
    const jh_project_build_options_t *options, const jh_project_build_t *build,
    const jh_feature_registry_t *registry, jh_failure_t *failure) {
  jb_string_t context = {0}, findings = {0};
  jb_vector_t required = {0};
  jh_feature_resolution_t resolution;
  jh_feature_resolution_init(&resolution);
  (void)jb_string_init(&context, jb_heap_allocator());
  (void)jb_string_init(&findings, jb_heap_allocator());
  (void)jb_vector_init(&required, sizeof(jb_json_span_t), jb_heap_allocator());
  hal_status_t status = jh_join(
      &context,
      JH_PARTS(jh_span(options->header[0] ? options->header
                                          : "hal_project_config.h"),
               jh_span(" ["), jh_string_span(&build->label), jh_span("]")));
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(options->required);
       ++i) {
    const jb_json_span_t item =
        jh_string_span(jh_list_at(options->required, i));
    status = jb_vector_append(&required, &item, 1);
  }
  const jh_feature_target_t target = {jh_span(options->target),
                                      jb_vector_const_data(&required),
                                      jb_vector_size(&required)};
  if (status == HAL_OK)
    status = jh_feature_resolve(
        registry, jb_vector_const_data(&build->requests),
        jb_vector_size(&build->requests), jh_string_span(&context),
        options->target[0] ? &target : NULL, &resolution, failure);
  if (status == HAL_OK && jb_vector_size(&resolution.findings) != 0) {
    status = join_list(&findings, &resolution.findings, "\n");
    if (status == HAL_OK)
      status = jh_fail_text(failure, HAL_ECONFIG, 2, jb_string_data(&findings));
  }
  jh_feature_resolution_destroy(&resolution);
  jb_vector_destroy(&required);
  jb_string_destroy(&findings);
  jb_string_destroy(&context);
  return status != HAL_OK && failure->status == HAL_OK
             ? jh_fail_storage(failure, status)
             : status;
}

/* CMake bracket arguments start with one '=' and grow past any delimiter
 * in the value. The list of exported variables preserves emission order. */
typedef struct {
  jb_string_t *out;
  jb_vector_t names;
  hal_status_t status;
} cmake_writer_t;

/* Match Path.write_text() of the former CMake command on each host. */
#ifdef _WIN32
static const char cmake_newline[] = "\r\n";
#else
static const char cmake_newline[] = "\n";
#endif

static void cmake_value(cmake_writer_t *writer, const char *prefix,
                        jb_json_span_t suffix, jb_json_span_t value) {
  jb_string_t name = {0}, fence = {0};
  (void)jb_string_init(&name, jb_heap_allocator());
  (void)jb_string_init(&fence, jb_heap_allocator());
  if (writer->status == HAL_OK)
    writer->status = jh_join(&name, JH_PARTS(jh_span(prefix), suffix));
  if (writer->status == HAL_OK)
    writer->status = jh_list_push(&writer->names, jh_string_span(&name));
  jh_put(&fence, &writer->status, "=");
  for (;;) {
    bool found = false;
    const size_t width = jb_string_size(&fence);
    for (size_t i = 0; i + width + 1 < value.length; ++i) {
      if (value.data[i] == ']' && value.data[i + width + 1] == ']' &&
          memcmp(value.data + i + 1, jb_string_data(&fence), width) == 0) {
        found = true;
        break;
      }
    }
    if (!found || writer->status != HAL_OK)
      break;
    jh_put(&fence, &writer->status, "=");
  }
  jh_put(writer->out, &writer->status, "set(");
  jh_put_span(writer->out, &writer->status, jh_string_span(&name));
  jh_put(writer->out, &writer->status, " [");
  jh_put_span(writer->out, &writer->status, jh_string_span(&fence));
  jh_put(writer->out, &writer->status, "[");
  jh_put_span(writer->out, &writer->status, value);
  jh_put(writer->out, &writer->status, "]");
  jh_put_span(writer->out, &writer->status, jh_string_span(&fence));
  jh_put(writer->out, &writer->status, "])");
  jh_put(writer->out, &writer->status, cmake_newline);
  jb_string_destroy(&fence);
  jb_string_destroy(&name);
}

static void cmake_list(cmake_writer_t *writer, const char *prefix,
                       jb_json_span_t suffix, const jb_vector_t *values) {
  jb_string_t joined = {0};
  (void)jb_string_init(&joined, jb_heap_allocator());
  if (writer->status == HAL_OK)
    writer->status = join_list(&joined, values, ";");
  if (writer->status == HAL_OK)
    cmake_value(writer, prefix, suffix, jh_string_span(&joined));
  jb_string_destroy(&joined);
}

static void cmake_array(cmake_writer_t *writer, const char *prefix,
                        jb_json_span_t suffix, jb_json_tree_view_t values) {
  jb_vector_t list = {0};
  (void)jh_list_init(&list);
  if (writer->status == HAL_OK)
    writer->status = array_list(&list, values);
  cmake_list(writer, prefix, suffix, &list);
  jh_list_destroy(&list);
}

static int macro_compare(const void *left, const void *right) {
  return jh_span_compare(
      text(jh_json_field(*(const jb_json_tree_view_t *)left, "name")),
      text(jh_json_field(*(const jb_json_tree_view_t *)right, "name")));
}

hal_status_t jh_project_build_cmake(const jh_project_build_options_t *options,
                                    const jh_project_build_t *build,
                                    const jb_vector_t *targets,
                                    jb_string_t *out, jh_failure_t *failure) {
  const jb_json_tree_view_t config = build->config_root;
  const jb_json_span_t empty = {"", 0};
  cmake_writer_t writer = {out, {0}, HAL_OK};
  jb_vector_t list = {0}, macros = {0};
  jb_string_t paths = {0};
  (void)jb_string_init(&paths, jb_heap_allocator());
  (void)jh_list_init(&writer.names);
  (void)jh_list_init(&list);
  (void)jb_vector_init(&macros, sizeof(jb_json_tree_view_t),
                       jb_heap_allocator());
  jb_string_clear(out);
  jh_put(out, &writer.status, "# Generated by jh config cmake; do not edit.");
  jh_put(out, &writer.status, cmake_newline);
  if (writer.status == HAL_OK)
    writer.status = jh_append_posix(&paths, jh_span(options->header));
  cmake_value(&writer, "JH_PROJECT_CONFIG_HEADER", empty,
              jh_string_span(&paths));
  jb_string_clear(&paths);
  const jb_json_tree_view_t files = jh_json_field(config, "files");
  for (size_t i = 0; writer.status == HAL_OK && i < jh_json_count(files); ++i) {
    if (i != 0)
      jh_put(&paths, &writer.status, ";");
    if (writer.status == HAL_OK)
      writer.status = jh_append_posix(&paths, text(jh_json_item(files, i)));
  }
  cmake_value(&writer, "JH_PROJECT_CONFIG_FILES", empty,
              jh_string_span(&paths));
  const jb_json_tree_view_t selected = jh_json_field(config, "targets");
  for (size_t i = 0; writer.status == HAL_OK && i < jh_json_count(selected);
       ++i) {
    const jb_json_span_t selector = text(jh_json_item(selected, i));
    /* project_target_ids maps each selector to its last registry ID. */
    for (size_t j = jb_vector_size(targets); j != 0; --j) {
      const jh_board_descriptor_t *target = jb_vector_const_at(targets, j - 1);
      if (jh_span_equal(text(jh_json_field(jh_json_field(target->root, "hal"),
                                           "targetSelector")),
                        selector)) {
        writer.status = jh_list_push(&list, jh_string_span(&target->id));
        break;
      }
    }
  }
  cmake_list(&writer, "JH_PROJECT_TARGETS", empty, &list);
  jh_list_destroy(&list);
  (void)jh_list_init(&list);
  const jb_json_tree_view_t variants = jh_json_field(config, "variants");
  for (size_t i = 0; writer.status == HAL_OK && i < jh_json_count(variants);
       ++i)
    writer.status = jh_list_push(
        &list, text(jh_json_field(jh_json_item(variants, i), "id")));
  cmake_list(&writer, "JH_PROJECT_VARIANTS", empty, &list);
  cmake_value(&writer, "JH_PROJECT_VARIANT", empty,
              text(jh_json_field(build->variant, "id")));
  cmake_list(&writer, "JH_PROJECT_DEFINITIONS", empty, &build->definitions);
  cmake_list(&writer, "JH_PROJECT_FEATURES", empty, &build->requested);
  const jb_json_tree_view_t all_macros = jh_json_field(config, "macros");
  for (size_t i = 0; writer.status == HAL_OK && i < jh_json_count(all_macros);
       ++i) {
    const jb_json_tree_view_t item = jh_json_item(all_macros, i);
    if (!flag(item, "functionLike"))
      writer.status = jb_vector_append(&macros, &item, 1);
  }
  if (jb_vector_size(&macros) > 1)
    qsort(jb_vector_data(&macros), jb_vector_size(&macros),
          sizeof(jb_json_tree_view_t), macro_compare);
  jh_list_destroy(&list);
  (void)jh_list_init(&list);
  for (size_t i = 0; writer.status == HAL_OK && i < jb_vector_size(&macros);
       ++i)
    writer.status = jh_list_push(
        &list, text(jh_json_field(
                   *(const jb_json_tree_view_t *)jb_vector_const_at(&macros, i),
                   "name")));
  cmake_list(&writer, "JH_PROJECT_MACROS", empty, &list);
  for (size_t i = 0; writer.status == HAL_OK && i < jb_vector_size(&macros);
       ++i) {
    const jb_json_tree_view_t macro =
        *(const jb_json_tree_view_t *)jb_vector_const_at(&macros, i);
    const jb_json_span_t name = text(jh_json_field(macro, "name"));
    const jb_json_tree_view_t error = jh_json_field(macro, "integerError");
    cmake_value(&writer, "JH_PROJECT_DEFINE_", name,
                text(jh_json_field(macro, "body")));
    if (equals(jh_json_field(macro, "integerState"), "error")) {
      if (equals(jh_json_field(error, "status"), "HAL_EOVERFLOW")) {
        cmake_value(&writer, "JH_PROJECT_LIMIT_", name,
                    text(jh_json_field(error, "message")));
      } else {
        writer.status = jh_check_fail(
            failure, JH_PARTS(text(jh_json_field(error, "message"))));
      }
    } else if (equals(jh_json_field(macro, "integerState"), "value")) {
      jb_json_span_t number = empty;
      (void)jb_json_tree_text(jh_json_field(macro, "integer"), &number);
      cmake_value(&writer, "JH_PROJECT_INT_", name, number);
    } else if (equals(jh_json_field(macro, "integerState"), "outOfRange")) {
      cmake_value(&writer, "JH_PROJECT_RANGE_", name,
                  text(jh_json_field(macro, "body")));
    }
  }
  const jb_json_tree_view_t dependencies =
      jh_json_field(config, "dependencies");
  for (size_t i = 0; writer.status == HAL_OK && i < jh_json_count(dependencies);
       ++i) {
    const jb_json_tree_view_t item = jh_json_item(dependencies, i);
    cmake_array(&writer, "JH_PROJECT_UNCERTAIN_",
                text(jh_json_field(item, "name")),
                jh_json_field(item, "unknown"));
  }
  for (size_t i = 0; writer.status == HAL_OK && i < jb_vector_size(&macros);
       ++i) {
    const jb_json_tree_view_t macro =
        *(const jb_json_tree_view_t *)jb_vector_const_at(&macros, i);
    const jb_json_tree_view_t unknown = jh_json_field(macro, "unresolved");
    if (jh_json_count(unknown) != 0)
      cmake_array(&writer, "JH_PROJECT_UNRESOLVED_",
                  text(jh_json_field(macro, "name")), unknown);
  }
  /* Join before appending this variable's own name. */
  cmake_list(&writer, "JH_PROJECT_CONFIG_VARIABLES", empty, &writer.names);
  jb_vector_destroy(&macros);
  jh_list_destroy(&list);
  jh_list_destroy(&writer.names);
  jb_string_destroy(&paths);
  return writer.status != HAL_OK && failure->status == HAL_OK
             ? jh_fail_storage(failure, writer.status)
             : writer.status;
}
