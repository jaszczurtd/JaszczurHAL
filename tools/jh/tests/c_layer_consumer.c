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

int jh_test_c_layer(const char *directory) {
  failures = 0;
  diff();
  aliasing(directory);
  return failures;
}
