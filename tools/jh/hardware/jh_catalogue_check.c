#include "jh_catalogue_internal.h"
#include "jh_python_repr.h"
#include <jb/json_number.h>
#include <jb/unicode.h>
#include <jb/utf8.h>
#include <string.h>

static const char *const binding_code = "JH-HW-BINDING";
static const char *const property_code = "JH-HW-PROPERTY";

static int number_compare(jh_node_reader_t *r, jb_json_node_t a,
                          jb_json_node_t b) {
  size_t bytes = 0;
  jh_node_require(r, jb_json_number_compare_workspace_size(a, b, &bytes), 4,
                  "number comparison workspace overflow");
  jb_vector_t scratch = {0};
  jh_node_storage(r, jb_vector_init(&scratch, 1, jb_heap_allocator()));
  if (r->status == HAL_OK)
    jh_node_storage(r, jb_vector_resize(&scratch, bytes));
  const jb_json_span_t ar = jh_node_raw(r, a), br = jh_node_raw(r, b);
  int order = 0;
  if (r->status == HAL_OK) {
    const jb_json_workspace_t work = {jb_vector_data(&scratch), bytes};
    jh_node_require(
        r,
        jb_json_number_compare(a, b, work,
                               64 * (ar.length + br.length) + 1000000, &order),
        4, "number comparison budget exceeded");
  }
  jb_vector_destroy(&scratch);
  return order;
}

static bool json_equal(jh_node_reader_t *r, jb_json_node_t a,
                       jb_json_node_t b) {
  const jb_json_type_t kind = jh_node_type(r, a);
  if (kind != jh_node_type(r, b) || r->status != HAL_OK)
    return false;
  switch (kind) {
  case JB_JSON_NULL:
    return true;
  case JB_JSON_BOOL:
    return jh_node_bool(r, a) == jh_node_bool(r, b);
  case JB_JSON_NUMBER:
    return number_compare(r, a, b) == 0;
  case JB_JSON_STRING: {
    const jb_json_span_t left = jh_node_string(r, a, false);
    const jb_json_span_t right = jh_node_string(r, b, false);
    return jh_span_equal(left, right);
  }
  case JB_JSON_ARRAY: {
    if (jh_node_count(r, a) != jh_node_count(r, b))
      return false;
    jb_json_node_t left = {0}, right = {0};
    bool more = jh_node_first(r, a, &left);
    (void)jh_node_first(r, b, &right);
    for (; more; more = jh_node_next(r, &left)) {
      if (!json_equal(r, left, right))
        return false;
      (void)jh_node_next(r, &right);
    }
    return r->status == HAL_OK;
  }
  default: {
    if (jh_node_count(r, a) != jh_node_count(r, b))
      return false;
    jb_json_node_t left = {0};
    for (bool more = jh_node_first(r, a, &left); more;
         more = jh_node_next(r, &left)) {
      jb_json_node_t right = {0};
      const char *key = jh_node_text(r, left, true);
      if (!jh_node_find(r, b, key, &right) || !json_equal(r, left, right))
        return false;
    }
    return r->status == HAL_OK;
  }
  }
}

/* A value's repr in the validation diagnostic, preserving number tokens. */
static void python_repr(jh_node_reader_t *r, jb_json_node_t node,
                        jb_string_t *out) {
  switch (jh_node_type(r, node)) {
  case JB_JSON_NULL:
    jh_node_storage(r, jh_append(out, "None"));
    return;
  case JB_JSON_BOOL:
    jh_node_storage(r,
                    jh_append(out, jh_node_bool(r, node) ? "True" : "False"));
    return;
  case JB_JSON_NUMBER:
    jh_node_storage(r, jh_append_span(out, jh_node_raw(r, node)));
    return;
  case JB_JSON_STRING:
    jh_node_storage(r,
                    jh_python_repr_append(out, jh_node_string(r, node, false)));
    return;
  default: {
    jh_node_storage(r, jh_append(out, "["));
    jb_json_node_t item = {0};
    bool first = true;
    for (bool more = jh_node_first(r, node, &item); more;
         more = jh_node_next(r, &item)) {
      if (!first)
        jh_node_storage(r, jh_append(out, ", "));
      first = false;
      python_repr(r, item, out);
    }
    jh_node_storage(r, jh_append(out, "]"));
  }
  }
}

