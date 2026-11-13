#include "jh_device_defaults.h"
#include "jh_catalogue_internal.h"
#include <jb/json_number.h>
#include <string.h>

hal_status_t jh_macro_token(jb_json_span_t name, jb_string_t *out) {
  if (out == NULL || (name.data == NULL && name.length != 0))
    return HAL_EINVAL;
  jb_string_t result = {0};
  bool separator = false;
  hal_status_t status = jb_string_init(&result, jb_heap_allocator());
  for (size_t i = 0; i < name.length && status == HAL_OK; ++i) {
    const char c = name.data[i];
    if (!jh_ascii_is_upper(c) && !jh_ascii_is_lower(c) &&
        !jh_ascii_is_digit(c)) {
      separator = true;
      continue;
    }
    if (i > 0 && jh_ascii_is_upper(c) &&
        (jh_ascii_is_lower(name.data[i - 1]) ||
         jh_ascii_is_digit(name.data[i - 1]) ||
         (jh_ascii_is_upper(name.data[i - 1]) && i + 1 < name.length &&
          jh_ascii_is_lower(name.data[i + 1]))))
      separator = true;
    if (separator && jb_string_size(&result) != 0)
      jh_put(&result, &status, "_");
    separator = false;
    const char converted = jh_ascii_to_upper(c);
    jh_put_span(&result, &status, (jb_json_span_t){&converted, 1});
  }
  if (status == HAL_OK)
    status =
        jb_string_assign(out, jb_string_data(&result), jb_string_size(&result));
  jb_string_destroy(&result);
  return status;
}

typedef struct {
  jh_node_reader_t reader;
  const jh_catalogue_t *types;
  jb_string_t text;
  jb_map_t origins;
} renderer_t;

typedef struct {
  const char *prefix;
  const jh_binding_t *type;
  jb_map_t entries;
  const char *origin;
} group_t;

static void defaults_error(jh_node_reader_t *r, const jb_json_span_t *parts,
                           size_t count) {
  if (r->status == HAL_OK)
    r->status = jh_fail(r->failure, HAL_ECONFIG, 2, parts, count);
}

static const char *token(jh_node_reader_t *r, jb_json_span_t name) {
  jb_string_t text = {0};
  jh_node_storage(r, jb_string_init(&text, jb_heap_allocator()));
  if (r->status == HAL_OK)
    jh_node_storage(r, jh_macro_token(name, &text));
  const char *result = jh_node_join(r, JH_PARTS(jh_string_span(&text)));
  jb_string_destroy(&text);
  return result;
}

static void literal_failure(jh_node_reader_t *r, hal_status_t status,
                            jb_json_node_t value, const jh_c_type_t *type,
                            const char *origin) {
  const jb_json_span_t raw = jh_node_raw(r, value);
  const char *reason;
  if (status == HAL_ETYPE) {
    reason = jh_node_type(r, value) == JB_JSON_NUMBER &&
                     type->category == JH_C_INTEGER
                 ? " is not an integer"
                 : jh_node_join(
                       r, JH_PARTS(jh_span(" is not a "),
                                   jh_span(jh_c_json_type(type->category))));
  } else if (status == HAL_EOVERFLOW && type->category == JH_C_FLOATING) {
    size_t bytes = 0;
    hal_status_t rounded =
        jb_json_float_workspace_size(value, type->bits, &bytes);
    jb_vector_t scratch = {0};
    jh_node_storage(r, jb_vector_init(&scratch, 1, jb_heap_allocator()));
    if (rounded == HAL_OK && r->status == HAL_OK) {
      jh_node_storage(r, jb_vector_resize(&scratch, bytes));
      const jb_json_float_options_t options = {false, true,
                                               64 * raw.length + 1000000};
      const jb_json_workspace_t memory = {jb_vector_data(&scratch), bytes};
      uint32_t bits32;
      uint64_t bits64;
      if (r->status == HAL_OK)
        rounded = type->bits == 32
                      ? jb_json_get_f32_bits(value, &options, memory, &bits32)
                      : jb_json_get_f64_bits(value, &options, memory, &bits64);
    }
    jb_vector_destroy(&scratch);
    reason = jh_node_join(
        r, JH_PARTS(jh_span(rounded == HAL_OK ? " rounds to zero in "
                                              : " overflows "),
                    jh_span(type->name)));
  } else if (status == HAL_EOVERFLOW) {
    reason = jh_node_join(
        r, JH_PARTS(jh_span(" does not fit "), jh_span(type->name)));
  } else {
    reason = jh_node_join(
        r, JH_PARTS(jh_span(": "), jh_span(hal_status_to_string(status))));
  }
  defaults_error(
      r, JH_PARTS(jh_span(origin), jh_span(": "), raw, jh_span(reason)));
}

