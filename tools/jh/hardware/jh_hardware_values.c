#include "jh_hardware_internal.h"
#include <inttypes.h>
#include <jb/json_number.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const jb_json_tree_limits_t jh_hw_tree_limits = {
    100000, 128, 16u * 1024u * 1024u, 64u * 1024u * 1024u};

static jb_json_tree_t *owned(jh_hw_t *h, jb_json_tree_t *tree) {
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jb_vector_append(&h->trees, &tree, 1));
  if (h->memory.status != HAL_OK) {
    jb_json_tree_destroy(tree);
    return NULL;
  }
  return tree;
}

jb_json_tree_view_t jh_hw_root(const jb_json_tree_t *tree) {
  jb_json_tree_view_t result = {0};
  if (tree != NULL)
    (void)jb_json_tree_root(tree, &result);
  return result;
}

static jb_json_tree_t *parse_tree(jh_hw_t *h, jb_json_span_t text,
                                  size_t maximum, bool generated) {
  jb_json_tree_t *tree = NULL;
  if (h->memory.status != HAL_OK)
    return NULL;
  if (text.length > maximum) {
    jh_node_storage(&h->memory, HAL_EOVERFLOW);
    return NULL;
  }
  const size_t nodes = text.length < 100000 ? text.length + 1 : 100000;
  /* The generated object may contain many long names. Charge enough for
   * each pair while keeping the ordinary input parser budget unchanged. */
  const size_t comparisons = generated ? nodes * 3 : 64;
  const jb_json_limits_t limits = {text.length,
                                   nodes,
                                   128,
                                   text.length,
                                   4096,
                                   nodes,
                                   text.length > (SIZE_MAX - 4096) / comparisons
                                       ? SIZE_MAX
                                       : text.length * comparisons + 4096};
  jh_node_storage(
      &h->memory,
      jb_json_tree_parse(text, &limits, &jh_hw_tree_limits, 0, &tree, NULL));
  return owned(h, tree);
}

jb_json_tree_t *jh_hw_parse_tree(jh_hw_t *h, jb_json_span_t text) {
  return parse_tree(h, text, 16u * 1024u * 1024u, false);
}

jb_json_tree_t *jh_hw_object(jh_hw_t *h) {
  return jh_hw_parse_tree(h, jh_span("{}"));
}
jb_json_tree_t *jh_hw_array(jh_hw_t *h) {
  return jh_hw_parse_tree(h, jh_span("[]"));
}

jb_json_tree_t *jh_hw_copy(jh_hw_t *h, jb_json_tree_view_t value) {
  if (value.tree == NULL)
    return jh_hw_parse_tree(h, jh_span("null"));
  jb_json_tree_t *tree = NULL;
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory,
                    jb_json_tree_copy(value, &jh_hw_tree_limits, &tree));
  return owned(h, tree);
}

jb_json_tree_view_t jh_hw_import(jh_hw_t *h, jb_json_node_t node) {
  jb_json_tree_t *tree = NULL;
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory,
                    jb_json_tree_create(node, &jh_hw_tree_limits, &tree));
  return jh_hw_root(owned(h, tree));
}

static void encoded_string(jh_hw_t *h, jb_json_span_t text, size_t limit,
                           jb_string_t *out) {
  const jb_json_writer_limits_t limits = {limit, 1, 1, 1, 1};
  size_t bytes = 0;
  jh_node_storage(&h->memory, jb_json_writer_workspace_size(&limits, &bytes));
  void *work = h->memory.status == HAL_OK ? malloc(bytes) : NULL;
  if (work == NULL && h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, HAL_ENOMEM);
  jb_json_writer_t writer = {0};
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory,
                    jb_json_writer_init(&writer, &limits,
                                        (jb_json_workspace_t){work, bytes},
                                        jh_string_sink, out));
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jb_json_writer_string(&writer, text));
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jb_json_writer_finish(&writer, &bytes));
  free(work);
}

jb_json_tree_view_t jh_hw_string(jh_hw_t *h, jb_json_span_t text) {
  jb_string_t encoded = {0};
  (void)jb_string_init(&encoded, jb_heap_allocator());
  encoded_string(h, text, 16u * 1024u * 1024u, &encoded);
  jb_json_tree_t *tree = jh_hw_parse_tree(h, jh_string_span(&encoded));
  jb_string_destroy(&encoded);
  return jh_hw_root(tree);
}