static bool floating_fits(jh_node_reader_t *r, jb_json_node_t node,
                          unsigned bits) {
  size_t bytes = 0;
  if (jb_json_float_workspace_size(node, bits, &bytes) != HAL_OK)
    return false;
  jb_vector_t scratch = {0};
  jh_node_storage(r, jb_vector_init(&scratch, 1, jb_heap_allocator()));
  if (r->status == HAL_OK)
    jh_node_storage(r, jb_vector_resize(&scratch, bytes));
  const jb_json_span_t raw = jh_node_raw(r, node);
  hal_status_t status = r->status;
  if (status == HAL_OK) {
    const jb_json_float_options_t options = {true, true,
                                             64 * raw.length + 1000000};
    const jb_json_workspace_t work = {jb_vector_data(&scratch), bytes};
    uint32_t out32 = 0;
    uint64_t out64 = 0;
    status = bits == 32 ? jb_json_get_f32_bits(node, &options, work, &out32)
                        : jb_json_get_f64_bits(node, &options, work, &out64);
  }
  jb_vector_destroy(&scratch);
  return status == HAL_OK;
}

static bool value_fits(jh_node_reader_t *r, jb_json_node_t node,
                       const jh_c_type_t *type) {
  const jb_json_type_t kind = jh_node_type(r, node);
  if (r->status != HAL_OK)
    return false;
  switch (type->category) {
  case JH_C_BOOLEAN:
    return kind == JB_JSON_BOOL;
  case JH_C_STRING:
    return kind == JB_JSON_STRING;
  case JH_C_INTEGER: {
    if (kind != JB_JSON_NUMBER)
      return false;
    if (type->is_signed) {
      int64_t number = 0;
      if (jb_json_get_i64(node, &number) != HAL_OK)
        return false;
      if (type->bits == 64)
        return true;
      const int64_t limit = INT64_C(1) << (type->bits - 1);
      return number >= -limit && number < limit;
    }
    uint64_t number = 0;
    if (jb_json_get_u64(node, &number) != HAL_OK)
      return false;
    return type->bits == 64 || number < (UINT64_C(1) << type->bits);
  }
  case JH_C_FLOATING:
    return kind == JB_JSON_NUMBER && floating_fits(r, node, type->bits);
  }
  return false;
}

static bool unit_valid(jh_node_reader_t *r, jb_json_span_t unit) {
  bool first = true, last_space = false;
  for (size_t offset = 0; r->status == HAL_OK && offset < unit.length;) {
    uint32_t scalar = 0;
    size_t used = 0;
    jh_node_require(r,
                    jb_utf8_decode(unit.data + offset, unit.length - offset,
                                   &scalar, &used),
                    2, "unit is not valid UTF-8");
    jb_unicode_properties_t property = {0};
    if (r->status == HAL_OK)
      jh_node_require(r, jb_unicode_properties(scalar, &property), 2,
                      "unit has an invalid scalar");
    if (r->status != HAL_OK || !property.printable ||
        (first && property.whitespace))
      return false;
    first = false;
    last_space = property.whitespace;
    offset += used;
  }
  if (first)
    return r->status == HAL_OK;
  if (last_space)
    return false;
  size_t elements = 0;
  const size_t budget = unit.length * unit.length * 4 + 4096;
  jh_node_require(
      r, jb_unicode_nfc_measure(unit.data, unit.length, budget, &elements), 4,
      "unit normalization budget exceeded");
  jb_vector_t scratch = {0};
  jh_node_storage(
      r, jb_vector_init(&scratch, sizeof(int32_t), jb_heap_allocator()));
  if (r->status == HAL_OK)
    jh_node_storage(r, jb_vector_resize(&scratch, elements));
  const char *text = NULL;
  size_t length = 0;
  if (r->status == HAL_OK)
    jh_node_require(r,
                    jb_unicode_nfc(unit.data, unit.length,
                                   jb_vector_data(&scratch), elements, budget,
                                   &text, &length),
                    4, "unit normalization failed");
  const bool valid = r->status == HAL_OK && length == unit.length &&
                     (length == 0 || memcmp(text, unit.data, length) == 0);
  jb_vector_destroy(&scratch);
  return valid;
}