static const char *c_literal(jh_node_reader_t *r, jb_json_node_t value,
                             const jh_c_type_t *type, const char *origin) {
  size_t bytes = 0, required = 0;
  hal_status_t status = HAL_OK;
  jb_vector_t scratch = {0};
  jb_string_t text = {0};
  jh_node_storage(r, jb_vector_init(&scratch, 1, jb_heap_allocator()));
  jh_node_storage(r, jb_string_init(&text, jb_heap_allocator()));
  const jb_json_span_t raw = jh_node_raw(r, value);
  if (r->status == HAL_OK)
    status = jb_c_literal_workspace_size(value, type->literal, &bytes);
  if (status == HAL_OK && r->status == HAL_OK) {
    jh_node_storage(r, jb_vector_resize(&scratch, bytes));
    jh_node_storage(r, jb_string_resize(&text, 256, '\0'));
    const jb_json_workspace_t memory = {jb_vector_data(&scratch), bytes};
    if (r->status == HAL_OK)
      status = jb_c_literal(
          value, type->literal, memory, 64 * raw.length + 1000000,
          jb_string_mutable_data(&text), jb_string_size(&text), &required);
    if (status == HAL_EOVERFLOW && required > jb_string_size(&text)) {
      jh_node_storage(r, jb_string_resize(&text, required, '\0'));
      if (r->status == HAL_OK)
        status = jb_c_literal(
            value, type->literal, memory, 64 * raw.length + 1000000,
            jb_string_mutable_data(&text), jb_string_size(&text), &required);
    }
    if (status == HAL_OK && r->status == HAL_OK)
      jh_node_storage(r, jb_string_resize(&text, required, '\0'));
  }
  if (status != HAL_OK)
    literal_failure(r, status, value, type, origin);
  const char *result = jh_node_join(r, JH_PARTS(jh_string_span(&text)));
  jb_string_destroy(&text);
  jb_vector_destroy(&scratch);
  return result;
}

static bool string_enum(jh_node_reader_t *r, jb_json_node_t definition) {
  const jb_json_node_t ctype = jh_node_member(r, definition, "cType");
  jb_json_node_t ignored;
  return jh_span_equal(jh_node_string(r, ctype, false), jh_span("string")) &&
         jh_node_find(r, definition, "enum", &ignored);
}

static const char *enum_macro(jh_node_reader_t *r, jb_json_span_t compatible,
                              jb_json_span_t prop, jb_json_span_t value) {
  const char *a = token(r, compatible), *b = token(r, prop),
             *c = token(r, value);
  return jh_node_join(r, JH_PARTS(jh_span("JH_ENUM_"), jh_span(a), jh_span("_"),
                                  jh_span(b), jh_span("_"), jh_span(c)));
}

static void values_init(jh_node_reader_t *r, jb_map_t *values) {
  jh_node_storage(
      r, jb_map_init(values, sizeof(jb_json_span_t), sizeof(jb_json_node_t),
                     jh_catalogue_span_order, NULL, jb_heap_allocator()));
}