void jh_hw_container_init(jh_hw_t *h, jh_hw_container_t *out, bool object) {
  memset(out, 0, sizeof *out);
  out->object = object;
  out->first = true;
  jh_node_storage(&h->memory, jb_string_init(&out->text, jb_heap_allocator()));
  jh_put(&out->text, &h->memory.status, object ? "{" : "[");
}

void jh_hw_container_put(jh_hw_t *h, jh_hw_container_t *out, jb_json_span_t key,
                         jb_json_tree_view_t value) {
  const size_t maximum = 64u * 1024u * 1024u;
  if (h->memory.status != HAL_OK)
    return;
  if (!out->first)
    jh_put(&out->text, &h->memory.status, ",");
  out->first = false;
  if (out->object) {
    if (jb_string_size(&out->text) >= maximum)
      jh_node_storage(&h->memory, HAL_EOVERFLOW);
    else
      encoded_string(h, key, maximum - jb_string_size(&out->text), &out->text);
    jh_put(&out->text, &h->memory.status, ":");
  }
  if (h->memory.status == HAL_OK) {
    const size_t used = jb_string_size(&out->text);
    if (used >= maximum) {
      jh_node_storage(&h->memory, HAL_EOVERFLOW);
      return;
    }
    const jb_json_tree_format_t options = {maximum - used, 0, false, false};
    size_t written = 0;
    jh_node_storage(&h->memory,
                    jb_json_tree_format(value, &options, jh_string_sink,
                                        &out->text, &written));
  }
}

jb_json_tree_view_t jh_hw_container_finish(jh_hw_t *h, jh_hw_container_t *out) {
  if (jb_string_size(&out->text) >= 64u * 1024u * 1024u)
    jh_node_storage(&h->memory, HAL_EOVERFLOW);
  jh_put(&out->text, &h->memory.status, out->object ? "}" : "]");
  const jb_json_tree_view_t value = jh_hw_root(
      parse_tree(h, jh_string_span(&out->text), 64u * 1024u * 1024u, true));
  jb_string_destroy(&out->text);
  return value;
}

jb_json_tree_view_t jh_hw_integer(jh_hw_t *h, int64_t number) {
  char text[32];
  (void)snprintf(text, sizeof text, "%" PRId64, number);
  return jh_hw_root(jh_hw_parse_tree(h, jh_span(text)));
}

jb_json_tree_view_t jh_hw_boolean(jh_hw_t *h, bool value) {
  return jh_hw_root(jh_hw_parse_tree(h, jh_span(value ? "true" : "false")));
}

void jh_hw_set(jh_hw_t *h, jb_json_tree_t *tree, const char *key,
               jb_json_tree_view_t value) {
  if (value.tree == NULL)
    value = jh_hw_root(jh_hw_parse_tree(h, jh_span("null")));
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jb_json_tree_set(tree, jh_span(key), value));
}

void jh_hw_append(jh_hw_t *h, jb_json_tree_t *tree, jb_json_tree_view_t value) {
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jb_json_tree_append(tree, value));
}

hal_status_t jh_hw_format(jb_json_tree_view_t value, unsigned indent,
                          jb_string_t *out) {
  const jb_json_tree_format_t options = {64u * 1024u * 1024u, indent, true,
                                         false};
  size_t written = 0;
  if (value.tree == NULL)
    return jh_append(out, "null");
  return jb_json_tree_format(value, &options, jh_string_sink, out, &written);
}

const char *jh_hw_join(jh_hw_t *h, const jb_json_span_t *parts, size_t count) {
  return jh_node_join(&h->memory, parts, count);
}

const char *jh_hw_text(jh_hw_t *h, jb_json_tree_view_t value) {
  jb_json_span_t text = {NULL, 0};
  if (jh_json_string(value, &text))
    return jh_hw_join(h, JH_PARTS(text));
  return "";
}

const char *jh_hw_key(jh_hw_t *h, jb_json_tree_view_t value) {
  return jh_hw_join(h, JH_PARTS(jh_json_key(value)));
}

