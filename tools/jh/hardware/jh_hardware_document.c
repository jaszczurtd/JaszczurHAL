#include "hal/security/jh_sha256.h"
#include "jh_hardware_internal.h"
#include "jh_json_digest.h"
#include "jh_tool_inputs.h"
#include <jb/filesystem.h>
#include <string.h>

const char *jh_hw_path(jh_hw_t *h, const char *base, const char *name) {
  jb_string_t path = {0};
  (void)jb_string_init(&path, jb_heap_allocator());
  jh_node_storage(&h->memory, jh_path_join(base, name, &path));
  const char *result = jh_hw_join(h, JH_PARTS(jh_string_span(&path)));
  jb_string_destroy(&path);
  return result;
}

void jh_hw_file_digest(jh_hw_t *h, const char *path, char out[65]) {
  if (h->memory.status != HAL_OK)
    return;
  jb_file_data_t data = {0};
  h->memory.status = jb_file_read(path, h->registry->max_bytes, &data);
  if (h->memory.status == HAL_OK) {
    jh_sha256_context_t sha;
    uint8_t bytes[JH_SHA256_DIGEST_BYTES];
    jh_sha256_init(&sha);
    jh_sha256_update(&sha, data.data, data.length);
    jh_sha256_final(&sha, bytes);
    jh_sha256_hex(bytes, out);
  } else {
    (void)jh_fail(h->failure, h->memory.status, 3,
                  JH_PARTS(jh_span(path), jh_span(": cannot read input: "),
                           jh_span(hal_status_to_string(h->memory.status))));
  }
  jb_file_release(&data);
}

typedef struct {
  jh_hw_t *h;
  jb_vector_t paths;
} dependencies_t;

static hal_status_t source_file(void *user, const char *path,
                                jb_json_span_t relative, const char *name) {
  (void)relative;
  dependencies_t *d = user;
  const char *dot = strrchr(name, '.');
  if ((dot != NULL && (strcmp(dot, ".c") == 0 || strcmp(dot, ".h") == 0)) ||
      strcmp(name, "CMakeLists.txt") == 0)
    return jh_list_push(&d->paths, jh_span(path));
  return HAL_OK;
}

static void dependency(jh_hw_t *h, jb_json_tree_t *table, const char *path,
                       bool project) {
  const char *name = NULL;
  if (project) {
    name = jh_hw_source_key(h, path);
  } else {
    const char *root = jb_string_data(&h->registry->root);
    const size_t length = strlen(root);
    if (strncmp(root, path, length) != 0 ||
        (path[length] != '/' && path[length] != '\\')) {
      jh_hw_error(
          h, "JH-HW-GENERATION", (jh_hw_origin_t){path, ""}, NULL,
          JH_PARTS(jh_span("registry input is outside the HAL checkout")));
      return;
    }
    jb_string_t posix = {0};
    (void)jb_string_init(&posix, jb_heap_allocator());
    jh_node_storage(&h->memory,
                    jh_append_posix(&posix, jh_span(path + length + 1)));
    name = jh_hw_join(h, JH_PARTS(jh_span("hal:"), jh_string_span(&posix)));
    jb_string_destroy(&posix);
  }
  char hash[65] = {0};
  jh_hw_file_digest(h, path, hash);
  jh_hw_set(h, table, name, jh_hw_string(h, jh_span(hash)));
}

static jb_json_tree_view_t resolve_dependencies(jh_hw_t *h) {
  jb_json_tree_t *table = jh_hw_object(h);
  dependency(h, table, h->path, true);
  for (size_t i = 0; h->memory.status == HAL_OK &&
                     i < jb_vector_size(&h->project_types.bindings);
       ++i) {
    const jh_binding_t *binding = jh_catalogue_at(&h->project_types, i);
    dependency(h, table, jb_string_data(&binding->path),
               strcmp(binding->origin, "project") == 0);
  }
  dependencies_t d = {h, {0}};
  jh_node_storage(&h->memory, jh_list_init(&d.paths));
  const char *root = jb_string_data(&h->registry->root);
  const char *schema = jb_string_data(&h->registry->schema_dir);
  jh_node_storage(&h->memory, jh_json_names(schema, &d.paths));
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&d.paths);
       ++i)
    dependency(h, table,
               jh_hw_path(h, schema, jb_string_data(jh_list_at(&d.paths, i))),
               false);
  jh_list_destroy(&d.paths);
  jh_node_storage(&h->memory, jh_list_init(&d.paths));
  if (h->memory.status == HAL_OK)
    h->memory.status = jh_walk_files(jh_hw_path(h, root, "tools/jh"),
                                     jh_span(""), source_file, &d, h->failure);
  static const char *const inputs[] = {"boards/capabilities.json",
                                       "config/tooling/board_components.json"};
  for (size_t i = 0; i < COUNTOF(inputs); ++i)
    dependency(h, table, jh_hw_path(h, root, inputs[i]), false);
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < COUNTOF(jh_tool_input_paths); ++i)
    dependency(h, table, jh_hw_path(h, root, jh_tool_input_paths[i]), false);
  dependency(h, table, jb_string_data(&h->target->path), false);
  dependency(h, table, jb_string_data(&h->board->path), false);
  const jh_clock_file_t *clock = jh_clock_registry_find(
      &h->registry->clocks, jh_string_span(&h->target->id));
  if (clock != NULL)
    dependency(h, table, jb_string_data(&clock->path), false);
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&d.paths);
       ++i)
    dependency(h, table, jb_string_data(jh_list_at(&d.paths, i)), false);
  jh_list_destroy(&d.paths);
  const jb_json_tree_view_t values = jh_hw_root(table);
  jb_vector_t keys = {0};
  jh_node_storage(&h->memory, jh_list_init(&keys));
  jh_hw_keys(h, values, &keys);
  jb_json_tree_t *result = jh_hw_array(h);
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&keys);
       ++i) {
    const char *key = jb_string_data(jh_list_at(&keys, i));
    jb_json_tree_t *item = jh_hw_object(h);
    jh_hw_set(h, item, "path", jh_hw_string(h, jh_span(key)));
    jh_hw_set(h, item, "sha256", jh_json_field(values, key));
    jh_hw_append(h, result, jh_hw_root(item));
  }
  jh_list_destroy(&keys);
  return jh_hw_root(result);
}

