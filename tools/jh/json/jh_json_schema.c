#include "jh_json_schema.h"
#include "jh_failure.h"
#include "jh_python_repr.h"
#include <jb/json_number.h>
#include <jb/json_pointer.h>
#include <jb/utf8.h>
#include <string.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

typedef jb_json_node_t node;
typedef jb_json_span_t text;
typedef struct {
  node n;
  bool key;
} value;
typedef struct {
  node schema;
  value instance;
} active;
typedef struct {
  unsigned char *data;
  size_t capacity, used;
} arena;
typedef struct {
  size_t count, bytes;
} mark;
typedef struct {
  const jh_json_schema_limits_t *limit;
  hal_status_t status;
  size_t remaining, depth, source_count, schema_nodes, report_count;
  jh_json_schema_source_t *sources;
  active *stack;
  jh_json_schema_error_t *reports;
  arena temp, report_text, regex_memory;
  jh_json_schema_load_fn load;
  void *load_user;
} engine;

static const node absent = {NULL, 0};
static const text empty = {"", 0};

/* Keep the first failure. Every recursive frame restores its arena position;
 * callers publish reports only after evaluation and ordering succeed. */
static bool checked(engine *e, hal_status_t status) {
  if (e->status == HAL_OK)
    e->status = status;
  return e->status == HAL_OK;
}
static bool require(engine *e, bool condition, hal_status_t status) {
  return checked(e, condition ? HAL_OK : status);
}
static size_t add(engine *e, size_t a, size_t b) {
  return require(e, b <= SIZE_MAX - a, HAL_EOVERFLOW) ? a + b : 0;
}
static size_t mul(engine *e, size_t a, size_t b) {
  return require(e, a == 0 || b <= SIZE_MAX / a, HAL_EOVERFLOW) ? a * b : 0;
}
static bool charge(engine *e, size_t n) {
  if (!require(e, n <= e->remaining, HAL_EOVERFLOW))
    return false;
  e->remaining -= n;
  return true;
}
static void *take(engine *e, arena *a, size_t n, size_t alignment) {
  if (e->status != HAL_OK)
    return NULL;
  const uintptr_t address = (uintptr_t)a->data + a->used;
  const size_t padding = (alignment - address % alignment) % alignment;
  const size_t end = add(e, add(e, a->used, padding), n);
  if (!require(e, end <= a->capacity, HAL_EOVERFLOW))
    return NULL;
  void *out = a->data + a->used + padding;
  a->used = end;
  return out;
}
static void *text_allocate(void *user, size_t bytes) {
  engine *e = user;
  return take(e, &e->temp, bytes, JB_ALIGNOF(jb_max_align_t));
}
static void arena_release(void *user, void *data) {
  (void)user;
  (void)data;
}
static void text_init(engine *e, jb_string_t *out) {
  const jb_allocator_t allocator = {e, text_allocate, arena_release};
  (void)checked(e, jb_string_init(out, &allocator));
}
static void append(engine *e, jb_string_t *out, text part) {
  if (e->status == HAL_OK)
    (void)checked(e, jh_append_span(out, part));
}
static void put(engine *e, jb_string_t *out, const char *part) {
  append(e, out, jh_span(part));
}
static void byte(engine *e, jb_string_t *out, char c) {
  const text part = {&c, 1};
  append(e, out, part);
}
/* The descriptor relinquishes its storage to the enclosing arena frame. */
static text text_finish(engine *e, jb_string_t *out) {
  const text result = e->status == HAL_OK ? jh_string_span(out) : empty;
  jb_string_destroy(out);
  return result;
}
static text concat(engine *e, text a, text b, text c) {
  jb_string_t out = {0};
  text_init(e, &out);
  const size_t size = add(e, add(e, a.length, b.length), c.length);
  if (e->status == HAL_OK)
    (void)checked(e, jb_string_reserve(&out, size));
  append(e, &out, a);
  append(e, &out, b);
  append(e, &out, c);
  return text_finish(e, &out);
}
static bool named(text a, const char *b) {
  return jh_span_equal(a, jh_span(b));
}
static bool exists(node n) {
  return n.doc != NULL && n.index != JB_JSON_NODE_NONE;
}
static jb_json_type_t kind(node n) {
  return exists(n) ? n.doc->tokens[n.index].type : (jb_json_type_t)JB_JSON_NULL;
}
static jb_json_type_t value_kind(value v) {
  return v.key ? (jb_json_type_t)JB_JSON_STRING : kind(v.n);
}
static value item(node n) {
  const value v = {n, false};
  return v;
}
static size_t count(node n) {
  return exists(n) ? n.doc->tokens[n.index].child_count : 0;
}
static node first(node n) {
  if (!exists(n))
    return absent;
  const node result = {n.doc, n.doc->tokens[n.index].first_child};
  return result;
}
static node next(node n) {
  if (!exists(n))
    return absent;
  const node result = {n.doc, n.doc->tokens[n.index].next_sibling};
  return result;
}
static bool boolean(engine *e, node n) {
  bool out = false;
  if (e->status == HAL_OK)
    (void)checked(e, jb_json_get_bool(n, &out));
  return out;
}
static text string_value(engine *e, value v) {
  if (e->status != HAL_OK)
    return empty;
  const jb_json_token_t *token = &v.n.doc->tokens[v.n.index];
  const size_t encoded = v.key ? token->key_length : token->length;
  if (!require(e, encoded <= add(e, mul(e, e->limit->max_value_bytes, 6), 2),
               HAL_EOVERFLOW) ||
      !charge(e, add(e, mul(e, encoded, 2), 1)))
    return empty;
  size_t n = 0;
  hal_status_t (*copy)(node, char *, size_t, size_t *) =
      v.key ? jb_json_key_copy : jb_json_string_copy;
  if (!checked(e, copy(v.n, NULL, 0, &n)) ||
      !require(e, n <= e->limit->max_value_bytes, HAL_EOVERFLOW) ||
      !charge(e, add(e, n, 1)))
    return empty;
  char *out = take(e, &e->temp, n, 1);
  if (e->status == HAL_OK)
    (void)checked(e, copy(v.n, out, n, &n));
  const text result = {out, n};
  return e->status == HAL_OK ? result : empty;
}
static text string(engine *e, node n) { return string_value(e, item(n)); }
static text key(engine *e, node n) {
  const value v = {n, true};
  return string_value(e, v);
}
static node find(engine *e, node object, text name) {
  if (!exists(object) ||
      !require(e, kind(object) == JB_JSON_OBJECT, HAL_EINVAL))
    return absent;
  for (node child = first(object); exists(child) && e->status == HAL_OK;
       child = next(child)) {
    const size_t saved = e->temp.used;
    (void)charge(e, 1);
    const bool match = jh_span_equal(key(e, child), name);
    e->temp.used = saved;
    if (match && e->status == HAL_OK)
      return child;
  }
  return absent;
}
static node field(engine *e, node object, const char *name) {
  return find(e, object, jh_span(name));
}
static text raw(engine *e, node n) {
  text out = empty;
  if (e->status == HAL_OK && checked(e, jb_json_raw(n, &out)) &&
      require(e, out.length <= e->limit->max_value_bytes, HAL_EOVERFLOW) &&
      charge(e, add(e, out.length, 1)))
    return out;
  return empty;
}
static text joined(engine *e, text at, text token) {
  jb_string_t out = {0};
  text_init(e, &out);
  append(e, &out, at);
  byte(e, &out, '/');
  for (size_t i = 0; i < token.length && charge(e, 1); ++i) {
    if (token.data[i] == '~')
      put(e, &out, "~0");
    else if (token.data[i] == '/')
      put(e, &out, "~1");
    else
      byte(e, &out, token.data[i]);
    (void)require(e, jb_string_size(&out) <= e->limit->max_pointer_bytes,
                  HAL_EOVERFLOW);
  }
  (void)require(e, jb_string_size(&out) <= e->limit->max_pointer_bytes,
                HAL_EOVERFLOW);
  return text_finish(e, &out);
}
static text decimal(engine *e, size_t n) {
  char buf[3 * sizeof(size_t) + 1];
  size_t i = COUNTOF(buf);
  do {
    buf[--i] = (char)('0' + n % 10);
    n /= 10;
  } while (n != 0);
  const text digits = {buf + i, COUNTOF(buf) - i};
  return concat(e, digits, empty, empty);
}
static size_t natural(engine *e, node n) {
  if (e->status != HAL_OK)
    return 0;
  const size_t length = n.doc->tokens[n.index].length;
  uint64_t out = 0;
  if (!require(e, length <= e->limit->max_value_bytes, HAL_EOVERFLOW) ||
      !charge(e, add(e, length, 1)) || !checked(e, jb_json_get_u64(n, &out)) ||
      !require(e, out <= SIZE_MAX, HAL_EOVERFLOW))
    return 0;
  return (size_t)out;
}
static int compare(engine *e, node a, node b) {
  const size_t saved = e->temp.used;
  int result = 0;
  if (e->status != HAL_OK)
    return result;
  const size_t al = a.doc->tokens[a.index].length,
               bl = b.doc->tokens[b.index].length;
  if (!require(
          e, al <= e->limit->max_value_bytes && bl <= e->limit->max_value_bytes,
          HAL_EOVERFLOW))
    return result;
  const size_t length = add(e, al, bl);
  if (!require(e, length <= mul(e, e->limit->max_value_bytes, 2),
               HAL_EOVERFLOW))
    return result;
  const size_t allowance = add(
      e, 256, add(e, mul(e, length, 64), mul(e, mul(e, length, length), 4)));
  size_t needed = 0;
  if (charge(e, allowance) &&
      checked(e, jb_json_number_compare_workspace_size(a, b, &needed))) {
    void *scratch = take(e, &e->temp, needed, JB_ALIGNOF(jb_max_align_t));
    const jb_json_workspace_t workspace = {scratch, needed};
    if (e->status == HAL_OK)
      (void)checked(
          e, jb_json_number_compare(a, b, workspace, allowance, &result));
  }
  e->temp.used = saved;
  return result;
}
static bool equal(engine *e, value a, value b, size_t nested) {
  const size_t saved = e->temp.used;
  bool result = false;
  if (!charge(e, 1) ||
      !require(e, nested < e->limit->max_ref_depth, HAL_EOVERFLOW) ||
      value_kind(a) != value_kind(b))
    goto done;
  switch (value_kind(a)) {
  case JB_JSON_NULL:
    result = true;
    goto done;
  case JB_JSON_BOOL: {
    const bool left = boolean(e, a.n);
    result = left == boolean(e, b.n);
    goto done;
  }
  case JB_JSON_NUMBER:
    result = compare(e, a.n, b.n) == 0;
    goto done;
  case JB_JSON_STRING: {
    const text left = string_value(e, a);
    result = jh_span_equal(left, string_value(e, b));
    goto done;
  }
  default:
    break;
  }
  if (count(a.n) != count(b.n))
    goto done;
  node right = first(b.n);
  result = true;
  for (node left = first(a.n); exists(left) && e->status == HAL_OK;
       left = next(left)) {
    const size_t member = e->temp.used;
    (void)charge(e, 1);
    if (value_kind(a) == JB_JSON_OBJECT)
      right = find(e, b.n, key(e, left));
    result = exists(right) && equal(e, item(left), item(right), nested + 1);
    e->temp.used = member;
    if (!result)
      break;
    if (value_kind(a) == JB_JSON_ARRAY)
      right = next(right);
  }
done:
  e->temp.used = saved;
  return e->status == HAL_OK && result;
}

