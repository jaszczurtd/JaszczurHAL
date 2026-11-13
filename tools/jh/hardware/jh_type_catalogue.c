#include "jh_catalogue_internal.h"
#include <jb/filesystem.h>
#include <stdlib.h>
#include <string.h>

const jh_c_type_t *jh_c_type(jb_json_span_t name) {
  static const jh_c_type_t types[] = {
      {"bool", JH_C_BOOLEAN, false, 1, JB_C_BOOL},
      {"uint8", JH_C_INTEGER, false, 8, JB_C_U8},
      {"uint16", JH_C_INTEGER, false, 16, JB_C_U16},
      {"uint32", JH_C_INTEGER, false, 32, JB_C_U32},
      {"uint64", JH_C_INTEGER, false, 64, JB_C_U64},
      {"int8", JH_C_INTEGER, true, 8, JB_C_I8},
      {"int16", JH_C_INTEGER, true, 16, JB_C_I16},
      {"int32", JH_C_INTEGER, true, 32, JB_C_I32},
      {"int64", JH_C_INTEGER, true, 64, JB_C_I64},
      {"float", JH_C_FLOATING, true, 32, JB_C_F32},
      {"double", JH_C_FLOATING, true, 64, JB_C_F64},
      {"string", JH_C_STRING, false, 0, JB_C_STRING},
  };
  for (size_t i = 0; i < COUNTOF(types); ++i)
    if (jh_span_equal(name, jh_span(types[i].name)))
      return &types[i];
  return NULL;
}

const char *jh_c_json_type(jh_c_kind_t kind) {
  switch (kind) {
  case JH_C_BOOLEAN:
    return "boolean";
  case JH_C_INTEGER:
    return "integer";
  case JH_C_FLOATING:
    return "number";
  case JH_C_STRING:
    return "string";
  }
  return "";
}

hal_status_t jh_catalogue_init(jh_catalogue_t *types) {
  if (types == NULL)
    return HAL_EINVAL;
  hal_status_t status = jb_vector_init(&types->bindings, sizeof(jh_binding_t *),
                                       jb_heap_allocator());
  if (status == HAL_OK)
    status = jb_map_init(&types->index, sizeof(jb_json_span_t), sizeof(size_t),
                         jh_catalogue_span_order, NULL, jb_heap_allocator());
  return status;
}

static void binding_destroy(jh_binding_t *binding) {
  if (binding == NULL)
    return;
  jh_json_document_destroy(&binding->document);
  jb_string_destroy(&binding->compatible);
  jb_string_destroy(&binding->path);
  free(binding);
}

void jh_catalogue_destroy(jh_catalogue_t *types) {
  if (types == NULL)
    return;
  jb_map_destroy(&types->index);
  for (size_t i = 0; i < jb_vector_size(&types->bindings); ++i)
    binding_destroy(*(jh_binding_t **)jb_vector_at(&types->bindings, i));
  jb_vector_destroy(&types->bindings);
}

const jh_binding_t *jh_catalogue_at(const jh_catalogue_t *types, size_t index) {
  if (types == NULL)
    return NULL;
  const jh_binding_t *const *entry =
      jb_vector_const_at(&types->bindings, index);
  return entry == NULL ? NULL : *entry;
}

const jh_binding_t *jh_catalogue_find(const jh_catalogue_t *types,
                                      jb_json_span_t compatible) {
  if (types == NULL)
    return NULL;
  const void *found = NULL;
  return jb_map_find_const(&types->index, &compatible, &found) == HAL_OK
             ? jh_catalogue_at(types, *(const size_t *)found)
             : NULL;
}

/* Unlike the glob helper, a catalogue requires its directory and includes
 * only regular files (following a binding symlink, as directory_iterator did).
 */
static hal_status_t json_files(const char *directory, jb_vector_t *names,
                               jh_failure_t *failure) {
  jb_fs_info_t info;
  hal_status_t status = jb_fs_status(directory, true, &info);
  if (status == HAL_OK && info.kind != JB_FS_DIRECTORY)
    status = HAL_ETYPE;
  jb_vector_t entries = {0};
  jb_string_t path = {0};
  if (status == HAL_OK)
    status = jh_list_init(&entries);
  if (status == HAL_OK)
    status = jb_string_init(&path, jb_heap_allocator());
  if (status == HAL_OK)
    status = jh_json_names(directory, &entries);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&entries); ++i) {
    const jb_string_t *name = jh_list_at(&entries, i);
    if (jb_string_size(name) <= 5)
      continue;
    status = jh_path_join(directory, jb_string_data(name), &path);
    if (status == HAL_OK)
      status = jb_fs_status(jb_string_data(&path), true, &info);
    if (status == HAL_ENOENT) {
      status = HAL_OK; /* A dangling binding link is not a regular file. */
      continue;
    }
    if (status == HAL_OK && info.kind == JB_FS_REGULAR)
      status = jh_list_push(names, jh_string_span(name));
  }
  jb_string_destroy(&path);
  jh_list_destroy(&entries);
  if (status == HAL_ENOMEM || status == HAL_EOVERFLOW)
    return jh_fail_storage(failure, status);
  if (status != HAL_OK)
    return jh_fail(failure, HAL_ENOENT, 3,
                   JH_PARTS(jh_span("cannot list "), jh_span(directory),
                            jh_span(": "),
                            jh_span(hal_status_to_string(status))));
  jh_list_sort(names);
  return HAL_OK;
}