static void property_value(jh_node_reader_t *r, jb_json_node_t definition,
                           jb_json_node_t value, const char *pointer,
                           const char *source) {
  const jb_json_node_t declared = jh_node_member(r, definition, "cType");
  const jb_json_span_t name = jh_node_string(r, declared, false);
  const jh_c_type_t *type = jh_c_type(name);
  if (type == NULL || !value_fits(r, value, type)) {
    jh_catalogue_fail(r, property_code, source, pointer,
                      JH_PARTS(jh_span("a "), name, jh_span(" value")));
    return;
  }
  jb_json_node_t members = {0};
  if (jh_node_find(r, definition, "enum", &members)) {
    bool listed = false;
    jb_json_node_t member = {0};
    for (bool more = jh_node_first(r, members, &member); more;
         more = jh_node_next(r, &member))
      if (json_equal(r, value, member)) {
        listed = true;
        break;
      }
    if (!listed) {
      jb_string_t repr = {0};
      (void)jb_string_init(&repr, jb_heap_allocator());
      python_repr(r, members, &repr);
      jh_catalogue_fail(r, property_code, source, pointer,
                        JH_PARTS(jh_span("one of "), jh_string_span(&repr)));
      jb_string_destroy(&repr);
      return;
    }
  }
  if (jh_node_type(r, value) != JB_JSON_NUMBER)
    return;
  jb_json_node_t bound = {0};
  if (jh_node_find(r, definition, "minimum", &bound) &&
      number_compare(r, value, bound) < 0)
    jh_catalogue_fail(r, property_code, source, pointer,
                      JH_PARTS(jh_span("at least "), jh_node_raw(r, bound)));
  if (jh_node_find(r, definition, "maximum", &bound) &&
      number_compare(r, value, bound) > 0)
    jh_catalogue_fail(r, property_code, source, pointer,
                      JH_PARTS(jh_span("at most "), jh_node_raw(r, bound)));
}

static void properties(jh_node_reader_t *r, const jh_binding_t *entry) {
  const char *source = jb_string_data(&entry->path);
  jb_json_node_t table = {0}, definition = {0};
  if (!jh_node_find(r, jh_binding_root(r, entry), "properties", &table))
    return;
  for (bool more = jh_node_first(r, table, &definition); more;
       more = jh_node_next(r, &definition)) {
    const char *pointer =
        jh_node_join(r, JH_PARTS(jh_span("/properties/"),
                                 jh_node_string(r, definition, true)));
    const jb_json_node_t declared = jh_node_member(r, definition, "cType");
    const jb_json_span_t name = jh_node_string(r, declared, false);
    const jb_json_node_t json_declared = jh_node_member(r, definition, "type");
    const jb_json_span_t json_name = jh_node_string(r, json_declared, false);
    const jh_c_type_t *type = jh_c_type(name);
    if (type == NULL ||
        !jh_span_equal(jh_span(jh_c_json_type(type->category)), json_name)) {
      const char *at =
          jh_node_join(r, JH_PARTS(jh_span(pointer), jh_span("/cType")));
      jh_catalogue_fail(
          r, binding_code, source, at,
          JH_PARTS(name, jh_span(" does not hold a JSON "), json_name));
      break;
    }
    jb_json_node_t value = {0};
    if (jh_node_find(r, definition, "unit", &value) &&
        !unit_valid(r, jh_node_string(r, value, false))) {
      const char *at =
          jh_node_join(r, JH_PARTS(jh_span(pointer), jh_span("/unit")));
      jh_catalogue_fail(
          r, binding_code, source, at,
          JH_PARTS(jh_span("a printable unit without surrounding whitespace")));
    }
    static const char *const keys[] = {"minimum", "maximum", "default"};
    for (size_t k = 0; r->status == HAL_OK && k < COUNTOF(keys); ++k)
      if (jh_node_find(r, definition, keys[k], &value) &&
          !value_fits(r, value, type)) {
        const char *at = jh_node_join(
            r, JH_PARTS(jh_span(pointer), jh_span("/"), jh_span(keys[k])));
        jh_catalogue_fail(r, binding_code, source, at,
                          JH_PARTS(jh_span("a value that fits "), name));
      }
    jb_json_node_t members = {0}, member = {0};
    if (jh_node_find(r, definition, "enum", &members)) {
      size_t index = 0;
      for (bool item = jh_node_first(r, members, &member); item;
           item = jh_node_next(r, &member), ++index)
        if (!value_fits(r, member, type)) {
          const char *at =
              jh_node_join(r, JH_PARTS(jh_span(pointer), jh_span("/enum/"),
                                       jh_span(jh_node_index(r, index))));
          jh_catalogue_fail(r, binding_code, source, at,
                            JH_PARTS(jh_span("a value that fits "), name));
        }
    }
    if (jh_node_find(r, definition, "default", &value)) {
      const char *at =
          jh_node_join(r, JH_PARTS(jh_span(pointer), jh_span("/default")));
      property_value(r, definition, value, at, source);
    }
  }
}

