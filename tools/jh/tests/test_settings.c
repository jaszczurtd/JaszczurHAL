/* The settings reader's accepted model, missing files, root types, located
 * parse errors, limits, differences from Python and paths that are no file.
 * A native test, also run under Valgrind and the sanitizers. */
#include "jh_json_file.h"
#include <stdio.h>
#include <string.h>

static int failures;
static void expect(bool ok, const char *what) {
  if (!ok) {
    (void)fprintf(stderr, "settings: %s\n", what);
    ++failures;
  }
}

typedef struct {
  jb_string_t text;
  bool exists;
  jh_failure_t error;
  jh_json_location_t where;
} outcome_t;

static void outcome_init(outcome_t *result) {
  memset(result, 0, sizeof *result);
  (void)jb_string_init(&result->text, jb_heap_allocator());
  jh_failure_init(&result->error);
  jh_json_location_init(&result->where);
}

static void outcome_destroy(outcome_t *result) {
  jh_json_location_destroy(&result->where);
  jh_failure_destroy(&result->error);
  jb_string_destroy(&result->text);
}

static void read_settings(const char *path, size_t limit, outcome_t *result) {
  outcome_init(result);
  jb_json_tree_t *tree = NULL;
  jb_json_tree_view_t root;
  const jb_json_tree_format_t format = {limit * 6, 0, false, false};
  size_t written = 0;
  if (jh_json_read_settings(&result->where, path, limit, &tree, &result->exists,
                            &result->error) == HAL_OK &&
      jh_require(&result->error, jb_json_tree_root(tree, &root), 2,
                 "no root") == HAL_OK)
    (void)jh_require(&result->error,
                     jb_json_tree_format(root, &format, jh_string_sink,
                                         &result->text, &written),
                     4, "cannot format the tree");
  jb_json_tree_destroy(tree);
}

static bool write_file(const char *directory, const char *name,
                       jb_json_span_t bytes, jb_string_t *path) {
  jh_failure_t failure = {0};
  jh_failure_init(&failure);
  bool changed = false;
  const bool ok = jh_absolute_path(name, directory, path, &failure) == HAL_OK &&
                  jb_file_write_atomic(jb_string_data(path), bytes.data,
                                       bytes.length, false, &changed) == HAL_OK;
  expect(ok, name);
  jh_failure_destroy(&failure);
  return ok;
}

static bool failed(const outcome_t *result, hal_status_t status,
                   int exit_code) {
  return result->error.status == status && result->error.exit_code == exit_code;
}

static bool equals(const jb_string_t *text, const char *expected) {
  return jh_span_equal(jh_string_span(text), jh_span(expected));
}

static void accepted_model(const char *directory, jb_string_t *path) {
  if (!write_file(
          directory, "settings.json",
          jh_span(
              "{\"a\": 1, // line\r\n \"a\": [NaN, -Infinity, 1.50e3, -0],\r"
              " /* block\n */ \"b\": \"\\u017c\xc5\xbc\", \"c\": {\"d\": true, "
              "\"d\": null}}"),
          path))
    return;
  outcome_t result;
  read_settings(jb_string_data(path), 65536, &result);
  expect(result.error.status == HAL_OK && result.exists, "settings accepted");
  expect(equals(&result.text, "{\"a\":[NaN,-Infinity,1.50e3,-0],\"b\":"
                              "\"\xc5\xbc\xc5\xbc\",\"c\":{\"d\":null}}"),
         "comments dropped, last duplicate kept in place, tokens kept");
  outcome_destroy(&result);
}

static void missing_file(const char *directory, jb_string_t *path) {
  jh_failure_t failure = {0};
  jh_failure_init(&failure);
  if (jh_absolute_path("missing.json", directory, path, &failure) != HAL_OK) {
    expect(false, "missing path");
  } else {
    outcome_t result;
    read_settings(jb_string_data(path), 65536, &result);
    expect(result.error.status == HAL_OK && !result.exists &&
               equals(&result.text, "{}"),
           "a missing file reads as an empty object");
    outcome_destroy(&result);
  }
  jh_failure_destroy(&failure);
}