typedef struct {
  engine *e;
  jb_string_t *out;
  size_t length;
} repr_sink;
static hal_status_t repr_write(void *user, text piece, size_t consumed) {
  repr_sink *sink = user;
  if (charge(sink->e, consumed)) {
    sink->length = add(sink->e, sink->length, piece.length);
    if (sink->out != NULL)
      append(sink->e, sink->out, piece);
  }
  return sink->e->status;
}
static void quoted_into(repr_sink *sink, text s) {
  engine *e = sink->e;
  if (e->status == HAL_OK)
    (void)checked(e, jh_python_repr(s, repr_write, sink));
}
static text quoted(engine *e, text s) {
  jb_string_t out = {0};
  text_init(e, &out);
  repr_sink sink = {e, &out, 0};
  quoted_into(&sink, s);
  return text_finish(e, &out);
}
static void repr_into(repr_sink *sink, node n, size_t nested) {
  engine *e = sink->e;
  if (!charge(e, 1) ||
      !require(e, nested < e->limit->max_ref_depth, HAL_EOVERFLOW))
    return;
  switch (kind(n)) {
  case JB_JSON_NULL:
    (void)repr_write(sink, jh_span("None"), 0);
    break;
  case JB_JSON_BOOL:
    (void)repr_write(sink, jh_span(boolean(e, n) ? "True" : "False"), 0);
    break;
  case JB_JSON_STRING:
    quoted_into(sink, string(e, n));
    break;
  case JB_JSON_NUMBER:
    (void)repr_write(sink, raw(e, n), 0);
    break;
  default: {
    const bool object = kind(n) == JB_JSON_OBJECT;
    (void)repr_write(sink, jh_span(object ? "{" : "["), 0);
    bool separator = false;
    for (node child = first(n); exists(child) && charge(e, 1);
         child = next(child)) {
      if (separator)
        (void)repr_write(sink, jh_span(", "), 0);
      separator = true;
      if (object) {
        quoted_into(sink, key(e, child));
        (void)repr_write(sink, jh_span(": "), 0);
      }
      repr_into(sink, child, nested + 1);
      (void)require(e, sink->length <= e->limit->max_report_bytes,
                    HAL_EOVERFLOW);
    }
    (void)repr_write(sink, jh_span(object ? "}" : "]"), 0);
    break;
  }
  }
}
static text repr(engine *e, node n, const char *lead) {
  /* Arena strings cannot reclaim abandoned growth blocks. Measure with the
   * same finite budget, then reserve once and charge logical work once. */
  const size_t saved = e->temp.used, remaining = e->remaining;
  repr_sink sink = {e, NULL, 0};
  (void)repr_write(&sink, jh_span(lead), 0);
  repr_into(&sink, n, 0);
  e->temp.used = saved;
  if (e->status != HAL_OK)
    return empty;
  e->remaining = remaining;
  jb_string_t out = {0};
  text_init(e, &out);
  (void)checked(e, jb_string_reserve(&out, sink.length));
  sink.out = &out;
  sink.length = 0;
  (void)repr_write(&sink, jh_span(lead), 0);
  repr_into(&sink, n, 0);
  return text_finish(e, &out);
}