static void defaults_of(jh_node_reader_t *r, const jh_binding_t *type,
                        jb_map_t *values) {
  jb_json_node_t properties, definition;
  if (jh_node_find(r, jh_binding_root(r, type), "properties", &properties) &&
      jh_node_first(r, properties, &definition))
    do {
      jb_json_node_t value;
      if (jh_node_find(r, definition, "default", &value)) {
        const jb_json_span_t prop = jh_node_string(r, definition, true);
        if (r->status == HAL_OK)
          jh_node_storage(r, jb_map_insert(values, &prop, &value));
      }
    } while (jh_node_next(r, &definition));
}

static void define(renderer_t *renderer, const char *name, const char *value,
                   const char *origin) {
  jh_node_reader_t *r = &renderer->reader;
  const jb_json_span_t key = jh_span(name);
  const void *existing = NULL;
  if (r->status != HAL_OK)
    return;
  const hal_status_t found =
      jb_map_find_const(&renderer->origins, &key, &existing);
  if (found == HAL_OK) {
    defaults_error(r, JH_PARTS(key, jh_span(" is produced by "),
                               jh_span(*(const char *const *)existing),
                               jh_span(" and "), jh_span(origin)));
    return;
  }
  if (found != HAL_ENOENT)
    jh_node_storage(r, found);
  if (r->status == HAL_OK)
    jh_node_storage(r, jb_map_insert(&renderer->origins, &key, &origin));
  jh_put(&renderer->text, &r->status, "#define ");
  jh_put(&renderer->text, &r->status, name);
  jh_put(&renderer->text, &r->status, " ");
  jh_put(&renderer->text, &r->status, value);
  jh_put(&renderer->text, &r->status, "\n");
}

static void emit_defaults(renderer_t *renderer, const group_t *group) {
  jh_node_reader_t *r = &renderer->reader;
  jb_json_node_t properties;
  const bool typed = jh_node_find(r, jh_binding_root(r, group->type),
                                  "properties", &properties);
  jb_map_iter_t it;
  hal_status_t status = jb_map_first(&group->entries, &it);
  for (; status == HAL_OK && r->status == HAL_OK; status = jb_map_next(&it)) {
    const void *key, *mapped;
    jh_node_storage(r, jb_map_iter_get(&it, &key, &mapped));
    if (r->status != HAL_OK)
      break;
    const jb_json_span_t prop = *(const jb_json_span_t *)key;
    const jb_json_node_t value = *(const jb_json_node_t *)mapped;
    const char *origin =
        jh_node_join(r, JH_PARTS(jh_span(group->origin), jh_span("."), prop));
    jb_json_node_t definition;
    if (!typed || !jh_node_find(r, properties, prop.data, &definition)) {
      defaults_error(r,
                     JH_PARTS(jh_span(origin), jh_span(": not a property of "),
                              jh_string_span(&group->type->compatible)));
      break;
    }
    const char *literal = "";
    if (string_enum(r, definition)) {
      literal = enum_macro(r, jh_string_span(&group->type->compatible), prop,
                           jh_node_string(r, value, false));
    } else {
      const jb_json_node_t ctype = jh_node_member(r, definition, "cType");
      const jh_c_type_t *type = jh_c_type(jh_node_string(r, ctype, false));
      if (type == NULL)
        defaults_error(r,
                       JH_PARTS(jh_span(origin), jh_span(": unknown C type")));
      else
        literal = c_literal(r, value, type, origin);
    }
    const char *name =
        jh_node_join(r, JH_PARTS(jh_span(group->prefix), jh_span("_"),
                                 jh_span(token(r, prop))));
    define(renderer, name, literal, origin);
  }
  if (status != HAL_OK && status != HAL_ENOENT)
    jh_node_storage(r, status);
}

static void sorted_members(jh_node_reader_t *r, jb_json_node_t owner,
                           const char *field, bool enums_only, jb_map_t *out) {
  jb_json_node_t members, member;
  if (jh_node_find(r, owner, field, &members) &&
      jh_node_first(r, members, &member))
    do {
      if (enums_only && !string_enum(r, member))
        continue;
      const jb_json_span_t key = jh_node_string(r, member, true);
      if (r->status == HAL_OK)
        jh_node_storage(r, jb_map_insert(out, &key, &member));
    } while (jh_node_next(r, &member));
}

