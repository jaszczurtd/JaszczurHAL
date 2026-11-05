#include "jh_feature_registry.h"
#include "hal/security/jh_sha256.h"
#include "jh_generated_output.h"
#include "jh_json_file.h"
#include "jh_python_repr.h"
#include <jb/array.h>
#include <jb/filesystem.h>
#include <jb/json_tree.h>
#include <jb/json_writer.h>
#include <jb/unicode.h>
#include <jb/utf8.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Version of the registry fragments and generator in the artifacts. */
#define JH_FEATURE_SCHEMA_VERSION 1
#define JH_FEATURE_GENERATOR_VERSION 2

static const jb_allocator_t *heap(void) { return jb_heap_allocator(); }

static jb_json_span_t text_of(const jb_string_t *text) {
  const jb_json_span_t span = {jb_string_data(text), jb_string_size(text)};
  return span;
}

static hal_status_t append(jb_string_t *out, const char *text) {
  return jb_string_append(out, text, strlen(text));
}

static hal_status_t append_span(jb_string_t *out, jb_json_span_t text) {
  return text.length == 0 ? HAL_OK
                          : jb_string_append(out, text.data, text.length);
}

/* Append text unless an earlier append failed. */
static void put(jb_string_t *out, hal_status_t *status, const char *text) {
  if (*status == HAL_OK)
    *status = append(out, text);
}

static void put_span(jb_string_t *out, hal_status_t *status,
                     jb_json_span_t text) {
  if (*status == HAL_OK)
    *status = append_span(out, text);
}

static void put_number(jb_string_t *out, hal_status_t *status, int value) {
  char number[16];
  const int written = snprintf(number, sizeof number, "%d", value);
  if (*status == HAL_OK)
    *status = jb_string_append(out, number, (size_t)written);
}

/* ---- Lists of owned strings in code point order ---- */

static hal_status_t list_init(jb_vector_t *list) {
  return jb_vector_init(list, sizeof(jb_string_t), heap());
}

static const jb_string_t *list_at(const jb_vector_t *list, size_t index) {
  return jb_vector_const_at(list, index);
}

static void list_destroy(jb_vector_t *list) {
  for (size_t index = 0; index < jb_vector_size(list); ++index)
    jb_string_destroy(jb_vector_at(list, index));
  jb_vector_destroy(list);
}

static hal_status_t list_push(jb_vector_t *list, jb_json_span_t text) {
  jb_string_t item = {0};
  hal_status_t status = jb_string_init(&item, heap());
  if (status == HAL_OK)
    status = append_span(&item, text);
  if (status == HAL_OK)
    status = jb_vector_append(list, &item, 1);
  if (status != HAL_OK)
    jb_string_destroy(&item);
  return status;
}

static int compare_text(jb_json_span_t a, jb_json_span_t b) {
  const size_t shorter = a.length < b.length ? a.length : b.length;
  const int order = shorter == 0 ? 0 : memcmp(a.data, b.data, shorter);
  if (order != 0)
    return order;
  return a.length < b.length ? -1 : a.length > b.length ? 1 : 0;
}

static int compare_strings(const void *left, const void *right) {
  return compare_text(text_of(left), text_of(right));
}

static void list_sort(jb_vector_t *list) {
  if (jb_vector_size(list) > 1)
    qsort(jb_vector_data(list), jb_vector_size(list), sizeof(jb_string_t),
          compare_strings);
}

static bool list_contains(const jb_vector_t *list, jb_json_span_t text) {
  for (size_t index = 0; index < jb_vector_size(list); ++index)
    if (compare_text(text_of(list_at(list, index)), text) == 0)
      return true;
  return false;
}

/* Python's repr of a list of strings: ['a', 'b']. */
static hal_status_t repr_list(jb_string_t *out, const jb_vector_t *list) {
  hal_status_t status = append(out, "[");
  for (size_t index = 0; status == HAL_OK && index < jb_vector_size(list);
       ++index) {
    if (index != 0)
      status = append(out, ", ");
    if (status == HAL_OK)
      status = jh_python_repr_append(out, text_of(list_at(list, index)));
  }
  return status == HAL_OK ? append(out, "]") : status;
}

/* ---- Failures with the wording of generate_hal_features.py ---- */

/* The failure of a container that could not grow, or a recorded one. */
static hal_status_t storage(jh_failure_t *failure, hal_status_t status) {
  return status == HAL_OK ? HAL_OK : jh_fail_storage(failure, status);
}

static hal_status_t fail_message(jh_failure_t *failure,
                                 const jb_string_t *message) {
  const jb_json_span_t part = text_of(message);
  return jh_fail(failure, HAL_ECONFIG, 2, &part, 1);
}

/* "{path}: {json_path}: got {got}; expected {expected}". */
static hal_status_t fail_got(jh_failure_t *failure, jb_json_span_t path,
                             jb_json_span_t json_path, const jb_string_t *got,
                             const char *expected) {
  jb_string_t message = {0};
  hal_status_t status = jb_string_init(&message, heap());
  if (status == HAL_OK)
    status = append_span(&message, path);
  if (status == HAL_OK)
    status = append(&message, ": ");
  if (status == HAL_OK)
    status = append_span(&message, json_path);
  if (status == HAL_OK)
    status = append(&message, ": got ");
  if (status == HAL_OK)
    status = append_span(&message, text_of(got));
  if (status == HAL_OK)
    status = append(&message, "; expected ");
  if (status == HAL_OK)
    status = append(&message, expected);
  status = status == HAL_OK ? fail_message(failure, &message)
                            : jh_fail_storage(failure, status);
  jb_string_destroy(&message);
  return status;
}

/* fail_got with the repr of a JSON value, or None for an absent one. */
static hal_status_t fail_value(jh_failure_t *failure, jb_json_span_t path,
                               jb_json_span_t json_path,
                               const jb_json_tree_view_t *value,
                               const char *expected) {
  jb_string_t got = {0};
  hal_status_t status = jb_string_init(&got, heap());
  if (status == HAL_OK)
    status = value == NULL ? append(&got, "None")
                           : jh_python_repr_json_append(&got, *value);
  status = status == HAL_OK ? fail_got(failure, path, json_path, &got, expected)
                            : jh_fail_storage(failure, status);
  jb_string_destroy(&got);
  return status;
}

/* fail_got with the repr of a list of strings. */
static hal_status_t fail_list(jh_failure_t *failure, jb_json_span_t path,
                              jb_json_span_t json_path, const jb_vector_t *list,
                              const char *expected) {
  jb_string_t got = {0};
  hal_status_t status = jb_string_init(&got, heap());
  if (status == HAL_OK)
    status = repr_list(&got, list);
  status = status == HAL_OK ? fail_got(failure, path, json_path, &got, expected)
                            : jh_fail_storage(failure, status);
  jb_string_destroy(&got);
  return status;
}

/* A failure message joined from NUL-terminated parts and spans. */
static hal_status_t fail_parts(jh_failure_t *failure,
                               const jb_json_span_t *parts, size_t count) {
  return jh_fail(failure, HAL_ECONFIG, 2, parts, count);
}

/* ---- JSON values ---- */

static jb_json_type_t type_of(jb_json_tree_view_t value) {
  jb_json_type_t type = JB_JSON_NULL;
  (void)jb_json_tree_type(value, &type);
  return type;
}

static bool is_string(jb_json_tree_view_t value, jb_json_span_t *text) {
  return type_of(value) == JB_JSON_STRING &&
         jb_json_tree_text(value, text) == HAL_OK;
}

/* The member key of object; absent members return false. */
static bool member(jb_json_tree_view_t object, const char *key,
                   jb_json_tree_view_t *out) {
  return type_of(object) == JB_JSON_OBJECT &&
         jb_json_tree_find(object, jh_span(key), out) == HAL_OK;
}

static size_t count_of(jb_json_tree_view_t value) {
  size_t count = 0;
  (void)jb_json_tree_count(value, &count);
  return count;
}

/* A JSON path: the base followed by a suffix. */
static hal_status_t json_path(jb_string_t *out, jb_json_span_t base,
                              const char *suffix) {
  jb_string_clear(out);
  hal_status_t status = append_span(out, base);
  return status == HAL_OK ? append(out, suffix) : status;
}

/* "{base}[{index}]". */
static hal_status_t indexed_path(jb_string_t *out, jb_json_span_t base,
                                 size_t index) {
  char number[32];
  const int written = snprintf(number, sizeof number, "[%zu]", index);
  jb_string_clear(out);
  hal_status_t status = append_span(out, base);
  return status == HAL_OK ? jb_string_append(out, number, (size_t)written)
                          : status;
}

/* ---- Patterns of generate_hal_features.py ---- */

static bool ascii_in(char c, const char *extra) {
  return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         (extra != NULL && strchr(extra, c) != NULL && c != '\0');
}

/* ^HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+$ */
static bool symbol_name(jb_json_span_t text) {
  static const char *const prefixes[] = {"HAL_ENABLE_", "HAL_DISABLE_"};
  for (size_t p = 0; p < COUNTOF(prefixes); ++p) {
    const size_t length = strlen(prefixes[p]);
    if (text.length <= length || memcmp(text.data, prefixes[p], length) != 0)
      continue;
    for (size_t i = length; i < text.length; ++i)
      if (!ascii_in(text.data[i], "_"))
        return false;
    return true;
  }
  return false;
}