static void *regex_alloc(PCRE2_SIZE n, void *user) {
  engine *e = user;
  if (!charge(e, n))
    return NULL;
  return take(e, &e->regex_memory, n, JB_ALIGNOF(jb_max_align_t));
}
static void regex_free(void *data, void *user) { arena_release(user, data); }
typedef struct {
  engine *owner;
  size_t step;
} regex_budget;
static int callout(pcre2_callout_block *block, void *user) {
  (void)block;
  regex_budget *budget = user;
  engine *e = budget->owner;
  /* Each unanchored restart may scan the remaining subject. */
  if (e->remaining < budget->step)
    return PCRE2_ERROR_CALLOUT;
  e->remaining -= budget->step;
  return 0;
}
static int compile_guard(uint32_t depth, void *user) {
  const engine *e = user;
  return depth > e->limit->max_ref_depth ? 1 : 0;
}
static text pattern(engine *e, text original) {
  jb_string_t out = {0};
  text_init(e, &out);
  if (!require(e, original.length <= e->limit->max_pattern_bytes,
               HAL_EOVERFLOW))
    goto done;
  bool in_class = false;
  static const char space[] =
      "\\u0009-\\u000d\\u001c-\\u0020\\u0085\\u00a0\\u1680\\u2000-"
      "\\u200a\\u2028-\\u2029\\u202f\\u205f\\u3000";
  for (size_t i = 0; i < original.length && charge(e, 1); ++i) {
    char c = original.data[i];
    if (c == '\\' && i + 1 < original.length) {
      c = original.data[++i];
      if (c == 's') {
        if (!in_class)
          byte(e, &out, '[');
        put(e, &out, space);
        if (!in_class)
          byte(e, &out, ']');
      } else if (c == 'v')
        put(e, &out, "\\u000b");
      else {
        const bool alphabetic = jh_ascii_is_lower(c) || jh_ascii_is_upper(c);
        if (!require(e,
                     !alphabetic || c == 'x' || c == 'u' || c == 'n' ||
                         c == 'r' || c == 't' || c == 'f' || c == 'v' ||
                         c == 'a',
                     HAL_EUNSUPPORTED) ||
            !require(e, !jh_ascii_is_digit(c), HAL_EUNSUPPORTED))
          goto done;
        if (c == 'x' || c == 'u') {
          const size_t digits = c == 'x' ? 2 : 4;
          if (!require(e, original.length - i > digits, HAL_EUNSUPPORTED))
            goto done;
          for (size_t j = 1; j <= digits; ++j)
            if (!require(e, jh_hex_digit(original.data[i + j]) >= 0,
                         HAL_EUNSUPPORTED))
              goto done;
        }
        byte(e, &out, '\\');
        byte(e, &out, c);
      }
    } else {
      if (c == '[') {
        if (in_class) {
          if (!require(e,
                       i + 1 == original.length || original.data[i + 1] != ':',
                       HAL_EUNSUPPORTED))
            goto done;
        } else {
          size_t first_byte = i + 1;
          if (first_byte < original.length && original.data[first_byte] == '^')
            ++first_byte;
          if (!require(e,
                       first_byte == original.length ||
                           original.data[first_byte] != ']',
                       HAL_EUNSUPPORTED))
            goto done;
          in_class = true;
        }
      }
      if (c == ']')
        in_class = false;
      if (c == '(' && i + 1 < original.length) {
        if (!require(e, original.data[i + 1] != '*', HAL_EUNSUPPORTED))
          goto done;
        if (original.data[i + 1] == '?' &&
            !require(e,
                     i + 2 < original.length && (original.data[i + 2] == ':' ||
                                                 original.data[i + 2] == '!' ||
                                                 original.data[i + 2] == '='),
                     HAL_EUNSUPPORTED))
          goto done;
      }
      if (c == '+' && i != 0 &&
          !require(e,
                   original.data[i - 1] != '+' && original.data[i - 1] != '*' &&
                       original.data[i - 1] != '?' &&
                       original.data[i - 1] != '}',
                   HAL_EUNSUPPORTED))
        goto done;
      byte(e, &out, c);
    }
  }
done:
  return text_finish(e, &out);
}
static bool search(engine *e, text original, text subject) {
  const size_t saved = e->temp.used, regex_saved = e->regex_memory.used;
  bool matched = false;
  pcre2_general_context *general = NULL;
  pcre2_compile_context *compile = NULL;
  pcre2_code *code = NULL;
  pcre2_match_context *match = NULL;
  pcre2_match_data *data = NULL;
  if (!require(e, subject.length <= e->limit->max_value_bytes, HAL_EOVERFLOW))
    goto done;
  const text adapted = pattern(e, original);
  if (!charge(
          e, add(e, 64,
                 mul(e, add(e, adapted.length, 1), add(e, adapted.length, 1)))))
    goto done;
  general = pcre2_general_context_create(regex_alloc, regex_free, e);
  if (!require(e, general != NULL, HAL_EOVERFLOW))
    goto done;
  compile = pcre2_compile_context_create(general);
  if (!require(e, compile != NULL, HAL_EOVERFLOW) ||
      !require(e, pcre2_set_newline(compile, PCRE2_NEWLINE_LF) == 0,
               HAL_EINTERNAL))
    goto done;
  (void)pcre2_set_compile_recursion_guard(compile, compile_guard, e);
  (void)pcre2_set_max_pattern_length(compile, adapted.length);
  (void)pcre2_set_max_pattern_compiled_length(compile,
                                              e->limit->max_regex_bytes);
  int error = 0;
  PCRE2_SIZE offset = 0;
  code = pcre2_compile((PCRE2_SPTR)adapted.data, adapted.length,
                       PCRE2_UTF | PCRE2_UCP | PCRE2_ALT_BSUX |
                           PCRE2_AUTO_CALLOUT | PCRE2_NO_AUTO_POSSESS |
                           PCRE2_NO_START_OPTIMIZE,
                       &error, &offset, compile);
  if (code == NULL) {
    if (require(e,
                error != PCRE2_ERROR_HEAP_FAILED &&
                    error != PCRE2_ERROR_PARENTHESES_NEST_TOO_DEEP &&
                    error != PCRE2_ERROR_PARENTHESES_STACK_CHECK &&
                    error != PCRE2_ERROR_PATTERN_TOO_LARGE &&
                    error != PCRE2_ERROR_PATTERN_COMPILED_SIZE_TOO_BIG &&
                    error != PCRE2_ERROR_PATTERN_STRING_TOO_LONG,
                HAL_EOVERFLOW))
      (void)checked(e, HAL_EUNSUPPORTED);
    goto done;
  }
  match = pcre2_match_context_create(general);
  data = pcre2_match_data_create_from_pattern(code, general);
  if (!require(e, match != NULL && data != NULL, HAL_EOVERFLOW))
    goto done;
  (void)pcre2_set_match_limit(match, e->limit->regex_match_limit);
  (void)pcre2_set_depth_limit(match, e->limit->regex_depth_limit);
  (void)pcre2_set_heap_limit(match, e->limit->regex_heap_limit_kib);
  regex_budget budget = {e, add(e, add(e, subject.length, adapted.length), 1)};
  (void)pcre2_set_callout(match, callout, &budget);
  if (!charge(e, add(e, subject.length, 1)))
    goto done;
  const int result = pcre2_match(code, (PCRE2_SPTR)subject.data, subject.length,
                                 0, 0, data, match);
  if (result >= 0)
    matched = true;
  else if (result != PCRE2_ERROR_NOMATCH) {
    const bool limited =
        result == PCRE2_ERROR_MATCHLIMIT || result == PCRE2_ERROR_DEPTHLIMIT ||
        result == PCRE2_ERROR_HEAPLIMIT || result == PCRE2_ERROR_NOMEMORY ||
        result == PCRE2_ERROR_CALLOUT;
    (void)checked(e, limited ? HAL_EOVERFLOW : HAL_EUNSUPPORTED);
  }
done:
  pcre2_match_data_free(data);
  pcre2_match_context_free(match);
  pcre2_code_free(code);
  pcre2_compile_context_free(compile);
  pcre2_general_context_free(general);
  e->regex_memory.used = regex_saved;
  e->temp.used = saved;
  return e->status == HAL_OK && matched;
}
static const char *const type_names[] = {
    "null", "boolean", "number", "integer", "string", "array", "object"};