static void signals(jh_node_reader_t *r, const jh_binding_t *entry) {
  const jb_json_node_t root = jh_binding_root(r, entry);
  jb_json_node_t table = {0}, property_table = {0}, signal = {0};
  const bool typed = jh_node_find(r, root, "properties", &property_table);
  if (!jh_node_find(r, root, "signals", &table))
    return;
  for (bool more = jh_node_first(r, table, &signal); more;
       more = jh_node_next(r, &signal)) {
    jb_json_node_t condition = {0}, ignored = {0};
    if (!jh_node_find(r, signal, "when", &condition))
      continue;
    const jb_json_node_t name = jh_node_member(r, condition, "property");
    if (!typed || !jh_node_find(r, property_table, jh_node_text(r, name, false),
                                &ignored)) {
      const char *at = jh_node_join(r, JH_PARTS(jh_span("/signals/"),
                                                jh_node_string(r, signal, true),
                                                jh_span("/when/property")));
      jh_catalogue_fail(r, binding_code, jb_string_data(&entry->path), at,
                        JH_PARTS(jh_span("a property of this type")));
    }
  }
}

static void children(jh_node_reader_t *r, const jh_catalogue_t *types,
                     const jh_binding_t *entry) {
  jb_json_node_t table = {0}, child = {0};
  if (!jh_node_find(r, jh_binding_root(r, entry), "children", &table))
    return;
  for (bool more = jh_node_first(r, table, &child); more;
       more = jh_node_next(r, &child)) {
    const jb_json_span_t name = jh_node_string(r, child, true);
    const jb_json_node_t declared = jh_node_member(r, child, "compatible");
    const jb_json_span_t compatible = jh_node_string(r, declared, false);
    const jh_binding_t *child_type = jh_catalogue_find(types, compatible);
    if (child_type == NULL) {
      const char *at = jh_node_join(
          r, JH_PARTS(jh_span("/children/"), name, jh_span("/compatible")));
      jh_catalogue_fail(r, binding_code, jb_string_data(&entry->path), at,
                        JH_PARTS(jh_span("unknown type "), compatible));
      break;
    }
    jb_json_node_t values = {0}, definitions = {0}, value = {0};
    if (!jh_node_find(r, child, "properties", &values))
      continue;
    const bool typed = jh_node_find(r, jh_binding_root(r, child_type),
                                    "properties", &definitions);
    for (bool item = jh_node_first(r, values, &value); item;
         item = jh_node_next(r, &value)) {
      const char *key = jh_node_text(r, value, true);
      const char *at =
          jh_node_join(r, JH_PARTS(jh_span("/children/"), name,
                                   jh_span("/properties/"), jh_span(key)));
      jb_json_node_t definition = {0};
      if (!typed || !jh_node_find(r, definitions, key, &definition))
        jh_catalogue_fail(r, binding_code, jb_string_data(&entry->path), at,
                          JH_PARTS(jh_span("a property of "), compatible));
      if (r->status == HAL_OK)
        property_value(r, definition, value, at, jb_string_data(&entry->path));
    }
  }
}

static void connection_target(jh_node_reader_t *r, const jh_catalogue_t *types,
                              const jh_binding_t *entry,
                              jb_json_node_t declared, bool has_children,
                              jb_map_t *owners, const char *name,
                              const char *at, jb_json_span_t target) {
  const char *dot = memchr(target.data, '.', target.length);
  const size_t prefix =
      dot == NULL ? target.length : (size_t)(dot - target.data);
  const jb_json_span_t child = {target.data, prefix};
  const jb_json_span_t signal =
      dot == NULL ? jh_span("")
                  : (jb_json_span_t){dot + 1, target.length - prefix - 1};
  const char *child_name = jh_node_join(r, JH_PARTS(child));
  const char *signal_name = jh_node_join(r, JH_PARTS(signal));
  jb_json_node_t child_entry = {0}, table = {0}, ignored = {0};
  const jh_binding_t *child_type = NULL;
  if (has_children && jh_node_find(r, declared, child_name, &child_entry)) {
    const jb_json_node_t compatible =
        jh_node_member(r, child_entry, "compatible");
    child_type = jh_catalogue_find(types, jh_node_string(r, compatible, false));
  }
  if (child_type == NULL ||
      !jh_node_find(r, jh_binding_root(r, child_type), "signals", &table) ||
      !jh_node_find(r, table, signal_name, &ignored)) {
    jh_catalogue_fail(r, binding_code, jb_string_data(&entry->path), at,
                      JH_PARTS(jh_span("a signal of a declared child")));
    return;
  }
  const void *previous = NULL;
  if (jb_map_find_const(owners, &target, &previous) == HAL_OK)
    jh_catalogue_fail(
        r, binding_code, jb_string_data(&entry->path), at,
        JH_PARTS(jh_span("a signal not already connected through "),
                 jh_span(*(const char *const *)previous)));
  else if (r->status == HAL_OK)
    jh_node_storage(r, jb_map_insert(owners, &target, &name));
}