/* ^[a-z][a-z0-9-]*$ */
static bool domain_name(jb_json_span_t text) {
  if (text.length == 0 || text.data[0] < 'a' || text.data[0] > 'z')
    return false;
  for (size_t i = 1; i < text.length; ++i) {
    const char c = text.data[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
      return false;
  }
  return true;
}

/* ^src/[A-Za-z0-9_./+-]+\.(?:c|cc|cpp|S)$ */
static bool source_path(jb_json_span_t text) {
  static const char *const suffixes[] = {".c", ".cc", ".cpp", ".S"};
  if (text.length < 4 || memcmp(text.data, "src/", 4) != 0)
    return false;
  for (size_t i = 4; i < text.length; ++i) {
    const char c = text.data[i];
    if (!((c >= 'a' && c <= 'z') || ascii_in(c, "_./+-")))
      return false;
  }
  const size_t rest = text.length - 4;
  for (size_t s = 0; s < COUNTOF(suffixes); ++s) {
    const size_t length = strlen(suffixes[s]);
    if (rest > length &&
        memcmp(text.data + text.length - length, suffixes[s], length) == 0)
      return true;
  }
  return false;
}

/* Python's str.strip(): whitespace that str.isspace() accepts on both ends;
 * false for text that is not valid UTF-8. */
static bool strip(jb_json_span_t text, jb_json_span_t *out) {
  size_t begin = 0, end = 0;
  bool content = false;
  for (size_t at = 0; at < text.length;) {
    uint32_t scalar = 0;
    size_t used = 0;
    jb_unicode_properties_t properties = {0};
    if (jb_utf8_decode(text.data + at, text.length - at, &scalar, &used) !=
            HAL_OK ||
        jb_unicode_properties(scalar, &properties) != HAL_OK)
      return false;
    if (!properties.whitespace) {
      if (!content)
        begin = at;
      content = true;
      end = at + used;
    }
    at += used;
  }
  out->data = text.data + begin;
  out->length = content ? end - begin : 0;
  return true;
}

/* ---- Fragments ---- */

typedef struct {
  jb_json_span_t path;        /* the fragment file */
  jb_string_t json_path;      /* scratch for the paths of the messages */
  jb_string_t scratch;        /* second scratch */
  const jb_vector_t *managed; /* managed dependency names */
  jh_failure_t *failure;
} fragment_t;

/* require_exact_fields: an object with every required and only allowed
 * fields. */
static hal_status_t
exact_fields(fragment_t *f, jb_json_span_t path_text, jb_json_tree_view_t value,
             const char *const *required, size_t required_count,
             const char *const *allowed, size_t allowed_count) {
  if (type_of(value) != JB_JSON_OBJECT)
    return fail_value(f->failure, f->path, path_text, &value, "an object");
  jb_vector_t missing = {0}, unknown = {0};
  hal_status_t status = list_init(&missing);
  if (status == HAL_OK)
    status = list_init(&unknown);
  for (size_t i = 0; status == HAL_OK && i < required_count; ++i) {
    jb_json_tree_view_t found = {0};
    if (!member(value, required[i], &found))
      status = list_push(&missing, jh_span(required[i]));
  }
  for (size_t i = 0; status == HAL_OK && i < count_of(value); ++i) {
    jb_json_tree_view_t item = {0};
    jb_json_span_t key = {NULL, 0};
    status = jb_json_tree_at(value, i, &item);
    if (status == HAL_OK)
      status = jb_json_tree_key(item, &key);
    bool known = false;
    for (size_t a = 0; status == HAL_OK && a < allowed_count; ++a)
      known = known || compare_text(key, jh_span(allowed[a])) == 0;
    if (status == HAL_OK && !known)
      status = list_push(&unknown, key);
  }
  if (status != HAL_OK) {
    status = storage(f->failure, status);
  } else if (jb_vector_size(&missing) != 0 || jb_vector_size(&unknown) != 0) {
    const bool absent = jb_vector_size(&missing) != 0;
    const char *const *names = absent ? required : allowed;
    const size_t names_count = absent ? required_count : allowed_count;
    jb_string_t expected = {0};
    jb_vector_t sorted = {0};
    status = jb_string_init(&expected, heap());
    if (status == HAL_OK)
      status = list_init(&sorted);
    for (size_t i = 0; status == HAL_OK && i < names_count; ++i)
      status = list_push(&sorted, jh_span(names[i]));
    list_sort(&sorted);
    list_sort(absent ? &missing : &unknown);
    if (status == HAL_OK)
      status = append(&expected, absent ? "required fields " : "only fields ");
    if (status == HAL_OK)
      status = repr_list(&expected, &sorted);
    status = status == HAL_OK ? fail_list(f->failure, f->path, path_text,
                                          absent ? &missing : &unknown,
                                          jb_string_data(&expected))
                              : storage(f->failure, status);
    list_destroy(&sorted);
    jb_string_destroy(&expected);
  }
  list_destroy(&unknown);
  list_destroy(&missing);
  return status;
}

/* One kind of list of a symbol and the message of each of its checks. */
typedef struct {
  const char *array, *item, *unique;
} list_rule_t;

static const list_rule_t relation_rule = {
    "an array of feature symbols", "a HAL_ENABLE_* or HAL_DISABLE_* symbol",
    "unique feature symbols"};
static const list_rule_t source_rule = {"an array of repository source paths",
                                        "a source path below src/",
                                        "unique repository source paths"};
static const list_rule_t dependency_rule = {
    "an array of managed dependency names", NULL,
    "unique managed dependency names"};

/* normalize_relation, normalize_build_sources and
 * normalize_build_dependencies: an array of unique valid items, sorted into
 * out. value NULL reads as the empty default. */
static hal_status_t normalized_list(fragment_t *f, jb_json_span_t path_text,
                                    const jb_json_tree_view_t *value,
                                    const list_rule_t *rule, jb_vector_t *out) {
  if (value == NULL)
    return HAL_OK;
  if (type_of(*value) != JB_JSON_ARRAY)
    return fail_value(f->failure, f->path, path_text, value, rule->array);
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < count_of(*value); ++i) {
    jb_json_tree_view_t item = {0};
    jb_json_span_t text = {NULL, 0};
    status = storage(f->failure, jb_json_tree_at(*value, i, &item));
    if (status != HAL_OK)
      break;
    const bool text_item = is_string(item, &text);
    bool valid = false;
    if (rule == &relation_rule)
      valid = text_item && symbol_name(text);
    else if (rule == &source_rule)
      valid = text_item && source_path(text);
    else
      valid = text_item && list_contains(f->managed, text);
    if (!valid) {
      status = storage(f->failure, indexed_path(&f->scratch, path_text, i));
      if (status != HAL_OK)
        break;
      if (rule->item != NULL)
        return fail_value(f->failure, f->path, text_of(&f->scratch), &item,
                          rule->item);
      jb_string_t expected = {0};
      status = jb_string_init(&expected, heap());
      if (status == HAL_OK)
        status = append(&expected, "one of ");
      for (size_t d = 0; status == HAL_OK && d < jb_vector_size(f->managed);
           ++d) {
        if (d != 0)
          status = append(&expected, ", ");
        if (status == HAL_OK)
          status = append_span(&expected, text_of(list_at(f->managed, d)));
      }
      status = status == HAL_OK
                   ? fail_value(f->failure, f->path, text_of(&f->scratch),
                                &item, jb_string_data(&expected))
                   : storage(f->failure, status);
      jb_string_destroy(&expected);
      return status;
    }
    if (list_contains(out, text))
      return fail_value(f->failure, f->path, path_text, value, rule->unique);
    status = storage(f->failure, list_push(out, text));
  }
  list_sort(out);
  return status;
}

static void feature_destroy(jh_feature_t *feature) {
  static const jh_feature_t empty = {0};
  jb_string_destroy(&feature->name);
  jb_string_destroy(&feature->domain);
  jb_string_destroy(&feature->note);
  list_destroy(&feature->implies);
  list_destroy(&feature->requirements);
  list_destroy(&feature->conflicts);
  list_destroy(&feature->feature_sources);
  list_destroy(&feature->portable_sources);
  list_destroy(&feature->dependencies);
  *feature = empty;
}

static hal_status_t feature_init(jh_feature_t *feature) {
  static const jh_feature_t empty = {0};
  *feature = empty;
  hal_status_t status = jb_string_init(&feature->name, heap());
  if (status == HAL_OK)
    status = jb_string_init(&feature->domain, heap());
  if (status == HAL_OK)
    status = jb_string_init(&feature->note, heap());
  jb_vector_t *lists[] = {&feature->implies,          &feature->requirements,
                          &feature->conflicts,        &feature->feature_sources,
                          &feature->portable_sources, &feature->dependencies};
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(lists); ++i)
    status = list_init(lists[i]);
  return status;
}

/* The optional member key of record: NULL when absent. */
static const jb_json_tree_view_t *optional(jb_json_tree_view_t record,
                                           const char *key,
                                           jb_json_tree_view_t *slot) {
  return member(record, key, slot) ? slot : NULL;
}

/* One entry of $.symbols, checked in the order of validate_fragment. */
static hal_status_t read_symbol(fragment_t *f, jb_json_span_t domain,
                                jb_json_span_t name, jb_json_tree_view_t record,
                                jh_feature_t *out) {
  static const char *const symbol_fields[] = {
      "kind", "implies", "requires", "conflicts", "buildEffects", "note"};
  static const char *const effect_fields[] = {
      "featureSources", "portableSources", "dependencies"};
  jb_string_t symbol_path = {0};
  hal_status_t status = jb_string_init(&symbol_path, heap());
  if (status == HAL_OK)
    status = append(&symbol_path, "$.symbols.");
  if (status == HAL_OK)
    status = append_span(&symbol_path, name);
  if (status != HAL_OK) {
    jb_string_destroy(&symbol_path);
    return storage(f->failure, status);
  }
  const jb_json_span_t base = text_of(&symbol_path);
  jb_json_tree_view_t slot = {0};
  jb_json_span_t text = {NULL, 0};
  if (!symbol_name(name)) {
    jb_string_t got = {0};
    status = jb_string_init(&got, heap());
    if (status == HAL_OK)
      status = jh_python_repr_append(&got, name);
    status = status == HAL_OK
                 ? fail_got(f->failure, f->path, base, &got,
                            "a HAL_ENABLE_* or HAL_DISABLE_* symbol")
                 : storage(f->failure, status);
    jb_string_destroy(&got);
  }
  if (status == HAL_OK)
    status = exact_fields(f, base, record, NULL, 0, symbol_fields,
                          COUNTOF(symbol_fields));
  // kind
  if (status == HAL_OK) {
    const jb_json_tree_view_t *kind = optional(record, "kind", &slot);
    bool known = kind == NULL;
    if (kind != NULL && is_string(*kind, &text)) {
      known = compare_text(text, jh_span("feature")) == 0 ||
              compare_text(text, jh_span("derived")) == 0;
      out->derived = compare_text(text, jh_span("derived")) == 0;
    }
    if (!known) {
      status = storage(f->failure, json_path(&f->json_path, base, ".kind"));
      if (status == HAL_OK)
        status = type_of(*kind) == JB_JSON_NULL
                     ? fail_value(f->failure, f->path, text_of(&f->json_path),
                                  NULL, "'feature' or 'derived'")
                     : fail_value(f->failure, f->path, text_of(&f->json_path),
                                  kind, "'feature' or 'derived'");
    }
  }
  // note: an absent or null note is no note
  if (status == HAL_OK) {
    const jb_json_tree_view_t *note = optional(record, "note", &slot);
    if (note != NULL && type_of(*note) != JB_JSON_NULL) {
      jb_json_span_t stripped = {NULL, 0};
      if (!is_string(*note, &text) || !strip(text, &stripped) ||
          stripped.length == 0) {
        status = storage(f->failure, json_path(&f->json_path, base, ".note"));
        if (status == HAL_OK)
          status = fail_value(f->failure, f->path, text_of(&f->json_path), note,
                              "a non-empty string");
      } else {
        status = storage(f->failure, append_span(&out->note, stripped));
        out->has_note = true;
      }
    }
  }
  // buildEffects: absent reads as {}, null is not an object
  jb_json_tree_view_t effects_slot = {0};
  const jb_json_tree_view_t *effects = NULL;
  if (status == HAL_OK) {
    effects = optional(record, "buildEffects", &effects_slot);
    if (effects != NULL) {
      status =
          storage(f->failure, json_path(&f->json_path, base, ".buildEffects"));
      jb_string_t effects_path = {0};
      if (status == HAL_OK)
        status = storage(f->failure, jb_string_init(&effects_path, heap()));
      if (status == HAL_OK)
        status = storage(f->failure,
                         append_span(&effects_path, text_of(&f->json_path)));
      if (status == HAL_OK)
        status = type_of(*effects) == JB_JSON_NULL
                     ? fail_value(f->failure, f->path, text_of(&effects_path),
                                  NULL, "an object")
                     : exact_fields(f, text_of(&effects_path), *effects, NULL,
                                    0, effect_fields, COUNTOF(effect_fields));
      jb_string_destroy(&effects_path);
    }
  }
  static const char *const effect_suffix[] = {".buildEffects.featureSources",
                                              ".buildEffects.portableSources",
                                              ".buildEffects.dependencies"};
  static const list_rule_t *const effect_rules[] = {&source_rule, &source_rule,
                                                    &dependency_rule};
  jb_vector_t *effect_lists[] = {&out->feature_sources, &out->portable_sources,
                                 &out->dependencies};
  for (size_t e = 0; status == HAL_OK && e < COUNTOF(effect_suffix); ++e) {
    if (e == 2) {
      // Disjoint sources are checked between the sources and dependencies.
      jb_vector_t overlap = {0};
      status = storage(f->failure, list_init(&overlap));
      for (size_t i = 0;
           status == HAL_OK && i < jb_vector_size(&out->feature_sources); ++i) {
        const jb_json_span_t source =
            text_of(list_at(&out->feature_sources, i));
        if (list_contains(&out->portable_sources, source))
          status = storage(f->failure, list_push(&overlap, source));
      }
      if (status == HAL_OK && jb_vector_size(&overlap) != 0) {
        status = storage(f->failure,
                         json_path(&f->json_path, base, ".buildEffects"));
        if (status == HAL_OK)
          status =
              fail_list(f->failure, f->path, text_of(&f->json_path), &overlap,
                        "disjoint featureSources and "
                        "portableSources");
      }
      list_destroy(&overlap);
      if (status != HAL_OK)
        break;
    }
    jb_json_tree_view_t list_slot = {0};
    const jb_json_tree_view_t *list =
        effects == NULL ? NULL
                        : optional(*effects, effect_suffix[e] + 14, &list_slot);
    jb_string_t list_path = {0};
    status = storage(f->failure, jb_string_init(&list_path, heap()));
    if (status == HAL_OK)
      status =
          storage(f->failure, json_path(&list_path, base, effect_suffix[e]));
    if (status == HAL_OK)
      status = normalized_list(f, text_of(&list_path), list, effect_rules[e],
                               effect_lists[e]);
    jb_string_destroy(&list_path);
  }
  static const char *const relations[] = {"implies", "requires", "conflicts"};
  jb_vector_t *relation_lists[] = {&out->implies, &out->requirements,
                                   &out->conflicts};
  for (size_t r = 0; status == HAL_OK && r < COUNTOF(relations); ++r) {
    jb_string_t relation_path = {0};
    status = storage(f->failure, jb_string_init(&relation_path, heap()));
    if (status == HAL_OK)
      status = storage(f->failure, json_path(&relation_path, base, "."));
    if (status == HAL_OK)
      status = storage(f->failure, append(&relation_path, relations[r]));
    if (status == HAL_OK)
      status = normalized_list(f, text_of(&relation_path),
                               optional(record, relations[r], &slot),
                               &relation_rule, relation_lists[r]);
    jb_string_destroy(&relation_path);
  }
  if (status == HAL_OK)
    status = storage(f->failure, append_span(&out->name, name));
  if (status == HAL_OK)
    status = storage(f->failure, append_span(&out->domain, domain));
  jb_string_destroy(&symbol_path);
  return status;
}