static void emit_enums(renderer_t *renderer, const jh_binding_t *type) {
  jh_node_reader_t *r = &renderer->reader;
  jb_map_t enums = {0};
  values_init(r, &enums);
  sorted_members(r, jh_binding_root(r, type), "properties", true, &enums);
  if (jb_map_size(&enums) != 0) {
    jh_put(&renderer->text, &r->status, "\n/* ");
    jh_put(&renderer->text, &r->status, jb_string_data(&type->compatible));
    jh_put(&renderer->text, &r->status, ": enum positions */\n");
  }
  jb_map_iter_t it;
  hal_status_t status = jb_map_first(&enums, &it);
  for (; status == HAL_OK && r->status == HAL_OK; status = jb_map_next(&it)) {
    const void *key, *mapped;
    jh_node_storage(r, jb_map_iter_get(&it, &key, &mapped));
    if (r->status != HAL_OK)
      break;
    const jb_json_span_t prop = *(const jb_json_span_t *)key;
    const jb_json_node_t values =
        jh_node_member(r, *(const jb_json_node_t *)mapped, "enum");
    const char *origin = jh_node_join(
        r, JH_PARTS(jh_string_span(&type->compatible), jh_span("."), prop));
    size_t index = 0;
    jb_json_node_t member;
    if (jh_node_first(r, values, &member))
      do {
        const char *name = enum_macro(r, jh_string_span(&type->compatible),
                                      prop, jh_node_string(r, member, false));
        const char *literal = jh_node_join(
            r, JH_PARTS(jh_span(jh_node_index(r, index)), jh_span("U")));
        define(renderer, name, literal, origin);
        ++index;
      } while (jh_node_next(r, &member));
  }
  if (status != HAL_OK && status != HAL_ENOENT)
    jh_node_storage(r, status);
  jb_map_destroy(&enums);
}

static void emit_type(renderer_t *renderer, const jh_binding_t *type) {
  jh_node_reader_t *r = &renderer->reader;
  jb_vector_t groups = {0};
  jb_map_t children = {0};
  jh_node_storage(
      r, jb_vector_init(&groups, sizeof(group_t), jb_heap_allocator()));
  values_init(r, &children);
  const char *prefix = jh_node_join(
      r, JH_PARTS(jh_span("JH_DEFAULT_"),
                  jh_span(token(r, jh_string_span(&type->compatible)))));
  group_t own = {prefix, type, {0}, jb_string_data(&type->compatible)};
  values_init(r, &own.entries);
  defaults_of(r, type, &own.entries);
  bool any = jb_map_size(&own.entries) != 0;
  if (r->status == HAL_OK)
    jh_node_storage(r, jb_vector_append(&groups, &own, 1));
  if (r->status != HAL_OK)
    jb_map_destroy(&own.entries);
  sorted_members(r, jh_binding_root(r, type), "children", false, &children);
  jb_map_iter_t it;
  hal_status_t status = jb_map_first(&children, &it);
  for (; status == HAL_OK && r->status == HAL_OK; status = jb_map_next(&it)) {
    const void *key, *mapped;
    jh_node_storage(r, jb_map_iter_get(&it, &key, &mapped));
    if (r->status != HAL_OK)
      break;
    const jb_json_span_t name = *(const jb_json_span_t *)key;
    const jb_json_node_t child = *(const jb_json_node_t *)mapped;
    const jb_json_node_t compatible_node =
        jh_node_member(r, child, "compatible");
    const jb_json_span_t compatible = jh_node_string(r, compatible_node, false);
    const jh_binding_t *child_type =
        jh_catalogue_find(renderer->types, compatible);
    const char *origin =
        jh_node_join(r, JH_PARTS(jh_string_span(&type->compatible),
                                 jh_span(" child "), name));
    if (child_type == NULL) {
      defaults_error(
          r, JH_PARTS(jh_span(origin), jh_span(": unknown type "), compatible));
      break;
    }
    group_t group = {jh_node_join(r, JH_PARTS(jh_span(prefix), jh_span("_"),
                                              jh_span(token(r, name)))),
                     child_type,
                     {0},
                     origin};
    values_init(r, &group.entries);
    defaults_of(r, child_type, &group.entries);
    jb_json_node_t overrides, value;
    if (jh_node_find(r, child, "properties", &overrides) &&
        jh_node_first(r, overrides, &value))
      do {
        const jb_json_span_t prop = jh_node_string(r, value, true);
        if (r->status == HAL_OK)
          jh_node_storage(r, jb_map_upsert(&group.entries, &prop, &value));
      } while (jh_node_next(r, &value));
    any = any || jb_map_size(&group.entries) != 0;
    if (r->status == HAL_OK)
      jh_node_storage(r, jb_vector_append(&groups, &group, 1));
    if (r->status != HAL_OK)
      jb_map_destroy(&group.entries);
  }
  if (status != HAL_OK && status != HAL_ENOENT)
    jh_node_storage(r, status);
  if (any && r->status == HAL_OK) {
    jh_put(&renderer->text, &r->status, "\n/* ");
    jh_put(&renderer->text, &r->status, jb_string_data(&type->compatible));
    jh_put(&renderer->text, &r->status, " */\n");
    for (size_t i = 0; i < jb_vector_size(&groups) && r->status == HAL_OK; ++i)
      emit_defaults(renderer, jb_vector_const_at(&groups, i));
  }
  for (size_t i = 0; i < jb_vector_size(&groups); ++i) {
    group_t *group = jb_vector_at(&groups, i);
    jb_map_destroy(&group->entries);
  }
  jb_map_destroy(&children);
  jb_vector_destroy(&groups);
}