const char *jh_hw_repr(jh_hw_t *h, jb_json_tree_view_t value) {
  jb_string_t repr = {0};
  (void)jb_string_init(&repr, jb_heap_allocator());
  jh_node_storage(&h->memory, value.tree == NULL
                                  ? jh_append(&repr, "None")
                                  : jh_python_repr_json_append(&repr, value));
  const char *result = jh_hw_join(h, JH_PARTS(jh_string_span(&repr)));
  jb_string_destroy(&repr);
  return result;
}

const char *jh_hw_pointer(jh_hw_t *h, const char *base, const char *key) {
  jb_string_t text = {0};
  (void)jb_string_init(&text, jb_heap_allocator());
  jh_put(&text, &h->memory.status, base);
  jh_put(&text, &h->memory.status, "/");
  for (; h->memory.status == HAL_OK && *key != '\0'; ++key) {
    if (*key == '~' || *key == '/')
      jh_node_storage(&h->memory, jh_append(&text, *key == '~' ? "~0" : "~1"));
    else
      jh_node_storage(&h->memory, jb_string_append(&text, key, 1));
  }
  const char *result = jh_hw_join(h, JH_PARTS(jh_string_span(&text)));
  jb_string_destroy(&text);
  return result;
}

const char *jh_hw_token(jh_hw_t *h, const char *path) {
  jb_string_t token = {0};
  (void)jb_string_init(&token, jb_heap_allocator());
  jh_node_storage(&h->memory, jh_macro_token(jh_span(path), &token));
  const char *result = jh_hw_join(h, JH_PARTS(jh_string_span(&token)));
  jb_string_destroy(&token);
  return result;
}

bool jh_hw_is(jb_json_tree_view_t value, const char *text) {
  jb_json_span_t bytes = {NULL, 0};
  return jh_json_string(value, &bytes) && jh_span_equal(bytes, jh_span(text));
}

bool jh_hw_bool(jb_json_tree_view_t value) {
  bool result = false;
  (void)jb_json_tree_get_bool(value, &result);
  return result;
}

jb_json_node_t jh_hw_document(jh_hw_t *h, jb_json_tree_view_t value,
                              jh_json_document_t *document) {
  jb_string_t text = {0};
  (void)jb_string_init(&text, jb_heap_allocator());
  jh_node_storage(&h->memory, jh_hw_format(value, 0, &text));
  if (h->memory.status == HAL_OK)
    h->memory.status = jh_json_parse_document(
        &h->where, "<resolved value>", jh_string_span(&text),
        64u * 1024u * 1024u, document, h->failure);
  jb_json_node_t result = {0};
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jb_json_root(&document->doc, &result));
  jb_string_destroy(&text);
  return result;
}

bool jh_hw_equal(jh_hw_t *h, jb_json_tree_view_t a, jb_json_tree_view_t b) {
  const jb_json_type_t kind = jh_json_type(a);
  if (h->memory.status != HAL_OK || kind != jh_json_type(b))
    return false;
  if (kind == JB_JSON_NULL)
    return true;
  if (kind == JB_JSON_BOOL)
    return jh_hw_bool(a) == jh_hw_bool(b);
  if (kind == JB_JSON_STRING) {
    jb_json_span_t left = {0}, right = {0};
    (void)jh_json_string(a, &left);
    (void)jh_json_string(b, &right);
    return jh_span_equal(left, right);
  }
  int64_t left_integer = 0, right_integer = 0;
  if (kind == JB_JSON_NUMBER && jh_json_int64(a, &left_integer) &&
      jh_json_int64(b, &right_integer))
    return left_integer == right_integer;
  jh_json_document_t x = {0}, y = {0};
  const jb_json_node_t left = jh_hw_document(h, a, &x);
  const jb_json_node_t right = jh_hw_document(h, b, &y);
  const bool same = jh_node_equal(&h->memory, left, right);
  jh_json_document_destroy(&y);
  jh_json_document_destroy(&x);
  return same;
}