jb_json_tree_view_t jh_hw_resolved_document(jh_hw_t *h) {
  char hash[65] = {0};
  jh_hw_digest(h, h->model, hash);
  jb_json_tree_t *result = jh_hw_copy(h, h->model);
  jh_hw_set(h, result, "schemaVersion", jh_hw_integer(h, 1));
  jh_hw_set(h, result, "hardwareSha256", jh_hw_string(h, jh_span(hash)));
  jh_hw_set(h, result, "dependencies", resolve_dependencies(h));
  return jh_hw_root(result);
}

hal_status_t jh_hardware_document(jh_hardware_t *hardware, jb_string_t *out,
                                  jh_failure_t *failure) {
  if (hardware == NULL || hardware->state == NULL || out == NULL ||
      failure == NULL)
    return jh_fail_arguments(failure);
  jh_hw_t *h = hardware->state;
  h->failure = h->memory.failure = failure;
  if (h->model.tree == NULL)
    return jh_fail_text(failure, HAL_ESTATE, 3,
                        "hardware has not been resolved");
  const jb_json_tree_view_t document =
      jh_hw_serialized(h, jh_hw_resolved_document(h), false);
  if (jh_hw_phase(h) == HAL_OK)
    jh_node_storage(&h->memory, jh_hw_format(document, 2, out));
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jh_append(out, "\n"));
  return h->memory.status;
}

jb_json_tree_view_t jh_hw_restore(jh_hw_t *h, const char *file,
                                  const char *header, const char *config_dir) {
  h->path = file;
  h->base = config_dir;
  jh_json_document_t document = {0};
  jb_file_data_t bytes = {0};
  jb_string_t expected = {0};
  (void)jb_string_init(&expected, jb_heap_allocator());
  h->memory.status = jh_json_read_document(
      &h->where, file, h->registry->max_bytes, true, &document, h->failure);
  jb_json_node_t node = {0};
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jb_json_root(&document.doc, &node));
  const jb_json_tree_view_t result = jh_hw_import(h, node);
  jb_json_tree_t *model = jh_hw_object(h);
  jh_hw_set(h, model, "selection", jh_json_field(result, "selection"));
  jh_hw_set(h, model, "hardware", jh_json_field(result, "hardware"));
  h->model = jh_hw_root(model);
  char digest[65] = {0};
  jh_hw_digest(h, h->model, digest);
  const char *message = NULL, *source = file;
  if (!jh_hw_is(jh_json_field(result, "hardwareSha256"), digest))
    message = "the hardware model does not match its digest";
  if (h->memory.status == HAL_OK && message == NULL) {
    jh_hw_render_header(h, h->model, digest, &expected);
    h->memory.status = jb_file_read(header, h->registry->max_bytes, &bytes);
    if (h->memory.status != HAL_OK)
      (void)jh_fail(h->failure, h->memory.status, 3,
                    JH_PARTS(jh_span(header),
                             jh_span(": cannot read hardware header: "),
                             jh_span(hal_status_to_string(h->memory.status))));
    if (h->memory.status == HAL_OK &&
        (bytes.length != jb_string_size(&expected) ||
         memcmp(bytes.data, jb_string_data(&expected), bytes.length) != 0)) {
      message = "the header is not the one this model produces";
      source = header;
    }
  }
  const jb_json_tree_view_t dependencies =
      jh_json_field(result, "dependencies");
  if (message == NULL && jh_json_type(dependencies) != JB_JSON_ARRAY)
    message = "the hardware model has no input digests";
  for (size_t i = 0; h->memory.status == HAL_OK && message == NULL &&
                     i < jh_json_count(dependencies);
       ++i) {
    const jb_json_tree_view_t item = jh_json_item(dependencies, i);
    const char *name = jh_hw_text(h, jh_json_field(item, "path"));
    const bool hal = strncmp(name, "hal:", 4) == 0;
    const char *path =
        jh_hw_path(h, hal ? jb_string_data(&h->registry->root) : config_dir,
                   hal ? name + 4 : name);
    jb_fs_info_t info;
    const hal_status_t status = jb_fs_status(path, true, &info);
    if (status == HAL_OK && info.kind == JB_FS_REGULAR)
      jh_hw_file_digest(h, path, digest);
    if (status != HAL_OK || info.kind != JB_FS_REGULAR ||
        !jh_hw_is(jh_json_field(item, "sha256"), digest))
      message = jh_hw_join(
          h, JH_PARTS(jh_span(name),
                      jh_span(" changed after resolve; resolve again")));
  }
  if (message != NULL)
    jh_hw_error(h, "JH-HW-GENERATION", (jh_hw_origin_t){source, ""}, NULL,
                JH_PARTS(jh_span(message)));
  (void)jh_hw_phase(h);
  jb_string_destroy(&expected);
  jb_file_release(&bytes);
  jh_json_document_destroy(&document);
  return result;
}