static bool type(engine *e, value v, text name) {
  const jb_json_type_t k = value_kind(v);
  if (named(name, "null"))
    return k == JB_JSON_NULL;
  if (named(name, "boolean"))
    return k == JB_JSON_BOOL;
  if (named(name, "string"))
    return k == JB_JSON_STRING;
  if (named(name, "object"))
    return k == JB_JSON_OBJECT;
  if (named(name, "array"))
    return k == JB_JSON_ARRAY;
  if (named(name, "number"))
    return k == JB_JSON_NUMBER;
  if (!require(e, named(name, "integer"), HAL_EUNSUPPORTED) ||
      k != JB_JSON_NUMBER)
    return false;
  bool integral = false;
  if (charge(e, add(e, v.n.doc->tokens[v.n.index].length, 1)))
    (void)checked(e, jb_json_number_is_integer(v.n, &integral));
  return e->status == HAL_OK && integral;
}
static bool listed(text name, const char *const *names, size_t length) {
  for (size_t i = 0; i < length; ++i)
    if (named(name, names[i]))
      return true;
  return false;
}
static void validate_type(engine *e, node t) {
  if (!require(e, kind(t) == JB_JSON_STRING, HAL_EINVAL))
    return;
  const text name = string(e, t);
  (void)require(e, listed(name, type_names, COUNTOF(type_names)),
                HAL_EUNSUPPORTED);
}
static void check_schema(engine *e, node s, size_t nested) {
  const size_t saved = e->temp.used;
  if (!charge(e, 1) ||
      !require(e, nested < e->limit->max_ref_depth, HAL_EOVERFLOW))
    goto done;
  if (kind(s) == JB_JSON_BOOL)
    goto done;
  if (!require(e, kind(s) == JB_JSON_OBJECT, HAL_EINVAL))
    goto done;
  static const char *const supported[] = {"$schema",
                                          "$defs",
                                          "$ref",
                                          "title",
                                          "description",
                                          "type",
                                          "properties",
                                          "required",
                                          "additionalProperties",
                                          "propertyNames",
                                          "enum",
                                          "const",
                                          "pattern",
                                          "minimum",
                                          "maximum",
                                          "minLength",
                                          "items",
                                          "minItems",
                                          "uniqueItems",
                                          "minProperties",
                                          "maxProperties",
                                          "allOf",
                                          "oneOf",
                                          "not",
                                          "if",
                                          "then"};
  for (node r = first(s); exists(r) && e->status == HAL_OK; r = next(r)) {
    const size_t member = e->temp.used;
    const text name = key(e, r);
    if (!charge(e, COUNTOF(supported)) ||
        !require(e, listed(name, supported, COUNTOF(supported)),
                 HAL_EUNSUPPORTED))
      goto done;
    if (named(name, "properties") || named(name, "$defs")) {
      if (!require(e, kind(r) == JB_JSON_OBJECT, HAL_EINVAL))
        goto done;
      for (node c = first(r); exists(c) && e->status == HAL_OK; c = next(c))
        check_schema(e, c, nested + 1);
    } else if (named(name, "allOf") || named(name, "oneOf")) {
      if (!require(e, kind(r) == JB_JSON_ARRAY, HAL_EINVAL))
        goto done;
      for (node c = first(r); exists(c) && e->status == HAL_OK; c = next(c))
        check_schema(e, c, nested + 1);
    } else if (named(name, "items") || named(name, "additionalProperties") ||
               named(name, "propertyNames") || named(name, "not") ||
               named(name, "if") || named(name, "then"))
      check_schema(e, r, nested + 1);
    else if (named(name, "type")) {
      if (kind(r) == JB_JSON_ARRAY) {
        if (!require(e, count(r) != 0, HAL_EINVAL))
          goto done;
        for (node c = first(r); exists(c) && e->status == HAL_OK; c = next(c))
          validate_type(e, c);
      } else
        validate_type(e, r);
    } else if (named(name, "required")) {
      if (!require(e, kind(r) == JB_JSON_ARRAY, HAL_EINVAL))
        goto done;
      for (node c = first(r); exists(c) && e->status == HAL_OK; c = next(c)) {
        if (!require(e, kind(c) == JB_JSON_STRING, HAL_EINVAL))
          goto done;
        (void)string(e, c);
      }
    } else if (named(name, "enum"))
      (void)require(e, kind(r) == JB_JSON_ARRAY, HAL_EINVAL);
    else if (named(name, "uniqueItems"))
      (void)require(e, kind(r) == JB_JSON_BOOL, HAL_EINVAL);
    else if (named(name, "minimum") || named(name, "maximum"))
      (void)require(e, kind(r) == JB_JSON_NUMBER, HAL_EINVAL);
    else if (named(name, "minLength") || named(name, "minItems") ||
             named(name, "minProperties") || named(name, "maxProperties"))
      (void)natural(e, r);
    else if (named(name, "pattern")) {
      if (!require(e, kind(r) == JB_JSON_STRING, HAL_EINVAL))
        goto done;
      (void)search(e, string(e, r), empty);
    } else if (named(name, "$ref") || named(name, "$schema") ||
               named(name, "title") || named(name, "description")) {
      if (!require(e, kind(r) == JB_JSON_STRING, HAL_EINVAL))
        goto done;
      (void)string(e, r);
    }
    e->temp.used = member;
  }
done:
  e->temp.used = saved;
}