/* The filename stem: the name without its last suffix. */
static jb_json_span_t stem_of(jb_json_span_t path) {
  size_t begin = path.length;
  while (begin > 0 && path.data[begin - 1] != '/' &&
         path.data[begin - 1] != '\\')
    --begin;
  size_t end = path.length;
  for (size_t at = path.length; at > begin + 1; --at)
    if (path.data[at - 1] == '.') {
      end = at - 1;
      break;
    }
  const jb_json_span_t stem = {path.data + begin, end - begin};
  return stem;
}

/* validate_fragment: the header fields of one fragment, then its symbols,
 * appended to features. */
static hal_status_t read_fragment(fragment_t *f, jb_json_tree_view_t document,
                                  size_t owner, jb_vector_t *features) {
  static const char *const fields[] = {"$schema", "schemaVersion", "domain",
                                       "symbols"};
  hal_status_t status = exact_fields(f, jh_span("$"), document, fields,
                                     COUNTOF(fields), fields, COUNTOF(fields));
  jb_json_tree_view_t value = {0};
  jb_json_span_t text = {NULL, 0};
  if (status == HAL_OK && member(document, "$schema", &value) &&
      !(is_string(value, &text) &&
        compare_text(text, jh_span("../features.schema.json")) == 0))
    status = fail_value(f->failure, f->path, jh_span("$.$schema"), &value,
                        "'../features.schema.json'");
  if (status == HAL_OK && member(document, "schemaVersion", &value) &&
      !(type_of(value) == JB_JSON_NUMBER &&
        jb_json_tree_text(value, &text) == HAL_OK &&
        compare_text(text, jh_span("1")) == 0))
    status = fail_value(f->failure, f->path, jh_span("$.schemaVersion"), &value,
                        "the integer 1");
  jb_json_span_t domain = {NULL, 0};
  if (status == HAL_OK && member(document, "domain", &value) &&
      !(is_string(value, &domain) && domain_name(domain)))
    status = fail_value(f->failure, f->path, jh_span("$.domain"), &value,
                        "a lower-case domain identifier");
  if (status == HAL_OK && compare_text(stem_of(f->path), domain) != 0) {
    jb_string_t expected = {0};
    status = storage(f->failure, jb_string_init(&expected, heap()));
    if (status == HAL_OK)
      status = storage(f->failure, append(&expected, "the filename stem "));
    if (status == HAL_OK)
      status = storage(f->failure,
                       jh_python_repr_append(&expected, stem_of(f->path)));
    if (status == HAL_OK)
      status = fail_value(f->failure, f->path, jh_span("$.domain"), &value,
                          jb_string_data(&expected));
    jb_string_destroy(&expected);
  }
  jb_json_tree_view_t symbols = {0};
  if (status == HAL_OK && member(document, "symbols", &symbols) &&
      (type_of(symbols) != JB_JSON_OBJECT || count_of(symbols) == 0))
    status = fail_value(f->failure, f->path, jh_span("$.symbols"), &symbols,
                        "a non-empty symbol object");
  for (size_t i = 0; status == HAL_OK && i < count_of(symbols); ++i) {
    jb_json_tree_view_t record = {0};
    jb_json_span_t name = {NULL, 0};
    status = storage(f->failure, jb_json_tree_at(symbols, i, &record));
    if (status == HAL_OK)
      status = storage(f->failure, jb_json_tree_key(record, &name));
    jh_feature_t feature = {0};
    if (status == HAL_OK)
      status = storage(f->failure, feature_init(&feature));
    feature.owner = owner;
    if (status == HAL_OK)
      status = read_symbol(f, domain, name, record, &feature);
    if (status == HAL_OK)
      status = storage(f->failure, jb_vector_append(features, &feature, 1));
    if (status != HAL_OK)
      feature_destroy(&feature);
  }
  return status;
}

/* ---- The registry ---- */

static jh_feature_t *feature_at(const jb_vector_t *features, size_t index) {
  return jb_vector_at(features, index);
}

static int compare_features(const void *left, const void *right) {
  return compare_text(text_of(&((const jh_feature_t *)left)->name),
                      text_of(&((const jh_feature_t *)right)->name));
}

/* The index of the feature called name, or SIZE_MAX. */
static size_t find_feature(const jb_vector_t *features, jb_json_span_t name) {
  size_t low = 0, high = jb_vector_size(features);
  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    const int order =
        compare_text(text_of(&feature_at(features, middle)->name), name);
    if (order == 0)
      return middle;
    if (order < 0)
      low = middle + 1;
    else
      high = middle;
  }
  return SIZE_MAX;
}

void jh_feature_registry_init(jh_feature_registry_t *registry) {
  static const jh_feature_registry_t empty = {0};
  *registry = empty;
  // Initialization only checks its arguments, which are valid here.
  (void)jb_vector_init(&registry->features, sizeof(jh_feature_t), heap());
  (void)list_init(&registry->fragments);
}

void jh_feature_registry_destroy(jh_feature_registry_t *registry) {
  static const jh_feature_registry_t empty = {0};
  for (size_t i = 0; i < jb_vector_size(&registry->features); ++i)
    feature_destroy(feature_at(&registry->features, i));
  jb_vector_destroy(&registry->features);
  list_destroy(&registry->fragments);
  *registry = empty;
}

/* Collects the *.json names of a directory as Path.glob("*.json") does,
 * hidden names and directories included. */
static hal_status_t collect_fragment(void *context, const char *name,
                                     const jb_fs_info_t *info) {
  (void)info;
  const size_t length = strlen(name);
  if (length < 5 || strcmp(name + length - 5, ".json") != 0)
    return HAL_OK;
  return list_push(context, jh_span(name));
}

/* The repr of name, appended for a message. */
static hal_status_t append_repr(jb_string_t *out, jb_json_span_t name) {
  return jh_python_repr_append(out, name);
}

/* The relations, kinds and build inputs every symbol must satisfy once all
 * fragments are read. */
static hal_status_t check_relations(const jh_feature_registry_t *registry,
                                    jh_failure_t *failure) {
  static const char *const relation_names[] = {"implies", "requires",
                                               "conflicts"};
  const jb_vector_t *features = &registry->features;
  for (size_t i = 0; i < jb_vector_size(features); ++i) {
    const jh_feature_t *feature = feature_at(features, i);
    const jb_json_span_t owner =
        text_of(list_at(&registry->fragments, feature->owner));
    const jb_json_span_t name = text_of(&feature->name);
    const jb_vector_t *relations[] = {&feature->implies, &feature->requirements,
                                      &feature->conflicts};
    for (size_t r = 0; r < COUNTOF(relations); ++r)
      for (size_t t = 0; t < jb_vector_size(relations[r]); ++t) {
        const jb_json_span_t target = text_of(list_at(relations[r], t));
        const bool self = compare_text(target, name) == 0;
        if (!self && find_feature(features, target) != SIZE_MAX)
          continue;
        jb_string_t message = {0};
        hal_status_t status = jb_string_init(&message, heap());
        if (status == HAL_OK)
          status = append_span(&message, owner);
        if (status == HAL_OK)
          status = append(&message, ": $.symbols.");
        if (status == HAL_OK)
          status = append_span(&message, name);
        if (status == HAL_OK)
          status = append(&message, ".");
        if (status == HAL_OK)
          status = append(&message, relation_names[r]);
        if (status == HAL_OK)
          status = append(&message, self ? ": a feature cannot reference itself"
                                         : ": unknown symbol ");
        if (status == HAL_OK && !self)
          status = append_repr(&message, target);
        status = status == HAL_OK ? fail_message(failure, &message)
                                  : storage(failure, status);
        jb_string_destroy(&message);
        return status;
      }
    const bool disable =
        name.length >= 12 && memcmp(name.data, "HAL_DISABLE_", 12) == 0;
    const char *rule = NULL;
    if (disable && feature->derived)
      rule = ".kind: HAL_DISABLE_* symbols cannot be derived";
    else if (disable && (jb_vector_size(&feature->feature_sources) != 0 ||
                         jb_vector_size(&feature->portable_sources) != 0 ||
                         jb_vector_size(&feature->dependencies) != 0))
      rule = ".buildEffects: HAL_DISABLE_* symbols cannot add build inputs";
    if (rule != NULL) {
      const jb_json_span_t parts[] = {owner, jh_span(": $.symbols."), name,
                                      jh_span(rule)};
      return fail_parts(failure, parts, COUNTOF(parts));
    }
  }
  return HAL_OK;
}

/* detect_implies_cycles: a depth-first walk in name order. */
static hal_status_t check_cycles(const jh_feature_registry_t *registry,
                                 jh_failure_t *failure) {
  const jb_vector_t *features = &registry->features;
  const size_t count = jb_vector_size(features);
  jb_vector_t state = {0}, path = {0}, next = {0};
  hal_status_t status = jb_vector_init(&state, 1, heap());
  if (status == HAL_OK)
    status = jb_vector_init(&path, sizeof(size_t), heap());
  if (status == HAL_OK)
    status = jb_vector_init(&next, sizeof(size_t), heap());
  if (status == HAL_OK)
    status = jb_vector_resize(&state, count); // 0 new, 1 visiting, 2 visited
  unsigned char *marks = jb_vector_data(&state);
  for (size_t root = 0; status == HAL_OK && root < count; ++root) {
    if (marks[root] == 2)
      continue;
    const size_t zero = 0;
    status = jb_vector_append(&path, &root, 1);
    if (status == HAL_OK)
      status = jb_vector_append(&next, &zero, 1);
    marks[root] = 1;
    while (status == HAL_OK && jb_vector_size(&path) != 0) {
      const size_t depth = jb_vector_size(&path) - 1;
      const size_t current = *(const size_t *)jb_vector_const_at(&path, depth);
      size_t *child = jb_vector_at(&next, depth);
      const jb_vector_t *implies = &feature_at(features, current)->implies;
      if (*child == jb_vector_size(implies)) {
        marks[current] = 2;
        (void)jb_vector_resize(&path, depth);
        (void)jb_vector_resize(&next, depth);
        continue;
      }
      const size_t target =
          find_feature(features, text_of(list_at(implies, (*child)++)));
      if (marks[target] == 2)
        continue;
      if (marks[target] == 1) {
        jb_string_t message = {0};
        status = jb_string_init(&message, heap());
        if (status == HAL_OK)
          status = append(&message, "feature registry: implies cycle: ");
        bool started = false;
        for (size_t d = 0; status == HAL_OK && d <= depth; ++d) {
          const size_t at = *(const size_t *)jb_vector_const_at(&path, d);
          started = started || at == target;
          if (!started)
            continue;
          status =
              append_span(&message, text_of(&feature_at(features, at)->name));
          if (status == HAL_OK)
            status = append(&message, " -> ");
        }
        if (status == HAL_OK)
          status = append_span(&message,
                               text_of(&feature_at(features, target)->name));
        status = status == HAL_OK ? fail_message(failure, &message)
                                  : storage(failure, status);
        jb_string_destroy(&message);
        break;
      }
      marks[target] = 1;
      status = jb_vector_append(&path, &target, 1);
      if (status == HAL_OK)
        status = jb_vector_append(&next, &zero, 1);
      marks = jb_vector_data(&state);
    }
  }
  if (status != HAL_OK && failure->status == HAL_OK)
    status = storage(failure, status);
  jb_vector_destroy(&next);
  jb_vector_destroy(&path);
  jb_vector_destroy(&state);
  return status;
}

/* The transitive implications of a feature, as indices in name order. */
static hal_status_t closure(const jb_vector_t *features, size_t index,
                            jb_vector_t *out) {
  jb_vector_t seen = {0}, pending = {0};
  hal_status_t status = jb_vector_init(&seen, 1, heap());
  if (status == HAL_OK)
    status = jb_vector_init(&pending, sizeof(size_t), heap());
  if (status == HAL_OK)
    status = jb_vector_resize(&seen, jb_vector_size(features));
  if (status == HAL_OK)
    status = jb_vector_append(&pending, &index, 1);
  jb_vector_clear(out);
  while (status == HAL_OK && jb_vector_size(&pending) != 0) {
    const size_t last = jb_vector_size(&pending) - 1;
    const size_t current = *(const size_t *)jb_vector_const_at(&pending, last);
    (void)jb_vector_resize(&pending, last);
    const jb_vector_t *implies = &feature_at(features, current)->implies;
    unsigned char *marks = jb_vector_data(&seen);
    for (size_t i = 0; status == HAL_OK && i < jb_vector_size(implies); ++i) {
      const size_t target =
          find_feature(features, text_of(list_at(implies, i)));
      if (marks[target])
        continue;
      marks[target] = 1;
      status = jb_vector_append(&pending, &target, 1);
    }
  }
  const unsigned char *marks = jb_vector_const_data(&seen);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(features); ++i)
    if (marks[i])
      status = jb_vector_append(out, &i, 1);
  jb_vector_destroy(&pending);
  jb_vector_destroy(&seen);
  return status;
}

