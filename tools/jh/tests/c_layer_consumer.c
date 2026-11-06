// The C layer of jh used from C: its headers compile as C11,
// jh_unified_diff writes the lines difflib.unified_diff(..., lineterm="")
// writes, and inputs that point into the string a call replaces keep their
// bytes. test_containers.cpp runs jh_test_c_layer() with a work directory.
#include "jh_failure.h"
#include "jh_generated_output.h"
#include "jh_json_file.h"
#include "jh_python_repr.h"
#include <jb/array.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int jh_test_c_layer(const char *directory);

/* The lines of one diff, each followed by LF. */
typedef struct {
  char text[1024];
  size_t length;
} collected_t;

static hal_status_t collect(void *user, jb_json_span_t prefix,
                            jb_json_span_t text) {
  collected_t *out = user;
  if (prefix.length + text.length + 1 > sizeof out->text - out->length)
    return HAL_EOVERFLOW;
  memcpy(out->text + out->length, prefix.data, prefix.length);
  out->length += prefix.length;
  if (text.length != 0)
    memcpy(out->text + out->length, text.data, text.length);
  out->length += text.length;
  out->text[out->length++] = '\n';
  return HAL_OK;
}

static int failures;

static void expect(bool ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "c layer: %s\n", what);
    ++failures;
  }
}

/* Whether the diff of from and to is expected, LF after every line. */
static bool diff_is(const char *const *from, size_t from_count,
                    const char *const *to, size_t to_count,
                    const char *expected) {
  jb_json_span_t from_lines[16], to_lines[16];
  if (from_count > COUNTOF(from_lines) || to_count > COUNTOF(to_lines))
    return false;
  for (size_t index = 0; index < from_count; ++index)
    from_lines[index] = jh_span(from[index]);
  for (size_t index = 0; index < to_count; ++index)
    to_lines[index] = jh_span(to[index]);
  collected_t out = {{0}, 0};
  return jh_unified_diff(from_lines, from_count, to_lines, to_count,
                         jh_span("from"), jh_span("to"), collect,
                         &out) == HAL_OK &&
         out.length == strlen(expected) &&
         memcmp(out.text, expected, out.length) == 0;
}

static void diff(void) {
  const char *const before[] = {"a", "b", "c"};
  const char *const after[] = {"a", "B", "c"};
  expect(diff_is(before, COUNTOF(before), after, COUNTOF(after),
                 "--- from\n+++ to\n@@ -1,3 +1,3 @@\n a\n-b\n+B\n c\n"),
         "unified diff of one changed line");
  expect(diff_is(before, COUNTOF(before), before, COUNTOF(before), ""),
         "equal inputs give no lines");

  // Seven equal lines between the changes: one more than twice the context,
  // so the diff splits into two hunks.
  const char *const lines[] = {"L1", "L2", "L3", "L4",  "L5",  "L6",
                               "L7", "L8", "L9", "L10", "L11", "L12"};
  const char *const changed[] = {"L1", "X",  "L3", "L4", "L5",  "L6",
                                 "L7", "L8", "L9", "Y",  "L11", "L12"};
  expect(diff_is(lines, COUNTOF(lines), changed, COUNTOF(changed),
                 "--- from\n+++ to\n@@ -1,5 +1,5 @@\n L1\n-L2\n+X\n L3\n L4\n"
                 " L5\n@@ -7,6 +7,6 @@\n L7\n L8\n L9\n-L10\n+Y\n L11\n L12\n"),
         "unified diff splits a gap of seven equal lines into two hunks");
}

static bool equals(const jb_string_t *text, const char *expected) {
  return jh_span_equal(
      (jb_json_span_t){jb_string_data(text), jb_string_size(text)},
      jh_span(expected));
}

/* Replace where->source with "XXXX" and path, and return the copy of path
 * inside it. */
static const char *path_in_source(jh_json_location_t *where,
                                  const jb_string_t *path) {
  return jb_string_assign(&where->source, "XXXX", 4) == HAL_OK &&
                 jb_string_append(&where->source, jb_string_data(path),
                                  jb_string_size(path)) == HAL_OK
             ? jb_string_data(&where->source) + 4
             : NULL;
}

/* Regressions: repr() of the bytes of its own output read them after the
 * output moved; a message part quoting the previous message read cleared
 * bytes; a path inside where->source was read after the assignment of
 * where->source had moved it. */