static hal_status_t callback_status(hal_status_t status) {
  return status == HAL_OK || (status >= HAL_ETYPE && status <= HAL_EINVAL)
             ? status
             : HAL_EINTERNAL;
}
static size_t register_source(engine *e, jh_json_schema_source_t s) {
  if (!require(e, s.doc != NULL && s.doc->valid && s.doc->token_count != 0,
               HAL_EINVAL) ||
      !require(e, s.uri.data != NULL && s.uri.length != 0, HAL_EINVAL) ||
      !require(e, s.uri.length <= e->limit->max_uri_bytes, HAL_EOVERFLOW) ||
      !charge(e, add(e, s.uri.length, 1)) ||
      !checked(e, jb_utf8_validate(s.uri.data, s.uri.length, NULL)) ||
      !require(e,
               memchr(s.uri.data, '#', s.uri.length) == NULL &&
                   memchr(s.uri.data, '\0', s.uri.length) == NULL,
               HAL_EINVAL))
    return 0;
  for (size_t i = 0; i < e->source_count; ++i) {
    if (!charge(e, add(e, s.uri.length, 1)))
      return 0;
    if (jh_span_equal(e->sources[i].uri, s.uri)) {
      (void)require(e, e->sources[i].doc == s.doc, HAL_EINVAL);
      return i;
    }
    if (!require(e, e->sources[i].doc != s.doc, HAL_EINVAL))
      return 0;
  }
  if (!require(
          e, s.doc->token_count <= e->limit->max_schema_nodes - e->schema_nodes,
          HAL_EOVERFLOW))
    return 0;
  e->schema_nodes += s.doc->token_count;
  const size_t index = e->source_count++;
  e->sources[index] = s;
  const node root = {s.doc, 0};
  check_schema(e, root, 0);
  return index;
}
static text retain(engine *e, text s, text lead) {
  const size_t size = add(e, lead.length, s.length);
  if (!charge(e, add(e, size, 1)))
    return empty;
  char *out = take(e, &e->report_text, size, 1);
  if (e->status != HAL_OK)
    return empty;
  if (lead.length != 0)
    memcpy(out, lead.data, lead.length);
  if (s.length != 0)
    memcpy(out + lead.length, s.data, s.length);
  const text result = {out, size};
  return result;
}
static void report(engine *e, node rule, text at, const char *reason,
                   text message, bool name_error) {
  const size_t saved = e->temp.used;
  size_t size = 0;
  if (!require(e, e->report_count < e->limit->max_reports, HAL_EOVERFLOW) ||
      !charge(e, add(e, rule.doc->token_count, rule.doc->input.length)) ||
      !checked(e, jb_json_pointer_copy(rule, NULL, 0, &size)) ||
      !require(e, size <= e->limit->max_pointer_bytes, HAL_EOVERFLOW))
    goto done;
  char *pointer = take(e, &e->temp, size, 1);
  if (e->status != HAL_OK ||
      !checked(e, jb_json_pointer_copy(rule, pointer, size, &size)))
    goto done;
  text uri = empty;
  for (size_t i = 0; i < e->source_count; ++i)
    if (e->sources[i].doc == rule.doc) {
      uri = e->sources[i].uri;
      break;
    }
  if (!require(e, uri.length != 0, HAL_EINTERNAL))
    goto done;
  const text path = {pointer, size};
  jh_json_schema_error_t result = {0};
  result.instance_pointer = retain(e, at, empty);
  result.schema_uri = retain(e, uri, empty);
  result.schema_pointer = retain(e, path, empty);
  result.schema_rule = rule;
  result.reason_code = retain(e, jh_span(reason), empty);
  result.message =
      retain(e, message, name_error ? jh_span("field name: ") : empty);
  if (e->status == HAL_OK)
    e->reports[e->report_count++] = result;
done:
  e->temp.used = saved;
}
static mark checkpoint(const engine *e) {
  const mark m = {e->report_count, e->report_text.used};
  return m;
}
static void restore(engine *e, mark m) {
  e->report_count = m.count;
  e->report_text.used = m.bytes;
}
static size_t text_find(text s, char c) {
  const char *found = s.length == 0 ? NULL : memchr(s.data, c, s.length);
  return found == NULL ? s.length : (size_t)(found - s.data);
}
static text prefix(text s, size_t length) {
  const text result = {s.data, length};
  return result;
}
static text suffix(text s, size_t start) {
  const text result = {s.data + start, s.length - start};
  return result;
}
static node resolve(engine *e, size_t *document, node reference) {
  const text ref = string(e, reference);
  const size_t hash = text_find(ref, '#');
  const text target = prefix(ref, hash);
  if (e->status != HAL_OK)
    return absent;
  if (target.length != 0) {
    if (!require(e, e->load != NULL, HAL_ENOENT))
      return absent;
    jh_json_schema_source_t loaded = {0};
    if (!checked(
            e, callback_status(e->load(e->load_user, e->sources[*document].uri,
                                       target, &loaded))))
      return absent;
    *document = register_source(e, loaded);
    if (e->status != HAL_OK)
      return absent;
  }
  const jb_json_doc_t *doc = e->sources[*document].doc;
  node out = {doc, 0};
  if (hash != ref.length) {
    const text encoded = suffix(ref, hash + 1);
    jb_string_t decoded = {0};
    text_init(e, &decoded);
    for (size_t i = 0; i < encoded.length && charge(e, 1); ++i) {
      if (encoded.data[i] == '%') {
        if (!require(e, encoded.length - i > 2, HAL_EINVAL))
          break;
        const int high = jh_hex_digit(encoded.data[i + 1]),
                  low = jh_hex_digit(encoded.data[i + 2]);
        if (!require(e, high >= 0 && low >= 0, HAL_EINVAL))
          break;
        byte(e, &decoded, (char)(high * 16 + low));
        i += 2;
      } else
        byte(e, &decoded, encoded.data[i]);
    }
    (void)require(e, jb_string_size(&decoded) <= e->limit->max_pointer_bytes,
                  HAL_EOVERFLOW);
    const text fragment = text_finish(e, &decoded);
    if (e->status != HAL_OK ||
        !checked(e, jb_utf8_validate(fragment.data, fragment.length, NULL)) ||
        !charge(e, add(e, doc->input.length, doc->token_count)) ||
        !checked(e, jb_json_pointer_get(doc, fragment, &out)))
      return absent;
  }
  check_schema(e, out, 0);
  return e->status == HAL_OK ? out : absent;
}