/* Symmetric conflicts, and no symbol whose closure holds a conflict. */
static hal_status_t check_conflicts(const jh_feature_registry_t *registry,
                                    jh_failure_t *failure) {
  const jb_vector_t *features = &registry->features;
  jb_vector_t active = {0};
  hal_status_t status = jb_vector_init(&active, sizeof(size_t), heap());
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(features); ++i) {
    const jh_feature_t *feature = feature_at(features, i);
    const jb_json_span_t name = text_of(&feature->name);
    for (size_t c = 0; c < jb_vector_size(&feature->conflicts); ++c) {
      const jb_json_span_t target = text_of(list_at(&feature->conflicts, c));
      const jh_feature_t *other =
          feature_at(features, find_feature(features, target));
      if (list_contains(&other->conflicts, name))
        continue;
      const jb_json_span_t parts[] = {
          text_of(list_at(&registry->fragments, feature->owner)),
          jh_span(": $.symbols."),
          name,
          jh_span(".conflicts: "),
          target,
          jh_span(" must declare the symmetric conflict")};
      jb_vector_destroy(&active);
      return fail_parts(failure, parts, COUNTOF(parts));
    }
    status = closure(features, i, &active);
    // The closure never holds the symbol itself: cycles are rejected first.
    if (status == HAL_OK)
      status = jb_vector_append(&active, &i, 1);
    if (status != HAL_OK)
      break;
    size_t *indices = jb_vector_data(&active);
    const size_t count = jb_vector_size(&active);
    for (size_t a = count - 1; a > 0 && indices[a] < indices[a - 1]; --a) {
      const size_t swap = indices[a];
      indices[a] = indices[a - 1];
      indices[a - 1] = swap;
    }
    for (size_t a = 0; a < count; ++a) {
      const jh_feature_t *candidate = feature_at(features, indices[a]);
      for (size_t b = 0; b < count; ++b) {
        const jb_json_span_t other =
            text_of(&feature_at(features, indices[b])->name);
        if (!list_contains(&candidate->conflicts, other))
          continue;
        const jb_json_span_t parts[] = {
            jh_span("feature registry: "),
            name,
            jh_span(" resolves to conflicting symbols "),
            text_of(&candidate->name),
            jh_span(" and "),
            other};
        jb_vector_destroy(&active);
        return fail_parts(failure, parts, COUNTOF(parts));
      }
    }
  }
  jb_vector_destroy(&active);
  return storage(failure, status);
}

/* ---- Digests: SHA-256 of json.dumps(sort_keys=True, separators=(",", ":"),
 * ensure_ascii=True) ---- */

static hal_status_t sha_sink(void *user, const char *data, size_t length) {
  jh_sha256_update(user, (const uint8_t *)data, length);
  return HAL_OK;
}

static hal_status_t string_sink(void *user, const char *data, size_t length) {
  return jb_string_append(user, data, length);
}

static void hex_digest(const uint8_t digest[JH_SHA256_DIGEST_BYTES],
                       char out[65]) {
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < JH_SHA256_DIGEST_BYTES; ++i) {
    out[2 * i] = hex[digest[i] >> 4];
    out[2 * i + 1] = hex[digest[i] & 15];
  }
  out[64] = '\0';
}

/* The digest of a tree value: compact, sorted keys, ASCII escapes. */
static hal_status_t tree_digest(jb_json_tree_view_t value, char out[65]) {
  jh_sha256_context_t context;
  jh_sha256_init(&context);
  const jb_json_tree_format_t format = {SIZE_MAX, 0, true, true};
  size_t written = 0;
  const hal_status_t status =
      jb_json_tree_format(value, &format, sha_sink, &context, &written);
  uint8_t digest[JH_SHA256_DIGEST_BYTES];
  jh_sha256_final(&context, digest);
  if (status == HAL_OK)
    hex_digest(digest, out);
  return status;
}

static hal_status_t write_list(jb_json_writer_t *writer, const char *key,
                               const jb_vector_t *list) {
  hal_status_t status = jb_json_writer_key(writer, jh_span(key));
  if (status == HAL_OK)
    status = jb_json_writer_array_begin(writer);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(list); ++i)
    status = jb_json_writer_string(writer, text_of(list_at(list, i)));
  return status == HAL_OK ? jb_json_writer_array_end(writer) : status;
}

/* The normalized registry document of normalized_digest, as JSON text. */
static hal_status_t normalized_document(const jb_vector_t *features,
                                        jb_string_t *out) {
  size_t key_bytes = 256;
  for (size_t i = 0; i < jb_vector_size(features); ++i)
    key_bytes += jb_string_size(&feature_at(features, i)->name) + 1;
  const size_t keys = jb_vector_size(features) + 16;
  const jb_json_writer_limits_t limits = {SIZE_MAX / 2, 8, keys, key_bytes,
                                          keys * key_bytes + 4096};
  jb_vector_t scratch = {0};
  jb_json_writer_t writer = {NULL, HAL_NONE};
  size_t bytes = 0;
  hal_status_t status = jb_vector_init(&scratch, 1, heap());
  if (status == HAL_OK)
    status = jb_json_writer_workspace_size(&limits, &bytes);
  if (status == HAL_OK)
    status = jb_vector_resize(&scratch, bytes);
  const jb_json_workspace_t work = {jb_vector_data(&scratch),
                                    jb_vector_size(&scratch)};
  if (status == HAL_OK)
    status = jb_json_writer_init(&writer, &limits, work, string_sink, out);
  if (status == HAL_OK)
    status = jb_json_writer_object_begin(&writer);
  if (status == HAL_OK)
    status = jb_json_writer_key(&writer, jh_span("schemaVersion"));
  if (status == HAL_OK)
    status = jb_json_writer_u64(&writer, JH_FEATURE_SCHEMA_VERSION);
  if (status == HAL_OK)
    status = jb_json_writer_key(&writer, jh_span("symbols"));
  if (status == HAL_OK)
    status = jb_json_writer_object_begin(&writer);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(features); ++i) {
    const jh_feature_t *feature = feature_at(features, i);
    status = jb_json_writer_key(&writer, text_of(&feature->name));
    if (status == HAL_OK)
      status = jb_json_writer_object_begin(&writer);
    if (status == HAL_OK)
      status = jb_json_writer_key(&writer, jh_span("domain"));
    if (status == HAL_OK)
      status = jb_json_writer_string(&writer, text_of(&feature->domain));
    if (status == HAL_OK)
      status = jb_json_writer_key(&writer, jh_span("kind"));
    if (status == HAL_OK)
      status = jb_json_writer_string(
          &writer, jh_span(feature->derived ? "derived" : "feature"));
    if (status == HAL_OK)
      status = write_list(&writer, "implies", &feature->implies);
    if (status == HAL_OK)
      status = write_list(&writer, "requires", &feature->requirements);
    if (status == HAL_OK)
      status = write_list(&writer, "conflicts", &feature->conflicts);
    if (status == HAL_OK)
      status = jb_json_writer_key(&writer, jh_span("buildEffects"));
    if (status == HAL_OK)
      status = jb_json_writer_object_begin(&writer);
    if (status == HAL_OK)
      status = write_list(&writer, "featureSources", &feature->feature_sources);
    if (status == HAL_OK)
      status =
          write_list(&writer, "portableSources", &feature->portable_sources);
    if (status == HAL_OK)
      status = write_list(&writer, "dependencies", &feature->dependencies);
    if (status == HAL_OK)
      status = jb_json_writer_object_end(&writer);
    if (status == HAL_OK)
      status = jb_json_writer_key(&writer, jh_span("note"));
    if (status == HAL_OK)
      status = feature->has_note
                   ? jb_json_writer_string(&writer, text_of(&feature->note))
                   : jb_json_writer_null(&writer);
    if (status == HAL_OK)
      status = jb_json_writer_object_end(&writer);
  }
  if (status == HAL_OK)
    status = jb_json_writer_object_end(&writer);
  if (status == HAL_OK)
    status = jb_json_writer_object_end(&writer);
  if (status == HAL_OK)
    status = jb_json_writer_finish(&writer, &bytes);
  jb_vector_destroy(&scratch);
  return status;
}

/* normalized_digest: the digest of the normalized document, keys sorted by
 * the formatter like sort_keys. */
static hal_status_t registry_digest(const jb_vector_t *features, char out[65]) {
  jb_string_t text = {0};
  jb_json_tree_t *tree = NULL;
  hal_status_t status = jb_string_init(&text, heap());
  if (status == HAL_OK)
    status = normalized_document(features, &text);
  const size_t length = jb_string_size(&text);
  const jb_json_limits_t limits = {length,     length + 1,        8, length, 64,
                                   length + 1, length * 64 + 4096};
  const jb_json_tree_limits_t tree_limits = {length + 1, 8, length,
                                             length * 64 + 4096};
  jb_json_tree_view_t root = {0};
  if (status == HAL_OK)
    status = jb_json_tree_parse(text_of(&text), &limits, &tree_limits, 0, &tree,
                                NULL);
  if (status == HAL_OK)
    status = jb_json_tree_root(tree, &root);
  if (status == HAL_OK)
    status = tree_digest(root, out);
  if (tree != NULL)
    jb_json_tree_destroy(tree);
  jb_string_destroy(&text);
  return status;
}

/* ---- Loading ---- */

/* "{path}: invalid JSON: {reason}", with the line and column of a parse
 * error. */
static hal_status_t fail_json(jh_failure_t *failure, jb_json_span_t path,
                              const jh_json_location_t *where,
                              const jh_failure_t *reason) {
  if (reason->status == HAL_ENOMEM)
    return jh_fail_storage(failure, HAL_ENOMEM);
  char place[64] = "";
  if (where->located)
    (void)snprintf(place, sizeof place, " (line %zu, column %zu)", where->line,
                   where->column);
  const jb_json_span_t parts[] = {path, jh_span(": invalid JSON: "),
                                  text_of(&reason->message), jh_span(place)};
  return fail_parts(failure, parts, COUNTOF(parts));
}

/* load_json_object: a file as json.loads reads it, whose root must be an
 * object. */
static hal_status_t load_object(jb_json_span_t path, size_t limit,
                                jb_json_tree_t **tree,
                                jb_json_tree_view_t *root,
                                jh_failure_t *failure) {
  jh_json_location_t where;
  jh_failure_t reason;
  jh_json_location_init(&where);
  jh_failure_init(&reason);
  hal_status_t status =
      jh_json_read_value(&where, path.data, limit, tree, &reason);
  if (status != HAL_OK)
    status = fail_json(failure, path, &where, &reason);
  if (status == HAL_OK)
    status = storage(failure, jb_json_tree_root(*tree, root));
  if (status == HAL_OK && type_of(*root) != JB_JSON_OBJECT)
    status = fail_value(failure, path, jh_span("$"), root, "an object");
  jh_failure_destroy(&reason);
  jh_json_location_destroy(&where);
  return status;
}

/* The managed dependency names of the schema's managedBuildDependency enum. */
static hal_status_t managed_dependencies(jb_json_span_t schema_path,
                                         jb_json_tree_view_t schema,
                                         jb_vector_t *out,
                                         jh_failure_t *failure) {
  jb_json_tree_view_t definitions = {0}, dependency = {0}, values = {0};
  const bool found =
      member(schema, "$defs", &definitions) &&
      member(definitions, "managedBuildDependency", &dependency) &&
      member(dependency, "enum", &values);
  bool valid = found && type_of(values) == JB_JSON_ARRAY && count_of(values);
  hal_status_t status = HAL_OK;
  for (size_t i = 0; valid && status == HAL_OK && i < count_of(values); ++i) {
    jb_json_tree_view_t item = {0};
    jb_json_span_t text = {NULL, 0};
    status = storage(failure, jb_json_tree_at(values, i, &item));
    valid = status == HAL_OK && is_string(item, &text) && domain_name(text) &&
            !list_contains(out, text);
    if (valid)
      status = storage(failure, list_push(out, text));
  }
  if (status != HAL_OK)
    return status;
  if (!valid)
    return fail_value(
        failure, schema_path, jh_span("$.$defs.managedBuildDependency.enum"),
        found ? &values : NULL, "unique lower-case managed dependency names");
  list_sort(out);
  return HAL_OK;
}