static void aliasing(const char *directory) {
  jb_string_t out = {0};
  expect(jb_string_init(&out, jb_heap_allocator()) == HAL_OK &&
             jb_string_assign(&out, "abcdefgh", 8) == HAL_OK,
         "repr input");
  const jb_json_span_t self = {jb_string_data(&out), jb_string_size(&out)};
  expect(jh_python_repr_append(&out, self) == HAL_OK &&
             equals(&out, "abcdefgh'abcdefgh'"),
         "repr of the bytes of its own output");
  jb_string_destroy(&out);

  jh_failure_t failure;
  jh_failure_init(&failure);
  (void)jh_fail_text(&failure, HAL_EIO, 3, "cannot read input file");
  const jb_json_span_t parts[] = {
      jh_span("wrapped: "),
      {jb_string_data(&failure.message), jb_string_size(&failure.message)}};
  expect(jh_fail(&failure, HAL_EIO, 3, parts, COUNTOF(parts)) == HAL_EIO &&
             equals(&failure.message, "wrapped: cannot read input file"),
         "a message part quoting the previous message");

  jb_string_t path = {0};
  bool changed = false;
  expect(jh_absolute_path("c-layer-alias.json", directory, &path, &failure) ==
                 HAL_OK &&
             jb_file_write_atomic(jb_string_data(&path), "{\"a\":1}", 7, false,
                                  &changed) == HAL_OK,
         "aliasing fixture file");
  jh_json_location_t where;
  jh_json_location_init(&where);
  jh_json_document_t document = {0};
  expect(jh_json_read_document(&where, path_in_source(&where, &path), 4096,
                               false, &document, &failure) == HAL_OK &&
             document.doc.token_count == 2 &&
             equals(&where.source, jb_string_data(&path)),
         "document path inside where->source");
  jh_json_document_destroy(&document);
  jb_json_tree_t *tree = NULL;
  bool exists = false;
  expect(jh_json_read_settings(&where, path_in_source(&where, &path), 4096,
                               &tree, &exists, &failure) == HAL_OK &&
             exists && equals(&where.source, jb_string_data(&path)),
         "settings path inside where->source");
  if (tree != NULL)
    jb_json_tree_destroy(tree);
  jh_json_location_destroy(&where);
  jb_string_destroy(&path);
  jh_failure_destroy(&failure);
}

/* jh_json_read_value reads a file the way json.loads does (the last value of
 * a repeated name at the first name's place, NaN and Infinity), and
 * jh_python_repr_json_append writes what repr() writes for the result. */
static void python_repr(const char *directory) {
  static const char input[] =
      "{\"b\": 1, \"b\": [1e16, 1e15, 1e-4, 1e-5, -0.0, 1.0, 1E2, 2.5e-7, 0.1, "
      "123456789012345678.0, NaN, -Infinity, 18446744073709551616, -0, true, "
      "null, \"a'b\", \"a\\\"b\", \"\\u00e9\\n\"], \"a\": {\"k\": []}}";
  static const char expected[] =
      "{'b': [1e+16, 1000000000000000.0, 0.0001, 1e-05, -0.0, 1.0, 100.0, "
      "2.5e-07, 0.1, 1.2345678901234568e+17, nan, -inf, 18446744073709551616, "
      "0, True, None, \"a'b\", 'a\"b', '\xc3\xa9\\n'], 'a': {'k': []}}";
  jh_failure_t failure;
  jh_failure_init(&failure);
  jb_string_t path = {0}, out = {0};
  bool changed = false;
  expect(jb_string_init(&out, jb_heap_allocator()) == HAL_OK &&
             jh_absolute_path("c-layer-repr.json", directory, &path,
                              &failure) == HAL_OK &&
             jb_file_write_atomic(jb_string_data(&path), input,
                                  COUNTOF(input) - 1, false,
                                  &changed) == HAL_OK,
         "repr fixture file");
  jh_json_location_t where;
  jh_json_location_init(&where);
  jb_json_tree_t *tree = NULL;
  jb_json_tree_view_t root = {0};
  expect(jh_json_read_value(&where, path_in_source(&where, &path), 4096, &tree,
                            &failure) == HAL_OK &&
             jb_json_tree_root(tree, &root) == HAL_OK &&
             jh_python_repr_json_append(&out, root) == HAL_OK &&
             equals(&out, expected),
         "repr() of a json.loads value");
  if (tree != NULL)
    jb_json_tree_destroy(tree);
  tree = NULL;
  expect(jh_absolute_path("c-layer-missing.json", directory, &path, &failure) ==
                 HAL_OK &&
             jh_json_read_value(&where, jb_string_data(&path), 4096, &tree,
                                &failure) == HAL_ENOENT &&
             tree == NULL && failure.exit_code == 3,
         "a missing value file fails");
  jh_json_location_destroy(&where);
  jb_string_destroy(&out);
  jb_string_destroy(&path);
  jh_failure_destroy(&failure);
}

/* Append count copies of digit. */
static bool append_digits(jb_string_t *text, char digit, size_t count) {
  return jb_string_resize(text, jb_string_size(text) + count, digit) == HAL_OK;
}

static bool append_text(jb_string_t *text, const char *literal) {
  return jb_string_append(text, literal, strlen(literal)) == HAL_OK;
}

/* Regressions: float() of a number past the conversion budget came out as
 * inf. Numbers of any length round as float() rounds them, and only a value
 * past 2^1024 - 2^970 is an infinity. */