static hal_status_t read_binding(jh_node_reader_t *reader,
                                 jh_schema_files_t *schemas, const char *path,
                                 jh_binding_t **out) {
  jh_binding_t *entry = calloc(1, sizeof *entry);
  if (entry == NULL)
    return jh_fail_storage(reader->failure, HAL_ENOMEM);
  (void)jb_string_init(&entry->compatible, jb_heap_allocator());
  (void)jb_string_init(&entry->path, jb_heap_allocator());
  entry->origin = "hal";
  hal_status_t status =
      jh_json_read_document(schemas->where, path, schemas->max_bytes, true,
                            &entry->document, reader->failure);
  if (status != HAL_OK && reader->failure->exit_code == 2)
    status = jh_fail_pointer(
        reader->failure, "JH-HW-SCHEMA", jh_span(path), jh_span(""),
        JH_PARTS(jh_string_span(&reader->failure->message)));
  if (status == HAL_OK) {
    status =
        jh_schema_files_validate(schemas, &entry->document, reader->failure);
    if (status == HAL_ECONFIG) {
      const jh_schema_report_t *report = jh_schema_files_report(schemas, 0);
      status = jh_fail_pointer(
          reader->failure, "JH-HW-SCHEMA", jh_span(path),
          report == NULL ? jh_span("") : jh_string_span(&report->pointer),
          JH_PARTS(report == NULL
                       ? jh_span("the binding schema rejects this file")
                       : jh_string_span(&report->message)));
    }
  }
  if (status == HAL_OK) {
    const jb_json_node_t root = jh_binding_root(reader, entry);
    const jb_json_node_t compatible =
        jh_node_member(reader, root, "compatible");
    const jb_json_span_t name = jh_node_string(reader, compatible, false);
    status = reader->status;
    if (status == HAL_OK)
      status = jb_string_assign(&entry->compatible, name.data, name.length);
    if (status == HAL_OK)
      status = jh_append(&entry->path, path);
    if (status != HAL_OK && reader->failure->status == HAL_OK)
      status = jh_fail_storage(reader->failure, status);
  }
  if (status != HAL_OK)
    binding_destroy(entry);
  else
    *out = entry;
  return status;
}

hal_status_t jh_catalogue_load(jh_catalogue_t *types, jh_json_location_t *where,
                               const char *schema_dir, const char *bindings_dir,
                               size_t max_bytes, jh_failure_t *failure) {
  if (types == NULL || where == NULL || schema_dir == NULL ||
      bindings_dir == NULL || failure == NULL)
    return jh_fail_arguments(failure);
  jh_schema_files_t schemas;
  jh_schema_files_init(&schemas, where, max_bytes);
  jh_node_reader_t reader;
  jh_node_reader_init(&reader, failure);
  jb_string_t path = {0}, expected = {0};
  jb_vector_t names = {0};
  (void)jb_string_init(&path, jb_heap_allocator());
  (void)jb_string_init(&expected, jb_heap_allocator());
  jh_node_storage(&reader, jh_list_init(&names));
  if (reader.status == HAL_OK)
    reader.status =
        jh_absolute_path("binding.schema.json", schema_dir, &path, failure);
  const jh_json_document_t *schema_document = NULL;
  if (reader.status == HAL_OK)
    reader.status = jh_schema_files_root(&schemas, jb_string_data(&path),
                                         &schema_document, failure);
  if (reader.status == HAL_OK)
    reader.status = json_files(bindings_dir, &names, failure);
  for (size_t i = 0; reader.status == HAL_OK && i < jb_vector_size(&names);
       ++i) {
    const jb_string_t *name = jh_list_at(&names, i);
    reader.status =
        jh_absolute_path(jb_string_data(name), bindings_dir, &path, failure);
    jh_binding_t *entry = NULL;
    if (reader.status == HAL_OK)
      reader.status =
          read_binding(&reader, &schemas, jb_string_data(&path), &entry);
    if (reader.status != HAL_OK)
      break;
    jh_node_storage(
        &reader, jb_string_assign(&expected, jb_string_data(&entry->compatible),
                                  jb_string_size(&entry->compatible)));
    if (reader.status == HAL_OK) {
      char *bytes = jb_string_mutable_data(&expected);
      for (size_t c = 0; c < jb_string_size(&expected); ++c)
        if (bytes[c] == ',')
          bytes[c] = '-';
      jh_node_storage(&reader, jh_append(&expected, ".json"));
    }
    if (!jh_span_equal(jh_string_span(name), jh_string_span(&expected)))
      jh_catalogue_fail(&reader, "JH-HW-BINDING", jb_string_data(&path),
                        "/compatible",
                        JH_PARTS(jh_span("HAL type file must be named "),
                                 jh_string_span(&expected)));
    const jb_json_span_t compatible = jh_string_span(&entry->compatible);
    const jh_binding_t *existing = jh_catalogue_find(types, compatible);
    if (existing != NULL)
      jh_catalogue_fail(&reader, "JH-HW-BINDING", jb_string_data(&path),
                        "/compatible",
                        JH_PARTS(compatible, jh_span(" is already defined by "),
                                 jh_string_span(&existing->path)));
    const size_t index = jb_vector_size(&types->bindings);
    if (reader.status == HAL_OK)
      jh_node_storage(&reader, jb_vector_append(&types->bindings, &entry, 1));
    if (reader.status != HAL_OK) {
      binding_destroy(entry);
      break;
    }
    /* The vector owns entry now, including when the index cannot grow. */
    jh_node_storage(&reader, jb_map_insert(&types->index, &compatible, &index));
  }
  const hal_status_t status = reader.status;
  jh_list_destroy(&names);
  jb_string_destroy(&expected);
  jb_string_destroy(&path);
  jh_node_reader_destroy(&reader);
  jh_schema_files_destroy(&schemas);
  return status;
}