static void connections(jh_node_reader_t *r, const jh_catalogue_t *types,
                        const jh_binding_t *entry) {
  const jb_json_node_t root = jh_binding_root(r, entry);
  jb_json_node_t table = {0}, signal_table = {0}, declared = {0},
                 connection = {0};
  if (!jh_node_find(r, root, "connections", &table))
    return;
  const bool has_signals = jh_node_find(r, root, "signals", &signal_table);
  const bool has_children = jh_node_find(r, root, "children", &declared);
  jb_map_t owners = {0};
  jh_node_storage(r, jb_map_init(&owners, sizeof(jb_json_span_t),
                                 sizeof(const char *), jh_catalogue_span_order,
                                 NULL, jb_heap_allocator()));
  for (bool more = jh_node_first(r, table, &connection); more;
       more = jh_node_next(r, &connection)) {
    const char *name = jh_node_text(r, connection, true);
    const char *prefix =
        jh_node_join(r, JH_PARTS(jh_span("/connections/"), jh_span(name)));
    jb_json_node_t ignored = {0};
    if (!has_signals || !jh_node_find(r, signal_table, name, &ignored)) {
      jh_catalogue_fail(r, binding_code, jb_string_data(&entry->path), prefix,
                        JH_PARTS(jh_span("a signal of this module")));
      break;
    }
    const char *at =
        jh_node_join(r, JH_PARTS(jh_span(prefix), jh_span("/owner")));
    const jb_json_node_t owner = jh_node_member(r, connection, "owner");
    connection_target(r, types, entry, declared, has_children, &owners, name,
                      at, jh_node_string(r, owner, false));
    const jb_json_node_t aliases = jh_node_member(r, connection, "aliases");
    jb_json_node_t alias = {0};
    size_t index = 0;
    for (bool item = jh_node_first(r, aliases, &alias); item;
         item = jh_node_next(r, &alias), ++index) {
      at = jh_node_join(r, JH_PARTS(jh_span(prefix), jh_span("/aliases/"),
                                    jh_span(jh_node_index(r, index))));
      connection_target(r, types, entry, declared, has_children, &owners, name,
                        at, jh_node_string(r, alias, false));
    }
  }
  jb_map_destroy(&owners);
}

static void listed(jh_node_reader_t *r, const jh_binding_t *entry,
                   const char *field, const jb_vector_t *allowed,
                   const char *expected) {
  jb_json_node_t table = {0}, item = {0};
  if (!jh_node_find(r, jh_binding_root(r, entry), field, &table))
    return;
  size_t index = 0;
  for (bool more = jh_node_first(r, table, &item); more;
       more = jh_node_next(r, &item), ++index)
    if (!jh_list_contains(allowed, jh_node_string(r, item, false))) {
      const char *at =
          jh_node_join(r, JH_PARTS(jh_span("/"), jh_span(field), jh_span("/"),
                                   jh_span(jh_node_index(r, index))));
      jh_catalogue_fail(r, binding_code, jb_string_data(&entry->path), at,
                        JH_PARTS(jh_span(expected)));
    }
}

/* A depth-first frame holds its next child before the stack can grow. */
typedef struct {
  size_t index;
  jb_json_node_t next;
  bool more;
} cycle_frame_t;

static void open_frame(jh_node_reader_t *r, const jh_catalogue_t *types,
                       jb_vector_t *stack, unsigned char *state, size_t index) {
  cycle_frame_t frame = {index, {0}, false};
  jb_json_node_t declared = {0};
  if (jh_node_find(r, jh_binding_root(r, jh_catalogue_at(types, index)),
                   "children", &declared))
    frame.more = jh_node_first(r, declared, &frame.next);
  state[index] = 1;
  if (r->status == HAL_OK)
    jh_node_storage(r, jb_vector_append(stack, &frame, 1));
}