/* The fragments of features_root in name order. */
static hal_status_t fragment_names(jb_json_span_t features_root,
                                   jb_vector_t *names, jh_failure_t *failure) {
  const hal_status_t listed =
      jb_fs_list(features_root.data, collect_fragment, names);
  if (listed != HAL_OK && listed != HAL_ENOENT) {
    if (listed == HAL_ENOMEM)
      return jh_fail_storage(failure, listed);
    const jb_json_span_t parts[] = {features_root,
                                    jh_span(": cannot list fragments: "),
                                    jh_span(hal_status_to_string(listed))};
    return fail_parts(failure, parts, COUNTOF(parts));
  }
  if (jb_vector_size(names) == 0) {
    const jb_json_span_t parts[] = {features_root,
                                    jh_span(": no feature fragments found")};
    return fail_parts(failure, parts, COUNTOF(parts));
  }
  list_sort(names);
  return HAL_OK;
}

/* Read every fragment into the registry, rejecting a symbol declared twice. */
static hal_status_t read_fragments(jh_feature_registry_t *registry,
                                   jb_json_span_t features_root,
                                   const jb_vector_t *managed, size_t limit,
                                   jh_failure_t *failure) {
  jb_vector_t names = {0};
  hal_status_t status = storage(failure, list_init(&names));
  if (status == HAL_OK)
    status = fragment_names(features_root, &names, failure);
  fragment_t f = {{NULL, 0}, {0}, {0}, managed, failure};
  if (status == HAL_OK)
    status = storage(failure, jb_string_init(&f.json_path, heap()));
  if (status == HAL_OK)
    status = storage(failure, jb_string_init(&f.scratch, heap()));
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&names); ++i) {
    jb_string_t path = {0};
    status = storage(failure, jb_string_init(&path, heap()));
    if (status == HAL_OK)
      status = jh_absolute_path(jb_string_data(list_at(&names, i)),
                                features_root.data, &path, failure);
    if (status == HAL_OK)
      status =
          storage(failure, list_push(&registry->fragments, text_of(&path)));
    jb_string_destroy(&path);
    if (status != HAL_OK)
      break;
    const size_t owner = jb_vector_size(&registry->fragments) - 1;
    f.path = text_of(list_at(&registry->fragments, owner));
    jb_json_tree_t *tree = NULL;
    jb_json_tree_view_t document = {0};
    const size_t first = jb_vector_size(&registry->features);
    status = load_object(f.path, limit, &tree, &document, failure);
    if (status == HAL_OK)
      status = read_fragment(&f, document, owner, &registry->features);
    if (tree != NULL)
      jb_json_tree_destroy(tree);
    // A symbol of this fragment that an earlier one declared.
    for (size_t n = first;
         status == HAL_OK && n < jb_vector_size(&registry->features); ++n) {
      const jh_feature_t *feature = feature_at(&registry->features, n);
      for (size_t o = 0; o < first; ++o) {
        const jh_feature_t *earlier = feature_at(&registry->features, o);
        if (compare_text(text_of(&earlier->name), text_of(&feature->name)))
          continue;
        const jb_json_span_t parts[] = {
            f.path, jh_span(": $.symbols."), text_of(&feature->name),
            jh_span(": duplicate symbol; first declared in "),
            text_of(list_at(&registry->fragments, earlier->owner))};
        status = fail_parts(failure, parts, COUNTOF(parts));
        break;
      }
    }
  }
  jb_string_destroy(&f.scratch);
  jb_string_destroy(&f.json_path);
  list_destroy(&names);
  return status;
}

hal_status_t jh_feature_registry_load(jh_feature_registry_t *registry,
                                      const char *config_root, size_t limit,
                                      jh_failure_t *failure) {
  if (registry == NULL || config_root == NULL ||
      jb_vector_size(&registry->features) != 0)
    return jh_fail_arguments(failure);
  jb_string_t schema_path = {0}, features_root = {0};
  jb_vector_t managed = {0};
  jb_json_tree_t *schema = NULL;
  jb_json_tree_view_t schema_root = {0};
  jb_json_tree_view_t value = {0};
  jb_json_span_t text = {NULL, 0};
  hal_status_t status = storage(failure, jb_string_init(&schema_path, heap()));
  if (status == HAL_OK)
    status = storage(failure, jb_string_init(&features_root, heap()));
  if (status == HAL_OK)
    status = storage(failure, list_init(&managed));
  if (status == HAL_OK)
    status = jh_absolute_path("features.schema.json", config_root, &schema_path,
                              failure);
  if (status == HAL_OK)
    status = load_object(text_of(&schema_path), limit, &schema, &schema_root,
                         failure);
  if (status == HAL_OK) {
    const bool present = member(schema_root, "$schema", &value);
    if (!(present && is_string(value, &text) &&
          compare_text(
              text, jh_span("https://json-schema.org/draft/2020-12/schema")) ==
              0))
      status =
          fail_value(failure, text_of(&schema_path), jh_span("$.$schema"),
                     present ? &value : NULL, "the JSON Schema 2020-12 URI");
  }
  if (status == HAL_OK)
    status = managed_dependencies(text_of(&schema_path), schema_root, &managed,
                                  failure);
  if (status == HAL_OK)
    status = jh_absolute_path("features", config_root, &features_root, failure);
  if (status == HAL_OK)
    status = read_fragments(registry, text_of(&features_root), &managed, limit,
                            failure);
  if (status == HAL_OK) {
    jb_vector_t *features = &registry->features;
    if (jb_vector_size(features) > 1)
      qsort(jb_vector_data(features), jb_vector_size(features),
            sizeof(jh_feature_t), compare_features);
    status = check_relations(registry, failure);
  }
  if (status == HAL_OK)
    status = check_cycles(registry, failure);
  if (status == HAL_OK)
    status = storage(failure,
                     registry_digest(&registry->features, registry->digest));
  if (status == HAL_OK)
    status =
        storage(failure, tree_digest(schema_root, registry->schema_digest));
  if (status == HAL_OK)
    status = check_conflicts(registry, failure);
  if (schema != NULL)
    jb_json_tree_destroy(schema);
  list_destroy(&managed);
  jb_string_destroy(&features_root);
  jb_string_destroy(&schema_path);
  return status;
}

/* ---- Resolving requests ---- */

hal_status_t jh_feature_registry_closure(const jh_feature_registry_t *registry,
                                         size_t index, jb_vector_t *indices) {
  if (registry == NULL || indices == NULL ||
      index >= jb_vector_size(&registry->features))
    return HAL_EINVAL;
  return closure(&registry->features, index, indices);
}

void jh_feature_resolution_init(jh_feature_resolution_t *resolution) {
  static const jh_feature_resolution_t empty = {0};
  *resolution = empty;
  // Initialization only checks its arguments, which are valid here.
  (void)list_init(&resolution->requested);
  (void)list_init(&resolution->resolved);
  (void)jb_vector_init(&resolution->provenance, sizeof(jh_feature_provenance_t),
                       heap());
  (void)list_init(&resolution->findings);
}

void jh_feature_resolution_destroy(jh_feature_resolution_t *resolution) {
  static const jh_feature_resolution_t empty = {0};
  for (size_t i = 0; i < jb_vector_size(&resolution->provenance); ++i) {
    jh_feature_provenance_t *entry = jb_vector_at(&resolution->provenance, i);
    jb_string_destroy(&entry->symbol);
    list_destroy(&entry->sources);
  }
  jb_vector_destroy(&resolution->provenance);
  list_destroy(&resolution->requested);
  list_destroy(&resolution->resolved);
  list_destroy(&resolution->findings);
  *resolution = empty;
}

/* Move a finding into the resolution; message is released on failure. */
static hal_status_t push_finding(jh_feature_resolution_t *resolution,
                                 jb_string_t *message, hal_status_t status) {
  if (status == HAL_OK)
    status = jb_vector_append(&resolution->findings, message, 1);
  if (status != HAL_OK)
    jb_string_destroy(message);
  return status;
}

/* The finding made of count parts. */
static hal_status_t add_finding(jh_feature_resolution_t *resolution,
                                const jb_json_span_t *parts, size_t count) {
  jb_string_t message = {0};
  hal_status_t status = jb_string_init(&message, heap());
  for (size_t i = 0; i < count; ++i)
    put_span(&message, &status, parts[i]);
  return push_finding(resolution, &message, status);
}

/* validate_requested_symbol: the findings of one request, and whether it
 * takes part in the resolution. */
static hal_status_t check_request(const jh_feature_registry_t *registry,
                                  const jh_feature_request_t *request,
                                  jh_feature_resolution_t *resolution,
                                  bool *valid) {
  // Python split the source at its first colon and joined the parts again,
  // so a finding starts with the source, and with ":$" when it has none.
  const jb_json_span_t path = request->source;
  const bool located =
      path.length != 0 && memchr(path.data, ':', path.length) != NULL;
  const jb_json_span_t location = jh_span(located ? "" : ":$");
  const jb_json_span_t symbol = request->symbol;
  const size_t index = find_feature(&registry->features, symbol);
  *valid = false;
  if (index == SIZE_MAX) {
    const jb_json_span_t parts[] = {
        path, location, jh_span(": [JH-CFG-UNKNOWN] unknown feature "), symbol};
    return add_finding(resolution, parts, COUNTOF(parts));
  }
  const bool enabled =
      !request->has_value || jh_span_equal(request->value, jh_span("1"));
  hal_status_t status = HAL_OK;
  if (!enabled && jh_span_equal(request->value, jh_span("0"))) {
    const jb_json_span_t parts[] = {
        path, location, jh_span(": [JH-CFG-VALUE] "), symbol,
        jh_span("=0 is unsupported; omit the symbol to disable it")};
    status = add_finding(resolution, parts, COUNTOF(parts));
  } else if (!enabled) {
    jb_string_t message = {0};
    status = jb_string_init(&message, heap());
    const jb_json_span_t parts[] = {path, location,
                                    jh_span(": [JH-CFG-VALUE] "), symbol,
                                    jh_span(" has unsupported value ")};
    for (size_t i = 0; i < COUNTOF(parts); ++i)
      put_span(&message, &status, parts[i]);
    if (status == HAL_OK)
      status = jh_python_repr_append(
          &message, request->value.length == 0 ? jh_span("") : request->value);
    status = push_finding(resolution, &message, status);
  }
  const bool derived = feature_at(&registry->features, index)->derived;
  if (status == HAL_OK && derived) {
    const jb_json_span_t parts[] = {path, location,
                                    jh_span(": [JH-CFG-DERIVED] "), symbol,
                                    jh_span(" cannot be requested directly")};
    status = add_finding(resolution, parts, COUNTOF(parts));
  }
  *valid = enabled && !derived;
  return status;
}

static int compare_requests(const void *left, const void *right) {
  return compare_text(((const jh_feature_request_t *)left)->symbol,
                      ((const jh_feature_request_t *)right)->symbol);
}

static int compare_spans(const void *left, const void *right) {
  return compare_text(*(const jb_json_span_t *)left,
                      *(const jb_json_span_t *)right);
}

/* One requested symbol and its sources, count requests long: the redundant
 * request finding, the provenance entry and the requested name. */
