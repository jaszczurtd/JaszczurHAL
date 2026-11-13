#include "jh_catalogue_internal.h"
#include <stdio.h>
#include <string.h>

void jh_node_reader_init(jh_node_reader_t *reader, jh_failure_t *failure) {
  memset(reader, 0, sizeof *reader);
  reader->status = HAL_OK;
  reader->failure = failure;
  jh_node_storage(reader, jh_list_init(&reader->strings));
}

void jh_node_reader_destroy(jh_node_reader_t *reader) {
  jh_list_destroy(&reader->strings);
}

void jh_node_require(jh_node_reader_t *reader, hal_status_t status,
                     int exit_code, const char *message) {
  if (reader->status == HAL_OK && status != HAL_OK)
    reader->status = jh_require(reader->failure, status, exit_code, message);
}

void jh_node_storage(jh_node_reader_t *reader, hal_status_t status) {
  if (reader->status == HAL_OK && status != HAL_OK)
    reader->status = jh_fail_storage(reader->failure, status);
}

jb_json_node_t jh_binding_root(jh_node_reader_t *reader,
                               const jh_binding_t *binding) {
  jb_json_node_t node = {0};
  if (reader->status == HAL_OK)
    jh_node_require(reader, jb_json_root(&binding->document.doc, &node), 3,
                    "binding has no root");
  return node;
}

jb_json_type_t jh_node_type(jh_node_reader_t *reader, jb_json_node_t node) {
  jb_json_type_t kind = JB_JSON_NULL;
  if (reader->status == HAL_OK)
    jh_node_require(reader, jb_json_type(node, &kind), 3,
                    "cannot read a JSON node");
  return kind;
}

bool jh_node_find(jh_node_reader_t *reader, jb_json_node_t node,
                  const char *name, jb_json_node_t *out) {
  if (reader->status != HAL_OK)
    return false;
  const hal_status_t status = jb_json_find(node, jh_span(name), out);
  if (status == HAL_ENOENT)
    return false;
  jh_node_require(reader, status, 3, "cannot search a JSON object");
  return status == HAL_OK;
}

jb_json_node_t jh_node_member(jh_node_reader_t *reader, jb_json_node_t node,
                              const char *name) {
  jb_json_node_t out = {0};
  if (!jh_node_find(reader, node, name, &out) && reader->status == HAL_OK)
    reader->status =
        jh_fail(reader->failure, HAL_ENOENT, 3,
                JH_PARTS(jh_span("the schema-validated document lacks "),
                         jh_span(name)));
  return out;
}

jb_json_span_t jh_node_string(jh_node_reader_t *reader, jb_json_node_t node,
                              bool key) {
  if (reader->status != HAL_OK)
    return jh_span("");
  const char *message =
      key ? "cannot read a JSON member name" : "cannot read a JSON string";
  size_t needed = 0;
  jh_node_require(reader,
                  key ? jb_json_key_copy(node, NULL, 0, &needed)
                      : jb_json_string_copy(node, NULL, 0, &needed),
                  3, message);
  jb_string_t text = {0};
  hal_status_t status = jb_string_init(&text, jb_heap_allocator());
  if (status == HAL_OK && reader->status == HAL_OK)
    status = jb_string_resize(&text, needed, '\0');
  jh_node_storage(reader, status);
  if (reader->status == HAL_OK) {
    char *bytes = jb_string_mutable_data(&text);
    jh_node_require(reader,
                    key ? jb_json_key_copy(node, bytes, needed, &needed)
                        : jb_json_string_copy(node, bytes, needed, &needed),
                    3, message);
  }
  if (reader->status == HAL_OK)
    jh_node_storage(reader, jb_vector_append(&reader->strings, &text, 1));
  if (reader->status != HAL_OK) {
    jb_string_destroy(&text);
    return jh_span("");
  }
  return jh_string_span(&text);
}

const char *jh_node_text(jh_node_reader_t *reader, jb_json_node_t node,
                         bool key) {
  return jh_node_string(reader, node, key).data;
}

const char *jh_node_join(jh_node_reader_t *reader, const jb_json_span_t *parts,
                         size_t count) {
  if (reader->status == HAL_OK)
    jh_node_storage(reader, jh_list_push_parts(&reader->strings, parts, count));
  return reader->status == HAL_OK
             ? jb_string_data(jh_list_at(&reader->strings,
                                         jb_vector_size(&reader->strings) - 1))
             : "";
}

const char *jh_node_index(jh_node_reader_t *reader, size_t index) {
  char text[3 * sizeof(size_t) + 1];
  (void)snprintf(text, sizeof text, "%zu", index);
  return jh_node_join(reader, JH_PARTS(jh_span(text)));
}

jb_json_span_t jh_node_raw(jh_node_reader_t *reader, jb_json_node_t node) {
  jb_json_span_t raw = {NULL, 0};
  if (reader->status == HAL_OK)
    jh_node_require(reader, jb_json_raw(node, &raw), 3,
                    "cannot read a JSON value");
  return raw;
}

bool jh_node_bool(jh_node_reader_t *reader, jb_json_node_t node) {
  bool value = false;
  if (reader->status == HAL_OK)
    jh_node_require(reader, jb_json_get_bool(node, &value), 3,
                    "cannot read a JSON boolean");
  return value;
}

size_t jh_node_count(jh_node_reader_t *reader, jb_json_node_t node) {
  size_t count = 0;
  if (reader->status == HAL_OK)
    jh_node_require(reader, jb_json_count(node, &count), 3,
                    "cannot count a JSON container");
  return count;
}

bool jh_node_first(jh_node_reader_t *reader, jb_json_node_t node,
                   jb_json_node_t *out) {
  if (reader->status != HAL_OK)
    return false;
  const hal_status_t status = jb_json_first(node, out);
  if (status == HAL_ENOENT)
    return false;
  jh_node_require(reader, status, 3, "cannot iterate a JSON container");
  return status == HAL_OK;
}

bool jh_node_next(jh_node_reader_t *reader, jb_json_node_t *node) {
  if (reader->status != HAL_OK)
    return false;
  const hal_status_t status = jb_json_next(*node, node);
  if (status == HAL_ENOENT)
    return false;
  jh_node_require(reader, status, 3, "cannot iterate a JSON container");
  return status == HAL_OK;
}

int jh_catalogue_span_order(const void *left, const void *right, void *user) {
  (void)user;
  return jh_span_compare(*(const jb_json_span_t *)left,
                         *(const jb_json_span_t *)right);
}

void jh_catalogue_fail(jh_node_reader_t *reader, const char *code,
                       const char *source, const char *pointer,
                       const jb_json_span_t *parts, size_t count) {
  if (reader->status == HAL_OK)
    reader->status = jh_fail_pointer(reader->failure, code, jh_span(source),
                                     jh_span(pointer), parts, count);
}