hal_status_t jh_render_device_defaults(const jh_catalogue_t *types,
                                       jb_string_t *out,
                                       jh_failure_t *failure) {
  if (types == NULL || out == NULL || failure == NULL)
    return jh_fail_arguments(failure);
  renderer_t renderer = {0};
  renderer.types = types;
  jh_node_reader_t *r = &renderer.reader;
  jh_node_reader_init(r, failure);
  jh_node_storage(r, jb_string_init(&renderer.text, jb_heap_allocator()));
  jh_node_storage(r, jb_map_init(&renderer.origins, sizeof(jb_json_span_t),
                                 sizeof(const char *), jh_catalogue_span_order,
                                 NULL, jb_heap_allocator()));
  jh_put(&renderer.text, &r->status,
         "#pragma once\n"
         "/* Generated from config/hardware/bindings by the device defaults "
         "generator;\n"
         " * do not edit. */\n");
  for (unsigned pass = 0; pass < 2 && r->status == HAL_OK; ++pass) {
    jb_map_iter_t it;
    hal_status_t status = jb_map_first(&types->index, &it);
    for (; status == HAL_OK && r->status == HAL_OK; status = jb_map_next(&it)) {
      const void *key, *mapped;
      jh_node_storage(r, jb_map_iter_get(&it, &key, &mapped));
      if (r->status != HAL_OK)
        break;
      const jh_binding_t *type =
          jh_catalogue_at(types, *(const size_t *)mapped);
      if (pass == 0)
        emit_enums(&renderer, type);
      else
        emit_type(&renderer, type);
    }
    if (status != HAL_OK && status != HAL_ENOENT)
      jh_node_storage(r, status);
  }
  const hal_status_t status = r->status;
  if (status == HAL_OK) {
    jb_string_destroy(out);
    *out = renderer.text;
    memset(&renderer.text, 0, sizeof renderer.text);
  } else if (failure->status == HAL_OK) {
    (void)jh_fail_storage(failure, status);
  }
  jb_map_destroy(&renderer.origins);
  jb_string_destroy(&renderer.text);
  jh_node_reader_destroy(r);
  return status;
}