static hal_status_t add_requested(jh_feature_resolution_t *resolution,
                                  jb_json_span_t context,
                                  const jh_feature_request_t *group,
                                  size_t count, jb_vector_t *sources) {
  jb_vector_clear(sources);
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < count; ++i)
    status = jb_vector_append(sources, &group[i].source, 1);
  if (status != HAL_OK)
    return status;
  jb_json_span_t *spans = jb_vector_data(sources);
  qsort(spans, count, sizeof *spans, compare_spans);
  size_t unique = 1;
  for (size_t i = 1; i < count; ++i)
    if (compare_text(spans[i], spans[unique - 1]) != 0)
      spans[unique++] = spans[i];
  if (count > 1) {
    jb_string_t message = {0};
    char times[32];
    const int written = snprintf(times, sizeof times, "%zu", count);
    status = jb_string_init(&message, heap());
    put_span(&message, &status, context);
    put(&message, &status, ": [JH-CFG-REDUNDANT] ");
    put_span(&message, &status, group->symbol);
    put(&message, &status, " is requested ");
    put_span(&message, &status, (jb_json_span_t){times, (size_t)written});
    put(&message, &status, " times by ");
    for (size_t i = 0; i < unique; ++i) {
      if (i != 0)
        put(&message, &status, ", ");
      put_span(&message, &status, spans[i]);
    }
    status = push_finding(resolution, &message, status);
  }
  jh_feature_provenance_t entry = {0};
  if (status == HAL_OK)
    status = jb_string_init(&entry.symbol, heap());
  if (status == HAL_OK)
    status = list_init(&entry.sources);
  put_span(&entry.symbol, &status, group->symbol);
  for (size_t i = 0; status == HAL_OK && i < unique; ++i)
    status = list_push(&entry.sources, spans[i]);
  if (status == HAL_OK)
    status = jb_vector_append(&resolution->provenance, &entry, 1);
  if (status != HAL_OK) {
    jb_string_destroy(&entry.symbol);
    list_destroy(&entry.sources);
    return status;
  }
  return list_push(&resolution->requested, group->symbol);
}

/* The findings of the resolved set: missing requirements and conflicts,
 * each conflicting pair once. */
static hal_status_t check_resolved(const jb_vector_t *features,
                                   const unsigned char *marks,
                                   jb_json_span_t context,
                                   jh_feature_resolution_t *resolution) {
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(features); ++i) {
    if (!marks[i])
      continue;
    const jh_feature_t *feature = feature_at(features, i);
    const jb_json_span_t name = text_of(&feature->name);
    for (size_t r = 0;
         status == HAL_OK && r < jb_vector_size(&feature->requirements); ++r) {
      const jb_json_span_t requirement =
          text_of(list_at(&feature->requirements, r));
      if (marks[find_feature(features, requirement)])
        continue;
      const jb_json_span_t parts[] = {context, jh_span(": [JH-CFG-REQUIRES] "),
                                      name, jh_span(" requires "), requirement};
      status = add_finding(resolution, parts, COUNTOF(parts));
    }
    for (size_t c = 0;
         status == HAL_OK && c < jb_vector_size(&feature->conflicts); ++c) {
      const jb_json_span_t conflict = text_of(list_at(&feature->conflicts, c));
      if (!marks[find_feature(features, conflict)] ||
          compare_text(name, conflict) >= 0)
        continue;
      const jb_json_span_t parts[] = {context, jh_span(": [JH-CFG-CONFLICT] "),
                                      name, jh_span(" conflicts with "),
                                      conflict};
      status = add_finding(resolution, parts, COUNTOF(parts));
    }
  }
  return status;
}

/* resolve_feature_requests into resolution, after any findings it holds. */
static hal_status_t resolve_requests(const jh_feature_registry_t *registry,
                                     const jh_feature_request_t *requests,
                                     size_t count, jb_json_span_t context,
                                     jh_feature_resolution_t *resolution) {
  const jb_vector_t *features = &registry->features;
  jb_vector_t valid = {0}, sources = {0}, marks = {0}, implied = {0};
  hal_status_t status =
      jb_vector_init(&valid, sizeof(jh_feature_request_t), heap());
  if (status == HAL_OK)
    status = jb_vector_init(&sources, sizeof(jb_json_span_t), heap());
  if (status == HAL_OK)
    status = jb_vector_init(&marks, 1, heap());
  if (status == HAL_OK)
    status = jb_vector_init(&implied, sizeof(size_t), heap());
  if (status == HAL_OK)
    status = jb_vector_resize(&marks, jb_vector_size(features));
  for (size_t i = 0; status == HAL_OK && i < count; ++i) {
    bool usable = false;
    status = check_request(registry, &requests[i], resolution, &usable);
    if (status == HAL_OK && usable)
      status = jb_vector_append(&valid, &requests[i], 1);
  }
  const size_t total = jb_vector_size(&valid);
  if (status == HAL_OK && total > 1)
    qsort(jb_vector_data(&valid), total, sizeof(jh_feature_request_t),
          compare_requests);
  const jh_feature_request_t *sorted = jb_vector_const_data(&valid);
  for (size_t first = 0, last = 0; status == HAL_OK && first < total;
       first = last) {
    last = first + 1;
    while (last < total &&
           jh_span_equal(sorted[last].symbol, sorted[first].symbol))
      ++last;
    status = add_requested(resolution, context, &sorted[first], last - first,
                           &sources);
    const size_t index = find_feature(features, sorted[first].symbol);
    if (status == HAL_OK)
      status = closure(features, index, &implied);
    unsigned char *marked = jb_vector_data(&marks);
    marked[index] = 1;
    for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&implied); ++i)
      marked[*(const size_t *)jb_vector_const_at(&implied, i)] = 1;
  }
  const unsigned char *marked = jb_vector_const_data(&marks);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(features); ++i)
    if (marked[i])
      status = list_push(&resolution->resolved,
                         text_of(&feature_at(features, i)->name));
  if (status == HAL_OK)
    status = check_resolved(features, marked, context, resolution);
  if (status == HAL_OK) {
    jh_sha256_context_t digest_context;
    jh_sha256_init(&digest_context);
    for (size_t i = 0; i < jb_vector_size(&resolution->resolved); ++i) {
      const jb_string_t *name = list_at(&resolution->resolved, i);
      if (i != 0)
        jh_sha256_update(&digest_context, (const uint8_t *)"\n", 1);
      jh_sha256_update(&digest_context, (const uint8_t *)jb_string_data(name),
                       jb_string_size(name));
    }
    uint8_t digest[JH_SHA256_DIGEST_BYTES];
    jh_sha256_final(&digest_context, digest);
    hex_digest(digest, resolution->digest);
  }
  jb_vector_destroy(&implied);
  jb_vector_destroy(&marks);
  jb_vector_destroy(&sources);
  jb_vector_destroy(&valid);
  return status;
}

static bool requested(const jh_feature_request_t *requests, size_t count,
                      jb_json_span_t symbol) {
  for (size_t i = 0; i < count; ++i)
    if (jh_span_equal(requests[i].symbol, symbol))
      return true;
  return false;
}

/* The provenance of a target's required symbol gains its source. */
static hal_status_t add_target_source(jh_feature_resolution_t *resolution,
                                      jb_json_span_t symbol,
                                      jb_json_span_t source) {
  jb_vector_t *provenance = &resolution->provenance;
  for (size_t i = 0; i < jb_vector_size(provenance); ++i) {
    jh_feature_provenance_t *entry = jb_vector_at(provenance, i);
    if (compare_text(text_of(&entry->symbol), symbol) != 0)
      continue;
    size_t at = 0;
    while (at < jb_vector_size(&entry->sources) &&
           compare_text(text_of(list_at(&entry->sources, at)), source) < 0)
      ++at;
    if (at < jb_vector_size(&entry->sources) &&
        compare_text(text_of(list_at(&entry->sources, at)), source) == 0)
      return HAL_OK;
    jb_string_t item = {0};
    hal_status_t status = jb_string_init(&item, heap());
    put_span(&item, &status, source);
    if (status == HAL_OK)
      status = jb_vector_insert(&entry->sources, at, &item, 1);
    if (status != HAL_OK)
      jb_string_destroy(&item);
    return status;
  }
  jh_feature_provenance_t entry = {0};
  hal_status_t status = jb_string_init(&entry.symbol, heap());
  if (status == HAL_OK)
    status = list_init(&entry.sources);
  put_span(&entry.symbol, &status, symbol);
  if (status == HAL_OK)
    status = list_push(&entry.sources, source);
  if (status == HAL_OK)
    status = jb_vector_append(provenance, &entry, 1);
  if (status != HAL_OK) {
    jb_string_destroy(&entry.symbol);
    list_destroy(&entry.sources);
  }
  return status;
}

/* resolve_target_feature_requests: the target's required features join the
 * resolution, and only the explicit requests stay requested. */
static hal_status_t resolve_target(const jh_feature_registry_t *registry,
                                   const jh_feature_request_t *requests,
                                   size_t count, jb_json_span_t context,
                                   const jh_feature_target_t *target,
                                   jh_feature_resolution_t *resolution) {
  static const char enable[] = "HAL_ENABLE_";
  jb_vector_t all = {0}, sources = {0};
  jb_string_t disabled = {0};
  hal_status_t status =
      jb_vector_init(&all, sizeof(jh_feature_request_t), heap());
  if (status == HAL_OK)
    status = list_init(&sources);
  if (status == HAL_OK)
    status = jb_string_init(&disabled, heap());
  if (status == HAL_OK && count != 0)
    status = jb_vector_append(&all, requests, count);
  for (size_t i = 0; status == HAL_OK && i < target->required_count; ++i) {
    char index[32];
    const int written = snprintf(index, sizeof index, "%zu", i);
    jb_string_t source = {0};
    status = jb_string_init(&source, heap());
    put(&source, &status, "target:");
    put_span(&source, &status, target->id);
    put(&source, &status, ":requiredFeatures[");
    put_span(&source, &status, (jb_json_span_t){index, (size_t)written});
    put(&source, &status, "]");
    if (status == HAL_OK)
      status = jb_vector_append(&sources, &source, 1);
    if (status != HAL_OK)
      jb_string_destroy(&source);
  }
  for (size_t i = 0; status == HAL_OK && i < target->required_count; ++i) {
    jb_json_span_t symbol = target->required[i];
    if (symbol.length >= 2 &&
        memcmp(symbol.data + symbol.length - 2, "=1", 2) == 0)
      symbol.length -= 2;
    // str.replace(..., 1): the first occurrence, wherever it is.
    size_t at = 0;
    while (at + sizeof enable - 1 <= symbol.length &&
           memcmp(symbol.data + at, enable, sizeof enable - 1) != 0)
      ++at;
    jb_string_clear(&disabled);
    if (at + sizeof enable - 1 <= symbol.length) {
      put_span(&disabled, &status, (jb_json_span_t){symbol.data, at});
      put(&disabled, &status, "HAL_DISABLE_");
      put_span(&disabled, &status,
               (jb_json_span_t){symbol.data + at + sizeof enable - 1,
                                symbol.length - at - (sizeof enable - 1)});
    } else {
      put_span(&disabled, &status, symbol);
    }
    if (status == HAL_OK && requested(requests, count, text_of(&disabled))) {
      const jb_json_span_t parts[] = {context,
                                      jh_span(": [JH-CFG-TARGET-REQUIRED] "),
                                      target->id,
                                      jh_span(" requires "),
                                      symbol,
                                      jh_span("; "),
                                      text_of(&disabled),
                                      jh_span(" cannot be requested")};
      status = add_finding(resolution, parts, COUNTOF(parts));
    }
    if (status == HAL_OK && !requested(requests, count, symbol)) {
      const jh_feature_request_t item = {symbol, jh_span("1"),
                                         text_of(list_at(&sources, i)), true};
      status = jb_vector_append(&all, &item, 1);
    }
  }
  if (status == HAL_OK)
    status = resolve_requests(registry, jb_vector_const_data(&all),
                              jb_vector_size(&all), context, resolution);
  for (size_t i = 0; status == HAL_OK && i < target->required_count; ++i) {
    jb_json_span_t symbol = target->required[i];
    if (symbol.length >= 2 &&
        memcmp(symbol.data + symbol.length - 2, "=1", 2) == 0)
      symbol.length -= 2;
    status =
        add_target_source(resolution, symbol, text_of(list_at(&sources, i)));
  }
  jb_vector_t *names = &resolution->requested;
  size_t kept = 0;
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(names); ++i) {
    jb_string_t *name = jb_vector_at(names, i);
    if (!requested(requests, count, text_of(name))) {
      jb_string_destroy(name);
      continue;
    }
    jb_string_t *slot = jb_vector_at(names, kept++);
    if (slot != name)
      *slot = *name;
  }
  if (status == HAL_OK)
    status = jb_vector_resize(names, kept);
  jb_string_destroy(&disabled);
  list_destroy(&sources);
  jb_vector_destroy(&all);
  return status;
}

hal_status_t jh_feature_resolve(const jh_feature_registry_t *registry,
                                const jh_feature_request_t *requests,
                                size_t count, jb_json_span_t context,
                                const jh_feature_target_t *target,
                                jh_feature_resolution_t *resolution,
                                jh_failure_t *failure) {
  if (registry == NULL || resolution == NULL ||
      (count != 0 && requests == NULL) ||
      (target != NULL && target->required_count != 0 &&
       target->required == NULL) ||
      jb_vector_size(&resolution->findings) != 0 ||
      jb_vector_size(&resolution->requested) != 0 ||
      jb_vector_size(&resolution->provenance) != 0)
    return jh_fail_arguments(failure);
  const hal_status_t status =
      target == NULL
          ? resolve_requests(registry, requests, count, context, resolution)
          : resolve_target(registry, requests, count, context, target,
                           resolution);
  return storage(failure, status);
}