static void root_type(const char *directory, jb_string_t *path) {
  if (!write_file(directory, "array.json", jh_span("[1]"), path))
    return;
  outcome_t result;
  read_settings(jb_string_data(path), 65536, &result);
  jb_string_t expected = {0};
  (void)jb_string_init(&expected, jb_heap_allocator());
  expect(jh_join(&expected, JH_PARTS(jh_string_span(path),
                                     jh_span(" must contain a JSON object"))) ==
                 HAL_OK &&
             failed(&result, HAL_ETYPE, 2) &&
             jh_span_equal(jh_string_span(&result.error.message),
                           jh_string_span(&expected)) &&
             !result.where.located,
         "a root other than an object");
  jb_string_destroy(&expected);
  outcome_destroy(&result);
}

static void located_error(const char *directory, jb_string_t *path) {
  if (!write_file(
          directory, "invalid.json",
          jh_span("{\n  // \"a\": 1,\n  /* two\n     lines */ \"b\": }\n"),
          path))
    return;
  outcome_t result;
  read_settings(jb_string_data(path), 65536, &result);
  expect(failed(&result, HAL_EPROTO, 2) && result.where.located &&
             result.where.line == 4 && result.where.column == 20,
         "a parse error is located in the file with its comments");
  outcome_destroy(&result);
}

static void limits(const char *directory, jb_string_t *path) {
  jb_string_t deep = {0};
  hal_status_t status = jb_string_init(&deep, jb_heap_allocator());
  jh_put(&deep, &status, "{\"a\": ");
  for (size_t i = 0; i < 256; ++i)
    jh_put(&deep, &status, "[");
  for (size_t i = 0; i < 256; ++i)
    jh_put(&deep, &status, "]");
  jh_put(&deep, &status, "}");
  expect(status == HAL_OK, "create deep input");
  if (status == HAL_OK &&
      write_file(directory, "deep.json", jh_string_span(&deep), path)) {
    outcome_t result;
    read_settings(jb_string_data(path), 65536, &result);
    expect(failed(&result, HAL_EOVERFLOW, 4),
           "257 nested containers exceed the parser limit");
    outcome_destroy(&result);
  }
  jb_string_destroy(&deep);
  if (write_file(directory, "large.json", jh_span("{\"a\": \"long\"}"), path)) {
    outcome_t result;
    read_settings(jb_string_data(path), 8, &result);
    expect(result.error.exit_code == 4, "a file above the limit");
    outcome_destroy(&result);
  }
}

static void python_differences(const char *directory, jb_string_t *path) {
  if (write_file(directory, "surrogate.json", jh_span("{\"a\": \"\\ud800\"}"),
                 path)) {
    outcome_t result;
    read_settings(jb_string_data(path), 65536, &result);
    expect(failed(&result, HAL_EPROTO, 2),
           "an unpaired surrogate escape is rejected");
    outcome_destroy(&result);
  }
  jb_string_t integer = {0};
  hal_status_t status = jb_string_init(&integer, jb_heap_allocator());
  jh_put(&integer, &status, "{\"a\":");
  if (status == HAL_OK)
    status = jb_string_resize(&integer, jb_string_size(&integer) + 4301, '7');
  jh_put(&integer, &status, "}");
  expect(status == HAL_OK, "create long integer");
  if (status == HAL_OK &&
      write_file(directory, "integer.json", jh_string_span(&integer), path)) {
    outcome_t result;
    read_settings(jb_string_data(path), 65536, &result);
    expect(result.error.status == HAL_OK &&
               jh_span_equal(jh_string_span(&result.text),
                             jh_string_span(&integer)),
           "an integer above the int() limit of Python keeps its token");
    outcome_destroy(&result);
  }
  jb_string_destroy(&integer);
}

int main(int argc, char **argv) {
  if (argc != 2)
    return 64;
  jb_temp_dir_t *owned = NULL;
  if (jb_temp_dir_create(argv[1], &owned) != HAL_OK) {
    (void)fprintf(stderr, "settings: cannot create a directory in %s\n",
                  argv[1]);
    return 1;
  }
  const char *directory = jb_temp_dir_path(owned);
  jb_string_t path = {0};
  (void)jb_string_init(&path, jb_heap_allocator());
  accepted_model(directory, &path);
  missing_file(directory, &path);
  root_type(directory, &path);
  located_error(directory, &path);
  limits(directory, &path);
  python_differences(directory, &path);
  outcome_t result;
  read_settings(directory, 65536, &result);
  expect(result.error.exit_code == 3, "a directory is not a file");
  outcome_destroy(&result);
  jb_string_destroy(&path);
  expect(jb_temp_dir_remove(&owned) == HAL_OK, "remove the directory");
  if (failures == 0)
    (void)printf("settings: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