static void float_repr(const char *directory) {
  static const char bound[] =
      "17976931348623158079372897140530341507993413271003782693617377898044496"
      "82927647509466490179775872070963302864166928879109465555478519404026306"
      "57488671505820681908902000708383676273854845817711531764475730270069855"
      "57136695962284291481986083493647529271907416844436551070434271155969950"
      "8093042880177904174497792";
  static const char midpoint[] =
      "1.00000000000000011102230246251565404236316680908203125";
  static const char expected[] =
      "[0.1111111111111111, inf, -inf, 1.7976931348623157e+308, "
      "1.0000000000000002, 1.0, inf, 0.0, -0.0, 5e-324, 0.0, 0.1, 1.0, inf, "
      "-0.0, 1.7976931348623157e+308]";
  char below[sizeof bound];
  memcpy(below, bound, sizeof bound);
  below[sizeof bound - 2] = '1'; /* bound - 1 */
  jh_failure_t failure;
  jh_failure_init(&failure);
  jb_string_t input = {0}, path = {0}, out = {0};
  bool built = jb_string_init(&input, jb_heap_allocator()) == HAL_OK &&
               jb_string_init(&out, jb_heap_allocator()) == HAL_OK &&
               append_text(&input, "[0.") && append_digits(&input, '1', 5000) &&
               append_text(&input, ", ") && append_text(&input, bound) &&
               append_text(&input, ".0, -") && append_text(&input, bound) &&
               append_text(&input, ".0, ") && append_text(&input, below) &&
               append_text(&input, ".") && append_digits(&input, '9', 800) &&
               append_text(&input, ", ") && append_text(&input, midpoint) &&
               append_digits(&input, '0', 1000) && append_text(&input, "1, ") &&
               append_text(&input, midpoint) &&
               append_text(&input, ", 1e309, 1e-400, -1e-400, "
                                   "2.4703282292062328e-324, "
                                   "2.4703282292062327e-324, 0.") &&
               append_digits(&input, '0', 400) &&
               append_text(&input, "1e400, ") &&
               append_digits(&input, '9', 2000) &&
               append_text(&input, "e-2000, 1e99999999999999999999, "
                                   "-0e-99999999999999999999, "
                                   "1.7976931348623157e308]");
  bool changed = false;
  expect(built &&
             jh_absolute_path("c-layer-floats.json", directory, &path,
                              &failure) == HAL_OK &&
             jb_file_write_atomic(jb_string_data(&path), jb_string_data(&input),
                                  jb_string_size(&input), false,
                                  &changed) == HAL_OK,
         "float fixture file");
  jh_json_location_t where;
  jh_json_location_init(&where);
  jb_json_tree_t *tree = NULL;
  jb_json_tree_view_t root = {0};
  expect(jh_json_read_value(&where, jb_string_data(&path), 65536, &tree,
                            &failure) == HAL_OK &&
             jb_json_tree_root(tree, &root) == HAL_OK &&
             jh_python_repr_json_append(&out, root) == HAL_OK &&
             equals(&out, expected),
         "float() rounding and range of long numbers");
  if (tree != NULL)
    jb_json_tree_destroy(tree);
  jh_json_location_destroy(&where);
  jb_string_destroy(&out);
  jb_string_destroy(&path);
  jb_string_destroy(&input);
  jh_failure_destroy(&failure);
}

/* Artifact paths from artifacts.json stay below the root they are joined to. */
static void artifact_paths(void) {
  static const char *const inside[] = {
      "src/hal/generated/x.h", "a/./b", "a..b/c", "..a", "a/b..", "x"};
  static const char *const outside[] = {
      "", "/x", "\\x", "C:x", "..", "../x", "a/..", "a\\..\\b", "a/../../x"};
  for (size_t i = 0; i < COUNTOF(inside); ++i)
    expect(jh_generated_path_relative(jh_span(inside[i])), inside[i]);
  for (size_t i = 0; i < COUNTOF(outside); ++i)
    expect(!jh_generated_path_relative(jh_span(outside[i])), outside[i]);
}

/* An allocator that refuses every request while blocked. */
typedef struct {
  bool blocked;
} gate_t;

static void *gate_allocate(void *context, size_t bytes) {
  const gate_t *gate = context;
  return gate->blocked ? NULL : malloc(bytes);
}

static void gate_release(void *context, void *data) {
  (void)context;
  free(data);
}

/* jh_python_float_append keeps the previous bytes when it cannot grow the
 * string, as jh_python_repr_append does. */
static void float_append_failure(void) {
  gate_t gate = {false};
  const jb_allocator_t allocator = {&gate, gate_allocate, gate_release};
  jb_string_t out = {0};
  expect(jb_string_init(&out, &allocator) == HAL_OK &&
             jb_string_append(&out, "x", 1) == HAL_OK,
         "gated string");
  gate.blocked = true;
  expect(jh_python_float_append(&out, -1.25) == HAL_ENOMEM && equals(&out, "x"),
         "a failed float repr leaves the string as it was");
  gate.blocked = false;
  expect(jh_python_float_append(&out, -1.25) == HAL_OK &&
             equals(&out, "x-1.25"),
         "the float repr after the failure");
  jb_string_destroy(&out);
}

int jh_test_c_layer(const char *directory) {
  failures = 0;
  diff();
  artifact_paths();
  aliasing(directory);
  python_repr(directory);
  float_repr(directory);
  float_append_failure();
  return failures;
}