/* ---- Generated artifacts ---- */

/* The header lines after the feature graph, before the verbose report. */
static const char verbose_preamble[] =
    "#endif /* JH_HAL_GENERATED_FEATURE_GRAPH_INCLUDED */\n"
    "\n"
    "#if defined(HAL_CONFIG_VERBOSE) &&                                      "
    "\\\n"
    "    !defined(JH_HAL_FEATURE_VERBOSE_REPORT_DEFERRED) &&                 "
    "\\\n"
    "    !defined(JH_HAL_FEATURE_VERBOSE_REPORT_EMITTED)\n"
    "#define JH_HAL_FEATURE_VERBOSE_REPORT_EMITTED 1\n"
    "/* Generated report of every active registered feature flag. */\n";

hal_status_t jh_feature_registry_header(const jh_feature_registry_t *registry,
                                        jb_string_t *out) {
  if (registry == NULL || out == NULL)
    return HAL_EINVAL;
  const jb_vector_t *features = &registry->features;
  const size_t count = jb_vector_size(features);
  jb_vector_t implied = {0};
  hal_status_t status = jb_vector_init(&implied, sizeof(size_t), heap());
  jb_string_clear(out);
  put(out, &status,
      "#ifndef JH_HAL_GENERATED_FEATURE_GRAPH_INCLUDED\n"
      "#define JH_HAL_GENERATED_FEATURE_GRAPH_INCLUDED 1\n"
      "/* Generated by jh generate features; do not edit. */\n"
      "#define JH_HAL_FEATURE_SCHEMA_VERSION ");
  put_number(out, &status, JH_FEATURE_SCHEMA_VERSION);
  put(out, &status, "\n#define JH_HAL_FEATURE_GENERATOR_VERSION ");
  put_number(out, &status, JH_FEATURE_GENERATOR_VERSION);
  put(out, &status, "\n#define JH_HAL_FEATURE_REGISTRY_DIGEST \"");
  put(out, &status, registry->digest);
  put(out, &status, "\"\n#define JH_HAL_FEATURE_SCHEMA_DIGEST \"");
  put(out, &status, registry->schema_digest);
  put(out, &status, "\"\n\n");
  for (size_t i = 0; i < count; ++i) {
    const jh_feature_t *feature = feature_at(features, i);
    if (!feature->derived)
      continue;
    put(out, &status, "#if defined(");
    put_span(out, &status, text_of(&feature->name));
    put(out, &status, ")\n#error \"[JH-CFG-DERIVED] ");
    put_span(out, &status, text_of(&feature->name));
    put(out, &status, " cannot be requested directly\"\n#endif\n\n");
  }
  for (size_t i = 0; status == HAL_OK && i < count; ++i) {
    status = closure(features, i, &implied);
    if (status != HAL_OK || jb_vector_size(&implied) == 0)
      continue;
    const jb_json_span_t name = text_of(&feature_at(features, i)->name);
    put(out, &status, "/* Resolved implications of ");
    put_span(out, &status, name);
    put(out, &status, ". */\n#if defined(");
    put_span(out, &status, name);
    put(out, &status, ")\n");
    for (size_t d = 0; d < jb_vector_size(&implied); ++d) {
      const jb_json_span_t dependency =
          text_of(&feature_at(features,
                              *(const size_t *)jb_vector_const_at(&implied, d))
                       ->name);
      put(out, &status, "#if !defined(");
      put_span(out, &status, dependency);
      put(out, &status, ")\n#define ");
      put_span(out, &status, dependency);
      put(out, &status, " 1\n#endif\n");
    }
    put(out, &status, "#endif\n\n");
  }
  for (size_t i = 0; i < count; ++i) {
    const jh_feature_t *feature = feature_at(features, i);
    const jb_json_span_t name = text_of(&feature->name);
    for (size_t r = 0; r < jb_vector_size(&feature->requirements); ++r) {
      const jb_json_span_t requirement =
          text_of(list_at(&feature->requirements, r));
      put(out, &status, "#if defined(");
      put_span(out, &status, name);
      put(out, &status, ") && !defined(");
      put_span(out, &status, requirement);
      put(out, &status, ")\n#error \"[JH-CFG-REQUIRES] ");
      put_span(out, &status, name);
      put(out, &status, " requires ");
      put_span(out, &status, requirement);
      put(out, &status, "\"\n#endif\n\n");
    }
    for (size_t c = 0; c < jb_vector_size(&feature->conflicts); ++c) {
      const jb_json_span_t conflict = text_of(list_at(&feature->conflicts, c));
      if (compare_text(name, conflict) >= 0)
        continue;
      put(out, &status, "#if defined(");
      put_span(out, &status, name);
      put(out, &status, ") && defined(");
      put_span(out, &status, conflict);
      put(out, &status, ")\n#error \"[JH-CFG-CONFLICT] ");
      put_span(out, &status, name);
      put(out, &status, " conflicts with ");
      put_span(out, &status, conflict);
      put(out, &status, "\"\n#endif\n\n");
    }
  }
  put(out, &status, verbose_preamble);
  for (size_t i = 0; i < count; ++i) {
    const jb_json_span_t name = text_of(&feature_at(features, i)->name);
    put(out, &status, "#if defined(");
    put_span(out, &status, name);
    put(out, &status, ")\n#pragma message(\"HAL_CONFIG: ");
    put_span(out, &status, name);
    put(out, &status, "\")\n#endif\n");
  }
  put(out, &status, "#endif /* generated HAL_CONFIG_VERBOSE report */\n");
  jb_vector_destroy(&implied);
  return status;
}

/* cmake_set: set(NAME "") or set(NAME / one quoted value per line / ). */
static void put_set(jb_string_t *out, hal_status_t *status, const char *prefix,
                    jb_json_span_t name, const char *suffix,
                    const jb_vector_t *values, const jb_vector_t *features,
                    bool indices) {
  put(out, status, "set(");
  put(out, status, prefix);
  put_span(out, status, name);
  put(out, status, suffix);
  if (jb_vector_size(values) == 0) {
    put(out, status, " \"\")\n");
    return;
  }
  put(out, status, "\n");
  for (size_t i = 0; i < jb_vector_size(values); ++i) {
    const jb_json_span_t value =
        indices
            ? text_of(&feature_at(features, *(const size_t *)jb_vector_const_at(
                                                values, i))
                           ->name)
            : text_of(list_at(values, i));
    put(out, status, "    \"");
    put_span(out, status, value);
    put(out, status, "\"\n");
  }
  put(out, status, ")\n");
}

/* The resolution functions after the per-symbol data. */
static const char *const cmake_functions[] = {
    "function(jh_hal_resolve_features REQUESTED_OUT RESOLVED_OUT)\n",
    "    if(NOT REQUESTED_OUT OR NOT RESOLVED_OUT)\n",
    "        message(FATAL_ERROR\n",
    "            \"jh_hal_resolve_features requires requested and resolved "
    "output variables\")\n",
    "    endif()\n",
    "    set(_jh_requested \"\")\n",
    "    foreach(_jh_raw IN LISTS ARGN)\n",
    "        string(REGEX REPLACE \"^-D\" \"\" _jh_definition "
    "\"${_jh_raw}\")\n",
    "        if(_jh_definition MATCHES "
    "\"^HAL_(ENABLE|DISABLE)_[A-Z0-9_]+=0$\")\n",
    "            message(FATAL_ERROR\n",
    "                \"[JH-CFG-VALUE] ${_jh_raw} is unsupported; omit the "
    "symbol to disable it\")\n",
    "        elseif(_jh_definition MATCHES "
    "\"^HAL_(ENABLE|DISABLE)_[A-Z0-9_]+=1$\")\n",
    "            string(REGEX REPLACE \"=1$\" \"\" _jh_symbol "
    "\"${_jh_definition}\")\n",
    "        elseif(_jh_definition MATCHES "
    "\"^HAL_(ENABLE|DISABLE)_[A-Z0-9_]+$\")\n",
    "            set(_jh_symbol \"${_jh_definition}\")\n",
    "        else()\n",
    "            message(FATAL_ERROR\n",
    "                \"[JH-CFG-VALUE] invalid feature definition "
    "${_jh_raw}\")\n",
    "        endif()\n",
    "        list(FIND JH_HAL_FEATURE_SYMBOLS \"${_jh_symbol}\" _jh_known)\n",
    "        if(_jh_known EQUAL -1)\n",
    "            message(FATAL_ERROR\n",
    "                \"[JH-CFG-UNKNOWN] unknown feature ${_jh_symbol}\")\n",
    "        endif()\n",
    "        list(FIND JH_HAL_FEATURE_DERIVED_SYMBOLS\n",
    "            \"${_jh_symbol}\" _jh_derived)\n",
    "        if(NOT _jh_derived EQUAL -1)\n",
    "            message(FATAL_ERROR\n",
    "                \"[JH-CFG-DERIVED] ${_jh_symbol} cannot be requested "
    "directly\")\n",
    "        endif()\n",
    "        list(APPEND _jh_requested \"${_jh_symbol}\")\n",
    "    endforeach()\n",
    "    list(REMOVE_DUPLICATES _jh_requested)\n",
    "    list(SORT _jh_requested)\n",
    "\n",
    "    set(_jh_resolved ${_jh_requested})\n",
    "    set(_jh_pending ${_jh_requested})\n",
    "    while(_jh_pending)\n",
    "        list(POP_FRONT _jh_pending _jh_current)\n",
    "        set(_jh_implies_var \"JH_HAL_FEATURE_${_jh_current}_IMPLIES\")\n",
    "        foreach(_jh_dependency IN LISTS ${_jh_implies_var})\n",
    "            list(FIND _jh_resolved\n",
    "                \"${_jh_dependency}\" _jh_dependency_index)\n",
    "            if(_jh_dependency_index EQUAL -1)\n",
    "                list(APPEND _jh_resolved \"${_jh_dependency}\")\n",
    "                list(APPEND _jh_pending \"${_jh_dependency}\")\n",
    "            endif()\n",
    "        endforeach()\n",
    "    endwhile()\n",
    "    list(SORT _jh_resolved)\n",
    "\n",
    "    foreach(_jh_feature IN LISTS _jh_resolved)\n",
    "        set(_jh_requires_var "
    "\"JH_HAL_FEATURE_${_jh_feature}_REQUIRES\")\n",
    "        foreach(_jh_required IN LISTS ${_jh_requires_var})\n",
    "            list(FIND _jh_resolved \"${_jh_required}\" "
    "_jh_required_index)\n",
    "            if(_jh_required_index EQUAL -1)\n",
    "                message(FATAL_ERROR\n",
    "                    \"[JH-CFG-REQUIRES] ${_jh_feature} requires "
    "${_jh_required}\")\n",
    "            endif()\n",
    "        endforeach()\n",
    "        set(_jh_conflicts_var "
    "\"JH_HAL_FEATURE_${_jh_feature}_CONFLICTS\")\n",
    "        foreach(_jh_conflict IN LISTS ${_jh_conflicts_var})\n",
    "            list(FIND _jh_resolved \"${_jh_conflict}\" "
    "_jh_conflict_index)\n",
    "            if(NOT _jh_conflict_index EQUAL -1)\n",
    "                message(FATAL_ERROR\n",
    "                    \"[JH-CFG-CONFLICT] ${_jh_feature} conflicts with "
    "${_jh_conflict}\")\n",
    "            endif()\n",
    "        endforeach()\n",
    "    endforeach()\n",
    "\n",
    "    set(${REQUESTED_OUT} \"${_jh_requested}\" PARENT_SCOPE)\n",
    "    set(${RESOLVED_OUT} \"${_jh_resolved}\" PARENT_SCOPE)\n",
    "endfunction()\n",
    "\n",
    "function(jh_hal_resolve_build_effects\n",
    "        FEATURE_SOURCES_OUT PORTABLE_SOURCES_OUT DEPENDENCIES_OUT)\n",
    "    set(_jh_feature_sources \"\")\n",
    "    set(_jh_portable_sources \"\")\n",
    "    set(_jh_dependencies \"\")\n",
    "    foreach(_jh_feature IN LISTS ARGN)\n",
    "        list(FIND JH_HAL_FEATURE_SYMBOLS \"${_jh_feature}\" _jh_known)\n",
    "        if(_jh_known EQUAL -1)\n",
    "            message(FATAL_ERROR\n",
    "                \"[JH-CFG-UNKNOWN] unknown resolved feature "
    "${_jh_feature}\")\n",
    "        endif()\n",
    "        set(_jh_feature_sources_var\n",
    "            "
    "\"JH_HAL_FEATURE_${_jh_feature}_BUILD_EFFECT_FEATURE_SOURCES\")\n",
    "        set(_jh_portable_sources_var\n",
    "            "
    "\"JH_HAL_FEATURE_${_jh_feature}_BUILD_EFFECT_PORTABLE_SOURCES\")\n",
    "        set(_jh_dependencies_var\n",
    "            "
    "\"JH_HAL_FEATURE_${_jh_feature}_BUILD_EFFECT_DEPENDENCIES\")\n",
    "        list(APPEND _jh_feature_sources ${${_jh_feature_sources_var}})\n",
    "        list(APPEND _jh_portable_sources "
    "${${_jh_portable_sources_var}})\n",
    "        list(APPEND _jh_dependencies ${${_jh_dependencies_var}})\n",
    "    endforeach()\n",
    "    list(REMOVE_DUPLICATES _jh_feature_sources)\n",
    "    list(REMOVE_DUPLICATES _jh_portable_sources)\n",
    "    list(REMOVE_DUPLICATES _jh_dependencies)\n",
    "    list(SORT _jh_feature_sources)\n",
    "    list(SORT _jh_portable_sources)\n",
    "    list(SORT _jh_dependencies)\n",
    "    set(${FEATURE_SOURCES_OUT} \"${_jh_feature_sources}\" PARENT_SCOPE)\n",
    "    set(${PORTABLE_SOURCES_OUT} \"${_jh_portable_sources}\" "
    "PARENT_SCOPE)\n",
    "    set(${DEPENDENCIES_OUT} \"${_jh_dependencies}\" PARENT_SCOPE)\n",
    "endfunction()\n",
};