static void evaluate(engine *e, size_t document, node s, value v, text at,
                     node keep, bool name_error);
static bool kept(engine *e, node condition, text name) {
  if (!exists(condition) || kind(condition) != JB_JSON_OBJECT)
    return false;
  const node properties = field(e, condition, "properties");
  if (exists(find(e, properties, name)))
    return true;
  const node required = field(e, condition, "required");
  if (exists(required))
    for (node c = first(required); exists(c) && e->status == HAL_OK;
         c = next(c)) {
      const size_t saved = e->temp.used;
      const bool match = jh_span_equal(string(e, c), name);
      e->temp.used = saved;
      if (match)
        return e->status == HAL_OK;
    }
  return false;
}
static void object_errors(engine *e, size_t document, node s, node object,
                          text at, node keep, bool name_error) {
  const size_t saved = e->temp.used;
  const node required = field(e, s, "required");
  if (exists(required))
    for (node c = first(required); exists(c) && e->status == HAL_OK;
         c = next(c)) {
      const size_t member = e->temp.used;
      const text name = string(e, c);
      if (!exists(find(e, object, name)) && e->status == HAL_OK)
        report(e, required, joined(e, at, name), "required",
               jh_span("required field is missing"), name_error);
      e->temp.used = member;
    }
  const node properties = field(e, s, "properties"),
             additional = field(e, s, "additionalProperties"),
             names = field(e, s, "propertyNames");
  for (node c = first(object); exists(c) && e->status == HAL_OK; c = next(c)) {
    const size_t member = e->temp.used;
    const text name = key(e, c), path = joined(e, at, name);
    const node property = find(e, properties, name);
    if (exists(property))
      evaluate(e, document, property, item(c), path, absent, name_error);
    else if (exists(additional)) {
      if (kind(additional) == JB_JSON_BOOL && !boolean(e, additional))
        report(e, additional, path, "additionalProperties",
               jh_span("unknown field"), name_error);
      else
        evaluate(e, document, additional, item(c), path, absent, name_error);
    }
    if (exists(names)) {
      const value name_value = {c, true};
      evaluate(e, document, names, name_value, path, absent, true);
    }
    e->temp.used = member;
  }
  node rule = field(e, s, "minProperties");
  if (exists(rule) && count(object) < natural(e, rule))
    report(e, rule, at, "minProperties",
           concat(e, jh_span("fewer than "), raw(e, rule), jh_span(" fields")),
           name_error);
  rule = field(e, s, "maxProperties");
  if (exists(rule) && count(object) > natural(e, rule) && e->status == HAL_OK) {
    node extra = absent;
    for (node c = first(object); exists(c) && e->status == HAL_OK;
         c = next(c)) {
      const size_t member = e->temp.used;
      const text name = key(e, c);
      if (!kept(e, keep, name) &&
          (!exists(extra) || jh_span_compare(name, key(e, extra)) < 0))
        extra = c;
      e->temp.used = member;
    }
    const text target = exists(extra) ? joined(e, at, key(e, extra)) : at;
    report(e, rule, target, "maxProperties",
           concat(e, jh_span("more than "), raw(e, rule), jh_span(" fields")),
           name_error);
  }
  e->temp.used = saved;
}
static bool matches_type(engine *e, node rule, value v, jb_string_t *names) {
  bool matches = false;
  const bool array = kind(rule) == JB_JSON_ARRAY;
  for (node t = array ? first(rule) : rule; exists(t) && e->status == HAL_OK;
       t = array ? next(t) : absent) {
    const text name = string(e, t);
    if (jb_string_size(names) != 0)
      put(e, names, " or ");
    append(e, names, name);
    if (type(e, v, name))
      matches = true;
  }
  return matches;
}
static void branch_errors(engine *e, size_t document, node s, value v, text at,
                          bool name_error) {
  node rule = field(e, s, "allOf");
  if (exists(rule))
    for (node c = first(rule); exists(c) && e->status == HAL_OK; c = next(c))
      evaluate(e, document, c, v, at, absent, name_error);
  rule = field(e, s, "oneOf");
  if (exists(rule)) {
    const mark base = checkpoint(e);
    size_t passing = 0, typed = 0;
    for (node c = first(rule); exists(c) && e->status == HAL_OK; c = next(c)) {
      const mark start = checkpoint(e);
      evaluate(e, document, c, v, at, absent, name_error);
      if (e->status != HAL_OK)
        return;
      if (e->report_count == start.count)
        ++passing;
      else {
        bool root_type = false;
        for (size_t i = start.count; i < e->report_count && charge(e, 1); ++i)
          if (jh_span_equal(e->reports[i].instance_pointer, at) &&
              named(e->reports[i].reason_code, "type"))
            root_type = true;
        if (!root_type)
          ++typed;
        if (!root_type && typed == 1)
          continue;
      }
      restore(e, start);
    }
    if (passing == 1)
      restore(e, base);
    else if (passing > 1) {
      restore(e, base);
      report(e, rule, at, "oneOf", jh_span("matches more than one form"),
             name_error);
    } else if (typed != 1) {
      restore(e, base);
      report(e, rule, at, "oneOf", jh_span("matches none of the allowed forms"),
             name_error);
    }
  }
  rule = field(e, s, "not");
  if (exists(rule)) {
    const mark start = checkpoint(e);
    evaluate(e, document, rule, v, at, absent, name_error);
    const bool matched = e->report_count == start.count;
    restore(e, start);
    if (matched)
      report(e, rule, at, "not", jh_span("matches a forbidden form"),
             name_error);
  }
  rule = field(e, s, "if");
  if (exists(rule)) {
    const mark start = checkpoint(e);
    evaluate(e, document, rule, v, at, absent, name_error);
    const bool matched = e->report_count == start.count;
    restore(e, start);
    const node then_rule = field(e, s, "then");
    if (matched && exists(then_rule))
      evaluate(e, document, then_rule, v, at, rule, name_error);
  }
}
static void array_errors(engine *e, size_t document, node s, node array,
                         text at, bool name_error) {
  node rule = field(e, s, "minItems");
  if (exists(rule) && count(array) < natural(e, rule))
    report(e, rule, at, "minItems",
           concat(e, jh_span("fewer than "), raw(e, rule), jh_span(" items")),
           name_error);
  const node unique = field(e, s, "uniqueItems"), items = field(e, s, "items");
  size_t index = 0;
  for (node c = first(array); exists(c) && e->status == HAL_OK;
       c = next(c), ++index) {
    const size_t member = e->temp.used;
    (void)charge(e, 1);
    const text child_at = joined(e, at, decimal(e, index));
    if (exists(unique) && boolean(e, unique)) {
      for (node earlier = first(array);
           earlier.index != c.index && e->status == HAL_OK;
           earlier = next(earlier))
        if (equal(e, item(c), item(earlier), 0)) {
          report(e, unique, child_at, "uniqueItems", jh_span("duplicate item"),
                 name_error);
          break;
        }
    }
    e->temp.used = member;
  }
  index = 0;
  if (exists(items))
    for (node c = first(array); exists(c) && e->status == HAL_OK;
         c = next(c), ++index) {
      const size_t member = e->temp.used;
      evaluate(e, document, items, item(c), joined(e, at, decimal(e, index)),
               absent, name_error);
      e->temp.used = member;
    }
}
static void scalar_errors(engine *e, node s, value v, text at,
                          bool name_error) {
  node rule = field(e, s, "const");
  if (exists(rule) && !equal(e, v, item(rule), 0))
    report(e, rule, at, "const", repr(e, rule, "expected "), name_error);
  rule = field(e, s, "enum");
  if (exists(rule)) {
    bool matches = false;
    for (node c = first(rule); exists(c) && e->status == HAL_OK; c = next(c))
      if (equal(e, v, item(c), 0)) {
        matches = true;
        break;
      }
    if (!matches)
      report(e, rule, at, "enum", repr(e, rule, "expected one of "),
             name_error);
  }
  if (value_kind(v) == JB_JSON_STRING) {
    const text input = string_value(e, v);
    rule = field(e, s, "minLength");
    if (exists(rule)) {
      size_t length = 0;
      for (size_t i = 0; i < input.length && charge(e, 1); ++i)
        if (((unsigned char)input.data[i] & 0xc0U) != 0x80U)
          ++length;
      if (length < natural(e, rule))
        report(e, rule, at, "minLength",
               concat(e, jh_span("shorter than "), raw(e, rule), empty),
               name_error);
    }
    rule = field(e, s, "pattern");
    if (exists(rule)) {
      const text regex = string(e, rule);
      if (!search(e, regex, input))
        report(e, rule, at, "pattern",
               concat(e, jh_span("does not match "), quoted(e, regex), empty),
               name_error);
    }
  }
  if (value_kind(v) == JB_JSON_NUMBER) {
    rule = field(e, s, "minimum");
    if (exists(rule) && compare(e, v.n, rule) < 0)
      report(e, rule, at, "minimum",
             concat(e, jh_span("below "), raw(e, rule), empty), name_error);
    rule = field(e, s, "maximum");
    if (exists(rule) && compare(e, v.n, rule) > 0)
      report(e, rule, at, "maximum",
             concat(e, jh_span("above "), raw(e, rule), empty), name_error);
  }
}
static void evaluate(engine *e, size_t document, node s, value v, text at,
                     node keep, bool name_error) {
  const size_t saved = e->temp.used, depth = e->depth;
  if (!charge(e, 1) ||
      !require(e, depth < e->limit->max_ref_depth, HAL_EOVERFLOW))
    goto done;
  for (size_t i = 0; i < depth; ++i) {
    const active *a = &e->stack[i];
    if (!charge(e, 1) ||
        !require(e,
                 !(a->schema.doc == s.doc && a->schema.index == s.index &&
                   a->instance.n.doc == v.n.doc &&
                   a->instance.n.index == v.n.index &&
                   a->instance.key == v.key),
                 HAL_EINVAL))
      goto done;
  }
  const active active_value = {s, v};
  e->stack[e->depth++] = active_value;
  if (kind(s) == JB_JSON_BOOL) {
    if (!boolean(e, s))
      report(e, s, at, "false", jh_span("no value is allowed here"),
             name_error);
    goto done;
  }
  node rule = field(e, s, "$ref");
  if (exists(rule)) {
    size_t target_doc = document;
    const node target = resolve(e, &target_doc, rule);
    evaluate(e, target_doc, target, v, at, absent, name_error);
  }
  rule = field(e, s, "type");
  if (exists(rule)) {
    jb_string_t names = {0};
    text_init(e, &names);
    const bool matches = matches_type(e, rule, v, &names);
    const text description = text_finish(e, &names);
    if (!matches) {
      report(e, rule, at, "type",
             concat(e, jh_span("type: expected "), description, empty),
             name_error);
      goto done;
    }
  }
  scalar_errors(e, s, v, at, name_error);
  if (value_kind(v) == JB_JSON_ARRAY)
    array_errors(e, document, s, v.n, at, name_error);
  if (value_kind(v) == JB_JSON_OBJECT)
    object_errors(e, document, s, v.n, at, keep, name_error);
  branch_errors(e, document, s, v, at, name_error);
done:
  e->depth = depth;
  e->temp.used = saved;
}