static hal_status_t object_keys(jh_json_location_t *where, const char *path,
                                size_t max_bytes, const char *field,
                                jb_vector_t *names, jh_failure_t *failure) {
  jh_json_document_t document = {0};
  jh_node_reader_t reader;
  jh_node_reader_init(&reader, failure);
  reader.status =
      jh_json_read_document(where, path, max_bytes, false, &document, failure);
  jb_json_node_t root = {0}, table = {0};
  if (reader.status == HAL_OK)
    jh_node_require(&reader, jb_json_root(&document.doc, &root), 2,
                    "document has no root");
  if (reader.status == HAL_OK &&
      (jh_node_type(&reader, root) != JB_JSON_OBJECT ||
       !jh_node_find(&reader, root, field, &table) ||
       jh_node_type(&reader, table) != JB_JSON_OBJECT))
    reader.status =
        jh_fail(failure, HAL_ECONFIG, 2,
                JH_PARTS(jh_span(path), jh_span(": expected an object with a "),
                         jh_span(field), jh_span(" object")));
  jb_json_node_t member = {0};
  for (bool more = jh_node_first(&reader, table, &member); more;
       more = jh_node_next(&reader, &member)) {
    const jb_json_span_t key = jh_node_string(&reader, member, true);
    if (reader.status == HAL_OK && !jh_list_contains(names, key))
      jh_node_storage(&reader, jh_list_push(names, key));
  }
  const hal_status_t status = reader.status;
  jh_node_reader_destroy(&reader);
  jh_json_document_destroy(&document);
  return status;
}

hal_status_t jh_catalogue_features(jh_json_location_t *where, const char *root,
                                   size_t max_bytes, jb_vector_t *names,
                                   jh_failure_t *failure) {
  if (where == NULL || root == NULL || names == NULL || failure == NULL)
    return jh_fail_arguments(failure);
  jb_string_t directory = {0}, path = {0};
  jb_vector_t files = {0};
  (void)jb_string_init(&directory, jb_heap_allocator());
  (void)jb_string_init(&path, jb_heap_allocator());
  (void)jh_list_init(&files);
  hal_status_t status =
      jh_absolute_path("config/features", root, &directory, failure);
  if (status == HAL_OK)
    status = json_files(jb_string_data(&directory), &files, failure);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&files); ++i) {
    status = jh_absolute_path(jb_string_data(jh_list_at(&files, i)),
                              jb_string_data(&directory), &path, failure);
    if (status == HAL_OK)
      status = object_keys(where, jb_string_data(&path), max_bytes, "symbols",
                           names, failure);
  }
  jh_list_destroy(&files);
  jb_string_destroy(&path);
  jb_string_destroy(&directory);
  return status;
}

hal_status_t jh_catalogue_capabilities(jh_json_location_t *where,
                                       const char *root, size_t max_bytes,
                                       jb_vector_t *names,
                                       jh_failure_t *failure) {
  if (where == NULL || root == NULL || names == NULL || failure == NULL)
    return jh_fail_arguments(failure);
  jb_string_t path = {0};
  (void)jb_string_init(&path, jb_heap_allocator());
  hal_status_t status =
      jh_absolute_path("boards/capabilities.json", root, &path, failure);
  if (status == HAL_OK)
    status = object_keys(where, jb_string_data(&path), max_bytes,
                         "capabilities", names, failure);
  jb_string_destroy(&path);
  return status;
}