hal_status_t jh_feature_registry_cmake(const jh_feature_registry_t *registry,
                                       jb_string_t *out) {
  if (registry == NULL || out == NULL)
    return HAL_EINVAL;
  const jb_vector_t *features = &registry->features;
  const size_t count = jb_vector_size(features);
  jb_vector_t names = {0}, derived = {0}, implied = {0};
  hal_status_t status = list_init(&names);
  if (status == HAL_OK)
    status = list_init(&derived);
  if (status == HAL_OK)
    status = jb_vector_init(&implied, sizeof(size_t), heap());
  for (size_t i = 0; status == HAL_OK && i < count; ++i) {
    const jh_feature_t *feature = feature_at(features, i);
    status = list_push(&names, text_of(&feature->name));
    if (status == HAL_OK && feature->derived)
      status = list_push(&derived, text_of(&feature->name));
  }
  jb_string_clear(out);
  put(out, &status,
      "# Generated by jh generate features; do not edit.\n"
      "include_guard(GLOBAL)\n\n"
      "set(JH_HAL_FEATURE_SCHEMA_VERSION ");
  put_number(out, &status, JH_FEATURE_SCHEMA_VERSION);
  put(out, &status, ")\nset(JH_HAL_FEATURE_GENERATOR_VERSION ");
  put_number(out, &status, JH_FEATURE_GENERATOR_VERSION);
  put(out, &status, ")\nset(JH_HAL_FEATURE_REGISTRY_DIGEST \"");
  put(out, &status, registry->digest);
  put(out, &status, "\")\nset(JH_HAL_FEATURE_SCHEMA_DIGEST \"");
  put(out, &status, registry->schema_digest);
  put(out, &status, "\")\n\n");
  put_set(out, &status, "JH_HAL_FEATURE_SYMBOLS", jh_span(""), "", &names,
          features, false);
  put(out, &status, "\n");
  put_set(out, &status, "JH_HAL_FEATURE_DERIVED_SYMBOLS", jh_span(""), "",
          &derived, features, false);
  put(out, &status, "\n");
  for (size_t i = 0; status == HAL_OK && i < count; ++i) {
    const jh_feature_t *feature = feature_at(features, i);
    const jb_json_span_t name = text_of(&feature->name);
    status = closure(features, i, &implied);
    put(out, &status, "set(JH_HAL_FEATURE_");
    put_span(out, &status, name);
    put(out, &status, "_DOMAIN \"");
    put_span(out, &status, text_of(&feature->domain));
    put(out, &status, "\")\nset(JH_HAL_FEATURE_");
    put_span(out, &status, name);
    put(out, &status, "_KIND \"");
    put(out, &status, feature->derived ? "derived" : "feature");
    put(out, &status, "\")\n");
    put_set(out, &status, "JH_HAL_FEATURE_", name, "_IMPLIES",
            &feature->implies, features, false);
    put_set(out, &status, "JH_HAL_FEATURE_", name, "_TRANSITIVE_IMPLIES",
            &implied, features, true);
    put_set(out, &status, "JH_HAL_FEATURE_", name, "_REQUIRES",
            &feature->requirements, features, false);
    put_set(out, &status, "JH_HAL_FEATURE_", name, "_CONFLICTS",
            &feature->conflicts, features, false);
    if (jb_vector_size(&feature->feature_sources) != 0)
      put_set(out, &status, "JH_HAL_FEATURE_", name,
              "_BUILD_EFFECT_FEATURE_SOURCES", &feature->feature_sources,
              features, false);
    if (jb_vector_size(&feature->portable_sources) != 0)
      put_set(out, &status, "JH_HAL_FEATURE_", name,
              "_BUILD_EFFECT_PORTABLE_SOURCES", &feature->portable_sources,
              features, false);
    if (jb_vector_size(&feature->dependencies) != 0)
      put_set(out, &status, "JH_HAL_FEATURE_", name,
              "_BUILD_EFFECT_DEPENDENCIES", &feature->dependencies, features,
              false);
    put(out, &status, "\n");
  }
  for (size_t i = 0; i < COUNTOF(cmake_functions); ++i)
    put(out, &status, cmake_functions[i]);
  jb_vector_destroy(&implied);
  list_destroy(&derived);
  list_destroy(&names);
  return status;
}

void jh_feature_outputs_destroy(jh_feature_outputs_t *outputs) {
  static const jh_feature_outputs_t empty = {{0}, {0}};
  jb_string_destroy(&outputs->header);
  jb_string_destroy(&outputs->cmake);
  *outputs = empty;
}

/* One generatedArtifacts entry of artifacts.json. */
static hal_status_t artifact_path(const jh_json_document_t *artifacts,
                                  const char *key, jb_string_t *out,
                                  jh_failure_t *failure) {
  jb_json_node_t root = {0}, generated = {0}, entry = {0};
  jb_json_span_t text = {NULL, 0};
  jb_json_type_t type = JB_JSON_NULL;
  bool found =
      jb_json_root(&artifacts->doc, &root) == HAL_OK &&
      jb_json_find(root, jh_span("generatedArtifacts"), &generated) == HAL_OK &&
      jb_json_find(generated, jh_span(key), &entry) == HAL_OK &&
      jb_json_type(entry, &type) == HAL_OK && type == JB_JSON_STRING;
  if (found) {
    size_t needed = 0;
    hal_status_t status = jb_json_string_copy(entry, NULL, 0, &needed);
    if (status == HAL_OK)
      status = jb_string_resize(out, needed, '\0');
    if (status == HAL_OK && needed != 0)
      status = jb_json_string_copy(entry, jb_string_mutable_data(out), needed,
                                   &needed);
    if (status != HAL_OK)
      return storage(failure, status);
    text = text_of(out);
    found = text.length != 0 && memchr(text.data, '\0', text.length) == NULL;
  }
  if (!found) {
    const jb_json_span_t parts[] = {
        jh_span("config/tooling/artifacts.json lacks generatedArtifacts."),
        jh_span(key)};
    return fail_parts(failure, parts, COUNTOF(parts));
  }
  // Joined to the output root before anything is written: never outside it.
  if (jh_generated_path_relative(text))
    return HAL_OK;
  const jb_json_span_t parts[] = {
      jh_span("config/tooling/artifacts.json: generatedArtifacts."),
      jh_span(key), jh_span(" must be repository-relative")};
  return fail_parts(failure, parts, COUNTOF(parts));
}

hal_status_t jh_feature_outputs_read(const char *root,
                                     jh_feature_outputs_t *outputs,
                                     jh_failure_t *failure) {
  if (root == NULL || outputs == NULL)
    return jh_fail_arguments(failure);
  jb_string_t path = {0};
  jh_json_document_t artifacts = {0};
  jh_json_location_t where;
  jh_failure_t reason;
  jh_json_location_init(&where);
  jh_failure_init(&reason);
  hal_status_t status = storage(failure, jb_string_init(&path, heap()));
  if (status == HAL_OK)
    status = storage(failure, jb_string_init(&outputs->header, heap()));
  if (status == HAL_OK)
    status = storage(failure, jb_string_init(&outputs->cmake, heap()));
  if (status == HAL_OK)
    status =
        jh_absolute_path("config/tooling/artifacts.json", root, &path, failure);
  if (status == HAL_OK &&
      jh_json_read_document(&where, jb_string_data(&path), 1024 * 1024, false,
                            &artifacts, &reason) != HAL_OK)
    status = fail_json(failure, text_of(&path), &where, &reason);
  if (status == HAL_OK)
    status =
        artifact_path(&artifacts, "featureHeader", &outputs->header, failure);
  if (status == HAL_OK)
    status =
        artifact_path(&artifacts, "featureCmake", &outputs->cmake, failure);
  if (status != HAL_OK)
    jh_feature_outputs_destroy(outputs);
  jh_json_document_destroy(&artifacts);
  jh_failure_destroy(&reason);
  jh_json_location_destroy(&where);
  jb_string_destroy(&path);
  return status;
}

/* A failed write or read of an artifact, as generate_hal_features.py
 * reports it: "{path}: cannot write/read generated output: {status}". */
static hal_status_t fail_output(jh_failure_t *failure, jb_json_span_t path,
                                const char *action) {
  if (jb_string_size(&failure->message) == 0)
    return failure->status;
  const hal_status_t status = failure->status;
  const jb_json_span_t parts[] = {path, jh_span(action),
                                  jh_span(hal_status_to_string(status))};
  (void)jh_fail(failure, status, 2, parts, COUNTOF(parts));
  return status;
}

hal_status_t jh_feature_registry_publish(const jh_feature_registry_t *registry,
                                         const jh_feature_outputs_t *outputs,
                                         const char *output_root, bool check,
                                         size_t *changed, bool *current,
                                         jh_failure_t *failure) {
  if (registry == NULL || outputs == NULL || output_root == NULL ||
      changed == NULL || current == NULL)
    return jh_fail_arguments(failure);
  *changed = 0;
  *current = true;
  const jb_string_t *relative[] = {&outputs->header, &outputs->cmake};
  jb_string_t content = {0}, path = {0}, label = {0};
  hal_status_t status = storage(failure, jb_string_init(&content, heap()));
  if (status == HAL_OK)
    status = storage(failure, jb_string_init(&path, heap()));
  if (status == HAL_OK)
    status = storage(failure, jb_string_init(&label, heap()));
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(relative); ++i) {
    status = storage(failure,
                     i == 0 ? jh_feature_registry_header(registry, &content)
                            : jh_feature_registry_cmake(registry, &content));
    if (status == HAL_OK)
      status = jh_absolute_path(jb_string_data(relative[i]), output_root, &path,
                                failure);
    if (status != HAL_OK)
      break;
    if (!check) {
      bool file_changed = false;
      status = jh_write_generated_mode(jb_string_data(&path), text_of(&content),
                                       0644, &file_changed, failure);
      if (status != HAL_OK)
        status = fail_output(failure, text_of(&path),
                             ": cannot write generated output: ");
      *changed += file_changed ? 1u : 0u;
      continue;
    }
    jb_string_clear(&label);
    status = storage(failure, append(&label, "generated:"));
    if (status == HAL_OK)
      status = storage(failure, append_span(&label, text_of(relative[i])));
    bool file_current = true;
    if (status == HAL_OK)
      status = jh_check_generated(jb_string_data(&path), text_of(&content), 80,
                                  text_of(&label), &file_current, failure);
    if (status != HAL_OK)
      status = fail_output(failure, text_of(&path),
                           ": cannot read generated output: ");
    *current = *current && file_current;
    *changed += file_current ? 0u : 1u;
  }
  jb_string_destroy(&label);
  jb_string_destroy(&path);
  jb_string_destroy(&content);
  return status;
}