bool jh_hw_i64(jh_hw_t *h, jb_json_tree_view_t value, int64_t *out) {
  if (jh_json_int64(value, out))
    return true;
  if (jh_json_type(value) != JB_JSON_NUMBER)
    return false;
  jh_json_document_t document = {0};
  const jb_json_node_t node = jh_hw_document(h, value, &document);
  const bool valid =
      h->memory.status == HAL_OK && jb_json_get_i64(node, out) == HAL_OK;
  jh_json_document_destroy(&document);
  return valid;
}

void jh_hw_keys(jh_hw_t *h, jb_json_tree_view_t object, jb_vector_t *keys) {
  for (size_t i = 0; h->memory.status == HAL_OK && i < jh_json_count(object);
       ++i)
    jh_node_storage(&h->memory,
                    jh_list_push(keys, jh_json_key(jh_json_item(object, i))));
  jh_list_sort(keys);
}

static bool path_separator(char c) {
#ifdef _WIN32
  if (c == '\\')
    return true;
#endif
  return c == '/';
}

static bool path_character_equal(char a, char b) {
#ifdef _WIN32
  return (path_separator(a) && path_separator(b)) ||
         jh_ascii_to_upper(a) == jh_ascii_to_upper(b);
#else
  return a == b;
#endif
}

#ifdef _WIN32
static size_t volume_length(const char *path) {
  if (path[0] != '\0' && path[1] == ':')
    return 2;
  if (!path_separator(path[0]) || !path_separator(path[1]))
    return 0;
  size_t i = 2;
  for (unsigned component = 0; component < 2; ++component) {
    while (path[i] != '\0' && !path_separator(path[i]))
      ++i;
    if (component == 0 && path[i] != '\0')
      ++i;
  }
  return i;
}
#endif

const char *jh_hw_source_key(jh_hw_t *h, const char *path) {
  /* Normalized absolute inputs. Other Windows volumes retain an absolute
   * location, as os.path.relpath cannot express a path across volumes. */
  const char *base = h->base;
  size_t common = 0, i = 0;
  while (base[i] != '\0' && path[i] != '\0' &&
         path_character_equal(base[i], path[i])) {
    if (path_separator(base[i]))
      common = i + 1;
    ++i;
  }
#ifdef _WIN32
  const size_t base_volume = volume_length(base),
               path_volume = volume_length(path);
  if (base_volume != path_volume || i < base_volume)
    return path;
#endif
  if (base[i] == '\0' && path[i] == '\0')
    return ".";
  if (base[i] == '\0' && path_separator(path[i]))
    common = i + 1;
  jb_string_t relative = {0};
  (void)jb_string_init(&relative, jb_heap_allocator());
  if (common <= strlen(base)) {
    bool component = false;
    for (i = common; base[i] != '\0'; ++i) {
      if (path_separator(base[i])) {
        if (component)
          jh_node_storage(&h->memory, jh_append(&relative, "../"));
        component = false;
      } else {
        component = true;
      }
    }
    if (component)
      jh_node_storage(&h->memory, jh_append(&relative, "../"));
  }
  jh_node_storage(&h->memory,
                  jh_append_posix(&relative, jh_span(path + common)));
  const char *result = jh_hw_join(h, JH_PARTS(jh_string_span(&relative)));
  jb_string_destroy(&relative);
  return result;
}

const char *jh_hw_definition_name(jh_hw_t *h, jb_json_span_t definition) {
  const char *equals = memchr(definition.data, '=', definition.length);
  if (equals != NULL)
    definition.length = (size_t)(equals - definition.data);
  return jh_hw_join(h, &definition, 1);
}

