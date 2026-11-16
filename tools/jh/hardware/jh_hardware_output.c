#include "jh_hardware_internal.h"
#include "jh_json_digest.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

const char *jh_hw_literal(jh_hw_t *h, const char *type,
                          jb_json_tree_view_t value) {
  const jh_c_type_t *declared = jh_c_type(jh_span(type));
  if (declared == NULL) {
    jh_node_storage(&h->memory, HAL_ESTATE);
    return "";
  }
  jh_json_document_t document = {0};
  const jb_json_node_t node = jh_hw_document(h, value, &document);
  const char *literal = jh_node_literal(&h->memory, node, declared, h->path);
  jh_json_document_destroy(&document);
  return literal;
}

static jb_json_tree_view_t floating(jh_hw_t *h, const char *type,
                                    jb_json_tree_view_t value, bool hash) {
  if (!hash) {
    const char *literal = jh_hw_literal(h, type, value);
    size_t length = strlen(literal);
    if (length > 1 && literal[0] == '(' && literal[length - 1] == ')') {
      ++literal;
      length -= 2;
    }
    if (length != 0 && literal[length - 1] == 'f')
      --length;
    return jh_hw_root(jh_hw_parse_tree(h, (jb_json_span_t){literal, length}));
  }
  jh_json_document_t document = {0};
  const jb_json_node_t node = jh_hw_document(h, value, &document);
  const unsigned width = strcmp(type, "float") == 0 ? 32u : 64u;
  size_t bytes = 0;
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory,
                    jb_json_float_workspace_size(node, width, &bytes));
  jb_vector_t scratch = {0};
  jh_node_storage(&h->memory, jb_vector_init(&scratch, 1, jb_heap_allocator()));
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jb_vector_resize(&scratch, bytes));
  const jb_json_workspace_t work = {jb_vector_data(&scratch), bytes};
  const jb_json_float_options_t options = {true, true, 2000000};
  uint32_t bits32 = 0;
  uint64_t bits64 = 0;
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory,
                    width == 32
                        ? jb_json_get_f32_bits(node, &options, work, &bits32)
                        : jb_json_get_f64_bits(node, &options, work, &bits64));
  char text[17];
  if (width == 32)
    (void)snprintf(text, sizeof text, "%08" PRIx32, bits32);
  else
    (void)snprintf(text, sizeof text, "%016" PRIx64, bits64);
  const jb_json_tree_view_t result = jh_hw_string(h, jh_span(text));
  jb_vector_destroy(&scratch);
  jh_json_document_destroy(&document);
  return result;
}

jb_json_tree_view_t jh_hw_serialized(jh_hw_t *h, jb_json_tree_view_t value,
                                     bool hash) {
  if (h->memory.status != HAL_OK)
    return (jb_json_tree_view_t){0};
  const jb_json_type_t kind = jh_json_type(value);
  if (kind == JB_JSON_OBJECT || kind == JB_JSON_ARRAY) {
    const jb_json_tree_view_t type = jh_json_field(value, "cType");
    const bool typed = jh_hw_is(type, "float") || jh_hw_is(type, "double");
    jh_hw_container_t out;
    jh_hw_container_init(h, &out, kind == JB_JSON_OBJECT);
    for (size_t i = 0; h->memory.status == HAL_OK && i < jh_json_count(value);
         ++i) {
      const jb_json_tree_view_t item = jh_json_item(value, i);
      const jb_json_span_t key =
          kind == JB_JSON_OBJECT ? jh_json_key(item) : (jb_json_span_t){0};
      jh_hw_container_put(h, &out, key,
                          typed && jh_span_equal(key, jh_span("value"))
                              ? floating(h, jh_hw_text(h, type), item, hash)
                              : jh_hw_serialized(h, item, hash));
    }
    return jh_hw_container_finish(h, &out);
  }
  if (kind == JB_JSON_NUMBER) {
    jb_json_span_t token = {0};
    (void)jb_json_tree_text(value, &token);
    bool decimal = false;
    for (size_t i = 0; i < token.length; ++i)
      decimal = decimal || token.data[i] == '.' || token.data[i] == 'e' ||
                token.data[i] == 'E';
    if (decimal) {
      jb_json_tree_t *typed = jh_hw_object(h);
      jh_hw_set(h, typed, "cType", jh_hw_string(h, jh_span("double")));
      jh_hw_set(h, typed, "value", floating(h, "double", value, hash));
      return jh_hw_root(typed);
    }
  }
  return value;
}

void jh_hw_digest(jh_hw_t *h, jb_json_tree_view_t model, char out[65]) {
  const jb_json_tree_view_t value = jh_hw_serialized(h, model, true);
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jh_json_digest(value, false, out));
}

hal_status_t jh_hardware_json(jh_hardware_t *hardware, jb_string_t *out,
                              jh_failure_t *failure) {
  if (hardware == NULL || hardware->state == NULL || out == NULL ||
      failure == NULL)
    return jh_fail_arguments(failure);
  jh_hw_t *h = hardware->state;
  h->failure = h->memory.failure = failure;
  if (h->model.tree == NULL || h->memory.status != HAL_OK)
    return jh_fail_text(failure, HAL_ESTATE, 3,
                        "hardware has no resolved model");
  const jb_json_tree_view_t normalized = jh_hw_serialized(h, h->model, false);
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jh_hw_format(normalized, 2, out));
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jh_append(out, "\n"));
  return h->memory.status;
}