static void cycles(jh_node_reader_t *r, const jh_catalogue_t *types) {
  jb_vector_t states = {0}, stack = {0};
  jh_node_storage(r, jb_vector_init(&states, 1, jb_heap_allocator()));
  jh_node_storage(
      r, jb_vector_init(&stack, sizeof(cycle_frame_t), jb_heap_allocator()));
  if (r->status == HAL_OK)
    jh_node_storage(
        r, jb_vector_resize(&states, jb_vector_size(&types->bindings)));
  unsigned char *state = jb_vector_data(&states);
  if (r->status == HAL_OK && jb_vector_size(&states) != 0)
    memset(state, 0, jb_vector_size(&states));
  jb_map_iter_t iterator;
  for (hal_status_t next = jb_map_first(&types->index, &iterator);
       r->status == HAL_OK && next == HAL_OK; next = jb_map_next(&iterator)) {
    const void *key = NULL, *value = NULL;
    jh_node_storage(r, jb_map_iter_get(&iterator, &key, &value));
    if (r->status != HAL_OK)
      break;
    const size_t index = *(const size_t *)value;
    if (state[index] == 2)
      continue;
    open_frame(r, types, &stack, state, index);
    while (r->status == HAL_OK && jb_vector_size(&stack) != 0) {
      const size_t last = jb_vector_size(&stack) - 1;
      cycle_frame_t *top = jb_vector_at(&stack, last);
      if (!top->more) {
        state[top->index] = 2;
        jh_node_storage(r, jb_vector_resize(&stack, last));
        continue;
      }
      const jb_json_node_t declared =
          jh_node_member(r, top->next, "compatible");
      const jb_json_span_t kid = jh_node_string(r, declared, false);
      top->more = jh_node_next(r, &top->next);
      const void *found = NULL;
      if (jb_map_find_const(&types->index, &kid, &found) != HAL_OK)
        continue;
      const size_t child = *(const size_t *)found;
      if (state[child] == 2)
        continue;
      if (state[child] == 1) {
        jb_string_t chain = {0};
        (void)jb_string_init(&chain, jb_heap_allocator());
        for (size_t i = 0; r->status == HAL_OK && i < jb_vector_size(&stack);
             ++i) {
          const cycle_frame_t *frame = jb_vector_const_at(&stack, i);
          jh_node_storage(
              r, jh_append_span(
                     &chain,
                     jh_string_span(
                         &jh_catalogue_at(types, frame->index)->compatible)));
          jh_node_storage(r, jh_append(&chain, " -> "));
        }
        const cycle_frame_t *first = jb_vector_const_at(&stack, 0);
        jh_catalogue_fail(
            r, binding_code,
            jb_string_data(&jh_catalogue_at(types, first->index)->path),
            "/children",
            JH_PARTS(jh_span("cyclic expansion: "), jh_string_span(&chain),
                     kid));
        jb_string_destroy(&chain);
      } else {
        open_frame(r, types, &stack, state, child);
      }
    }
  }
  jb_vector_destroy(&stack);
  jb_vector_destroy(&states);
}

hal_status_t jh_catalogue_check(const jh_catalogue_t *types,
                                const jb_vector_t *features,
                                const jb_vector_t *capabilities,
                                jh_failure_t *failure) {
  if (types == NULL || failure == NULL)
    return jh_fail_arguments(failure);
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&types->bindings);
       ++i) {
    jh_node_reader_t reader;
    jh_node_reader_init(&reader, failure);
    const jh_binding_t *entry = jh_catalogue_at(types, i);
    properties(&reader, entry);
    signals(&reader, entry);
    children(&reader, types, entry);
    connections(&reader, types, entry);
    if (features != NULL)
      listed(&reader, entry, "requiresFeatures", features,
             "a flag of the HAL feature registry");
    if (capabilities != NULL)
      listed(&reader, entry, "provides", capabilities,
             "a capability of boards/capabilities.json");
    status = reader.status;
    jh_node_reader_destroy(&reader);
  }
  if (status == HAL_OK) {
    jh_node_reader_t reader;
    jh_node_reader_init(&reader, failure);
    cycles(&reader, types);
    status = reader.status;
    jh_node_reader_destroy(&reader);
  }
  return status;
}