void jh_hw_record(jh_hw_t *h, const jh_failure_t *failure) {
  if (h->memory.status != HAL_OK)
    return;
  if (failure->status != HAL_ECONFIG) {
    jh_node_storage(&h->memory, failure->status);
    return;
  }
  const char *source = jh_hw_source_key(h, jb_string_data(&failure->source));
  int order = h->errors == 0 ? -1 : strcmp(source, h->first_source_key);
  if (order == 0)
    order = jh_json_pointer_compare(jh_string_span(&failure->pointer),
                                    jh_string_span(&h->first.pointer));
  if (order == 0)
    order = jh_span_compare(jh_string_span(&failure->code),
                            jh_string_span(&h->first.code));
  ++h->errors;
  if (order >= 0)
    return;
  h->first_source_key = source;
  h->first.status = failure->status;
  h->first.exit_code = failure->exit_code;
  h->first.line = failure->line;
  h->first.column = failure->column;
  jh_node_storage(
      &h->memory,
      jh_join(&h->first.message, JH_PARTS(jh_string_span(&failure->message))));
  jh_node_storage(
      &h->memory,
      jh_join(&h->first.code, JH_PARTS(jh_string_span(&failure->code))));
  jh_node_storage(
      &h->memory,
      jh_join(&h->first.source, JH_PARTS(jh_string_span(&failure->source))));
  jh_node_storage(
      &h->memory,
      jh_join(&h->first.pointer, JH_PARTS(jh_string_span(&failure->pointer))));
  jh_node_storage(
      &h->memory,
      jh_join(&h->first.detail, JH_PARTS(jh_string_span(&failure->detail))));
  jh_node_storage(
      &h->memory,
      jh_join(&h->first.related, JH_PARTS(jh_string_span(&failure->related))));
  jh_node_storage(&h->memory,
                  jh_join(&h->first.argument,
                          JH_PARTS(jh_string_span(&failure->argument))));
}

void jh_hw_error(jh_hw_t *h, const char *code, jh_hw_origin_t origin,
                 const char *related, const jb_json_span_t *parts,
                 size_t count) {
  if (h->memory.status != HAL_OK)
    return;
  jb_string_t message = {0};
  (void)jb_string_init(&message, jb_heap_allocator());
  jh_node_storage(&h->memory, jh_join(&message, parts, count));
  if (related != NULL) {
    jh_put(&message, &h->memory.status, " (earlier: ");
    jh_put(&message, &h->memory.status, related);
    jh_put(&message, &h->memory.status, ")");
  }
  if (h->memory.status == HAL_OK) {
    jh_failure_t failure;
    jh_failure_init(&failure);
    (void)jh_fail_pointer(&failure, code, jh_span(origin.source),
                          jh_span(origin.pointer),
                          JH_PARTS(jh_string_span(&message)));
    if (related != NULL)
      jh_node_storage(&h->memory, jh_append(&failure.related, related));
    jh_hw_record(h, &failure);
    jh_failure_destroy(&failure);
  }
  jb_string_destroy(&message);
}

hal_status_t jh_hw_phase(jh_hw_t *h) {
  if (h->memory.status != HAL_OK)
    return h->failure->status == h->memory.status
               ? h->memory.status
               : jh_fail_storage(h->failure, h->memory.status);
  if (h->errors != 0) {
    jh_failure_destroy(h->failure);
    *h->failure = h->first;
    memset(&h->first, 0, sizeof h->first);
    h->memory.status = HAL_ECONFIG;
    return HAL_ECONFIG;
  }
  return HAL_OK;
}

const jh_hw_type_t *jh_hw_type(jh_hw_t *h, const char *compatible) {
  for (size_t i = 0; i < jb_vector_size(&h->types); ++i) {
    const jh_hw_type_t *type = jb_vector_const_at(&h->types, i);
    if (strcmp(jb_string_data(&type->binding->compatible), compatible) == 0)
      return type;
  }
  return NULL;
}

jh_hw_node_t *jh_hw_node(jh_hw_t *h, const char *path) {
  for (size_t i = 0; i < jb_vector_size(&h->nodes); ++i) {
    jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    if (strcmp(node->path, path) == 0)
      return node;
  }
  return NULL;
}

jh_hw_property_t *jh_hw_property(jh_hw_node_t *node, const char *name) {
  for (size_t i = 0; i < jb_vector_size(&node->properties); ++i) {
    jh_hw_property_t *value = jb_vector_at(&node->properties, i);
    if (strcmp(value->name, name) == 0)
      return value;
  }
  return NULL;
}

jh_hw_pin_t *jh_hw_pin(jh_hw_node_t *node, const char *name) {
  for (size_t i = 0; i < jb_vector_size(&node->pins); ++i) {
    jh_hw_pin_t *value = jb_vector_at(&node->pins, i);
    if (strcmp(value->name, name) == 0)
      return value;
  }
  return NULL;
}