static bool numeric(text t) {
  if (t.length == 0)
    return false;
  for (size_t i = 0; i < t.length; ++i)
    if (!jh_ascii_is_digit(t.data[i]))
      return false;
  return true;
}
static int pointer_order(engine *e, text a, text b) {
  while (a.length != 0 && b.length != 0 && charge(e, 1)) {
    a = suffix(a, 1);
    b = suffix(b, 1);
    const size_t ae = text_find(a, '/'), be = text_find(b, '/');
    text x = prefix(a, ae), y = prefix(b, be);
    if (!charge(e, add(e, x.length, y.length)))
      return 0;
    const bool xn = numeric(x), yn = numeric(y);
    if (xn != yn)
      return xn ? -1 : 1;
    if (xn) {
      while (x.length > 1 && x.data[0] == '0')
        x = suffix(x, 1);
      while (y.length > 1 && y.data[0] == '0')
        y = suffix(y, 1);
      if (x.length != y.length)
        return x.length < y.length ? -1 : 1;
    }
    const int order = jh_span_compare(x, y);
    if (order != 0)
      return order;
    a = suffix(a, ae);
    b = suffix(b, be);
  }
  return a.length == 0 ? (b.length == 0 ? 0 : -1) : 1;
}
static void sort_reports(engine *e) {
  for (size_t i = 1; i < e->report_count && e->status == HAL_OK; ++i) {
    const jh_json_schema_error_t current = e->reports[i];
    size_t j = i;
    while (j != 0 &&
           pointer_order(e, current.instance_pointer,
                         e->reports[j - 1].instance_pointer) < 0 &&
           charge(e, 1)) {
      e->reports[j] = e->reports[j - 1];
      --j;
    }
    e->reports[j] = current;
  }
}
static size_t temp_size(engine *e) {
  const jh_json_schema_limits_t *l = e->limit;
  return mul(e, add(e, l->max_ref_depth, 2),
             add(e,
                 add(e, mul(e, l->max_value_bytes, 32),
                     mul(e, l->max_pointer_bytes, 4)),
                 add(e, l->max_uri_bytes, 8192)));
}
static size_t workspace_size(engine *e) {
  const jh_json_schema_limits_t *l = e->limit;
  if (!require(e, l->max_ref_depth <= 128, HAL_EINVAL))
    return 0;
  size_t n = mul(e, l->max_schema_nodes, sizeof(jh_json_schema_source_t));
  n = add(e, n, mul(e, l->max_ref_depth, sizeof(active)));
  n = add(e, n, mul(e, l->max_reports, sizeof(jh_json_schema_error_t)));
  n = add(e, n, temp_size(e));
  n = add(e, n, l->max_report_bytes);
  return add(e, add(e, n, l->max_regex_bytes), 6 * JB_ALIGNOF(jb_max_align_t));
}
static arena subarena(engine *e, arena *all, size_t size) {
  const arena result = {take(e, all, size, JB_ALIGNOF(jb_max_align_t)), size,
                        0};
  return result;
}
hal_status_t
jh_json_schema_workspace_size(const jh_json_schema_limits_t *limits,
                              size_t *out_bytes) {
  if (limits == NULL || out_bytes == NULL)
    return HAL_EINVAL;
  engine e = {0};
  e.status = HAL_OK;
  e.limit = limits;
  const size_t n = workspace_size(&e);
  if (e.status == HAL_OK)
    *out_bytes = n;
  return e.status;
}
hal_status_t jh_json_schema_validate(
    const jb_json_doc_t *instance, const jh_json_schema_source_t *schema,
    jh_json_schema_load_fn load, void *load_user,
    jh_json_schema_report_fn callback, void *report_user,
    jb_json_workspace_t work, const jh_json_schema_limits_t *limits) {
  if (instance == NULL || schema == NULL || limits == NULL ||
      (work.data == NULL && work.size != 0))
    return HAL_EINVAL;
  engine e = {0};
  e.status = HAL_OK;
  e.limit = limits;
  e.remaining = limits->max_work;
  e.load = load;
  e.load_user = load_user;
  const size_t needed = workspace_size(&e);
  if (!require(&e, work.data != NULL && work.size >= needed, HAL_EOVERFLOW) ||
      !require(&e, instance->valid && instance->token_count != 0, HAL_EINVAL) ||
      !require(&e, instance->token_count <= limits->max_instance_nodes,
               HAL_EOVERFLOW))
    return e.status;
  arena all = {work.data, work.size, 0};
  e.sources =
      take(&e, &all, mul(&e, limits->max_schema_nodes, sizeof(*e.sources)),
           JB_ALIGNOF(jb_max_align_t));
  e.stack = take(&e, &all, mul(&e, limits->max_ref_depth, sizeof(*e.stack)),
                 JB_ALIGNOF(jb_max_align_t));
  e.reports = take(&e, &all, mul(&e, limits->max_reports, sizeof(*e.reports)),
                   JB_ALIGNOF(jb_max_align_t));
  e.temp = subarena(&e, &all, temp_size(&e));
  e.report_text = subarena(&e, &all, limits->max_report_bytes);
  e.regex_memory = subarena(&e, &all, limits->max_regex_bytes);
  if (e.status != HAL_OK)
    return e.status;
  const size_t root = register_source(&e, *schema);
  const node schema_root = {schema->doc, 0}, instance_root = {instance, 0};
  evaluate(&e, root, schema_root, item(instance_root), empty, absent, false);
  if (e.status == HAL_OK)
    sort_reports(&e);
  if (callback != NULL)
    for (size_t i = 0; i < e.report_count && e.status == HAL_OK; ++i)
      (void)checked(&e, callback_status(callback(report_user, &e.reports[i])));
  return e.status != HAL_OK    ? e.status
         : e.report_count == 0 ? HAL_OK
                               : HAL_ECONFIG;
}
