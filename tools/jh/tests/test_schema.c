#include "jh_failure.h"
#include "jh_json_schema.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

hal_status_t jh_schema_test_unknown_status(void *,
                                           const jh_json_schema_error_t *);
hal_status_t jh_schema_test_unknown_load(void *, jb_json_span_t, jb_json_span_t,
                                         jh_json_schema_source_t *);

static int failures;
static bool heap_forbidden;
static size_t heap_requests;
static void *test_heap_allocate(void *user, size_t bytes) {
  (void)user;
  if (heap_forbidden) {
    ++heap_requests;
    return NULL;
  }
  return malloc(bytes);
}
static void test_heap_release(void *user, void *data) {
  (void)user;
  free(data);
}
/* Replace the static host allocator in this executable. A container using
 * the default heap during validation must fail, on Windows as well as Unix. */
const jb_allocator_t *jb_heap_allocator(void) {
  static const jb_allocator_t allocator = {NULL, test_heap_allocate,
                                           test_heap_release};
  return &allocator;
}
static void require(bool ok, const char *what) {
  if (!ok) {
    (void)fprintf(stderr, "schema: %s\n", what);
    ++failures;
  }
}
static void setup(hal_status_t status) {
  if (status != HAL_OK) {
    (void)fprintf(stderr, "schema setup: %s\n", hal_status_to_string(status));
    exit(1);
  }
}
typedef struct {
  jb_string_t input;
  jb_vector_t tokens;
  jb_json_doc_t doc;
} document;
static void document_init(document *d, const char *input) {
  const jb_json_limits_t limits = {65536, 4096, 128,    65536,
                                   4096,  4096, 1000000};
  setup(jb_string_init(&d->input, jb_heap_allocator()));
  setup(jh_append(&d->input, input));
  setup(
      jb_vector_init(&d->tokens, sizeof(jb_json_token_t), jb_heap_allocator()));
  setup(jb_vector_resize(&d->tokens, 4096));
  size_t n = 0;
  setup(jb_json_parse_workspace_size(&limits, &n));
  void *scratch = malloc(n);
  if (scratch == NULL)
    setup(HAL_ENOMEM);
  const jb_json_workspace_t workspace = {scratch, n};
  setup(jb_json_parse(jh_string_span(&d->input), &limits,
                      jb_vector_data(&d->tokens), jb_vector_size(&d->tokens),
                      workspace, &d->doc, NULL));
  free(scratch);
}
static void document_destroy(document *d) {
  jb_vector_destroy(&d->tokens);
  jb_string_destroy(&d->input);
}
typedef struct {
  jb_string_t at, uri, rule, code, message;
  jb_json_node_t node;
} report_record;
typedef struct {
  document schema, instance;
  jb_vector_t reports;
  jh_json_schema_limits_t limits;
} run;
static run runs[64];
static size_t run_count;
static jh_json_schema_limits_t defaults(void) {
  const jh_json_schema_limits_t result = {10000000,    32,     64,   2048, 4096,
                                          65536,       4096,   4096, 4096, 1024,
                                          1024 * 1024, 100000, 64,   512};
  return result;
}
static run *new_run(const char *schema, const char *instance) {
  if (run_count == COUNTOF(runs))
    setup(HAL_EOVERFLOW);
  run *r = &runs[run_count++];
  document_init(&r->schema, schema);
  document_init(&r->instance, instance);
  setup(
      jb_vector_init(&r->reports, sizeof(report_record), jb_heap_allocator()));
  r->limits = defaults();
  return r;
}
static void copy_text(jb_string_t *out, jb_json_span_t s) {
  setup(jb_string_init(out, jb_heap_allocator()));
  setup(jh_append_span(out, s));
}
static hal_status_t collect(void *user, const jh_json_schema_error_t *error) {
  const bool previous = heap_forbidden;
  heap_forbidden = false;
  jb_vector_t *reports = user;
  report_record record = {0};
  copy_text(&record.at, error->instance_pointer);
  copy_text(&record.uri, error->schema_uri);
  copy_text(&record.rule, error->schema_pointer);
  copy_text(&record.code, error->reason_code);
  copy_text(&record.message, error->message);
  record.node = error->schema_rule;
  setup(jb_vector_append(reports, &record, 1));
  heap_forbidden = previous;
  return HAL_OK;
}
static size_t reports(const run *r) { return jb_vector_size(&r->reports); }
static const report_record *report_at(const run *r, size_t index) {
  return jb_vector_const_at(&r->reports, index);
}
static bool same(const jb_string_t *s, const char *expected) {
  return jh_span_equal(jh_string_span(s), jh_span(expected));
}
static hal_status_t validate_with(run *r, jh_json_schema_report_fn callback,
                                  void *user, jh_json_schema_load_fn loader,
                                  void *load_user) {
  size_t n = 0;
  setup(jh_json_schema_workspace_size(&r->limits, &n));
  unsigned char *scratch = malloc(n + 1);
  if (scratch == NULL)
    setup(HAL_ENOMEM);
  const jh_json_schema_source_t source = {{"root.schema.json", 16},
                                          &r->schema.doc};
  const jb_json_workspace_t workspace = {scratch + 1, n};
  heap_forbidden = true;
  const hal_status_t status = jh_json_schema_validate(
      &r->instance.doc, &source, loader, load_user, callback,
      user == NULL ? &r->reports : user, workspace, &r->limits);
  heap_forbidden = false;
  free(scratch);
  return status;
}
static hal_status_t validate(run *r) {
  return validate_with(r, collect, NULL, NULL, NULL);
}
static void cleanup(void) {
  for (size_t i = 0; i < run_count; ++i) {
    run *r = &runs[i];
    for (size_t j = 0; j < reports(r); ++j) {
      report_record *record = jb_vector_at(&r->reports, j);
      jb_string_destroy(&record->at);
      jb_string_destroy(&record->uri);
      jb_string_destroy(&record->rule);
      jb_string_destroy(&record->code);
      jb_string_destroy(&record->message);
    }
    jb_vector_destroy(&r->reports);
    document_destroy(&r->schema);
    document_destroy(&r->instance);
    memset(r, 0, sizeof(*r));
  }
  run_count = 0;
}
static void equality_and_branches(void) {
  /* Scalar type checks formerly in test_hardware_data.py. */
  require(validate(new_run("{\"type\":\"integer\"}", "true")) == HAL_ECONFIG,
          "booleans are not integers");
  require(validate(new_run("{\"type\":\"integer\"}", "2.0")) == HAL_OK,
          "exact integral decimal is an integer");
  require(validate(new_run("{\"type\":\"number\"}", "1.5")) == HAL_OK,
          "finite fractional number");
  char huge_integer[402];
  memset(huge_integer, '0', sizeof huge_integer - 1);
  huge_integer[0] = '1';
  huge_integer[sizeof huge_integer - 1] = '\0';
  require(validate(new_run("{\"type\":\"number\"}", huge_integer)) == HAL_OK,
          "integer magnitude need not fit binary64");
  run *equivalent =
      new_run("{\"const\":{\"a\":1,\"b\":[2.0]}}", "{\"b\":[2],\"a\":1.0}");
  require(validate(equivalent) == HAL_OK, "structural exact equality");
  run *distinct = new_run("{\"const\":1}", "true");
  require(validate(distinct) == HAL_ECONFIG, "bool differs from number");
  require(reports(distinct) == 1 &&
              same(&report_at(distinct, 0)->code, "const"),
          "const report");
  run *large = new_run("{\"const\":9007199254740992}", "9007199254740993.0");
  require(validate(large) == HAL_ECONFIG, "large decimal alias differs");
  run *fraction = new_run("{\"const\":0.1}", "0.10000000000000001");
  require(validate(fraction) == HAL_ECONFIG,
          "fractional decimal alias differs");
  run *fractional_integer =
      new_run("{\"type\":\"integer\"}", "9007199254740992.1");
  require(validate(fractional_integer) == HAL_ECONFIG,
          "fraction does not round to integer");
  run *separate =
      new_run("{\"uniqueItems\":true}", "[0.1,0.10000000000000001]");
  require(validate(separate) == HAL_OK, "distinct decimals are unique items");
  run *bounded = new_run("{\"maximum\":0.1}", "0.10000000000000001");
  require(validate(bounded) == HAL_ECONFIG, "exact decimal bound");
  run *enum_alias = new_run("{\"enum\":[0.1]}", "0.10000000000000001");
  require(validate(enum_alias) == HAL_ECONFIG, "exact decimal enum membership");
  run *forms = new_run(
      "{\"oneOf\":[{\"const\":0.1},{\"const\":0.10000000000000001}]}", "0.1");
  require(validate(forms) == HAL_OK, "oneOf distinguishes binary64 aliases");
  run *unique = new_run("{\"uniqueItems\":true}", "[1,1.0,true]");
  require(validate(unique) == HAL_ECONFIG && reports(unique) == 1 &&
              same(&report_at(unique, 0)->at, "/1"),
          "exact uniqueItems equality");
  run *typed = new_run("{\"oneOf\":[{\"type\":\"integer\",\"minimum\":3},{"
                       "\"type\":\"string\",\"minLength\":2}]}",
                       "1");
  require(validate(typed) == HAL_ECONFIG && reports(typed) == 1,
          "oneOf branch selection");
  if (reports(typed) == 1) {
    const report_record *e = report_at(typed, 0);
    require(same(&e->at, "") && same(&e->rule, "/oneOf/0/minimum") &&
                same(&e->code, "minimum") && same(&e->message, "below 3"),
            "chosen branch metadata");
    require(e->node.doc == &typed->schema.doc &&
                e->node.doc->tokens[e->node.index].type == JB_JSON_NUMBER,
            "actual keyword node");
  }
  run *conditional =
      new_run("{\"not\":{\"type\":\"null\"},\"if\":{\"required\":[\"mode\"],"
              "\"properties\":{\"mode\":{\"const\":\"x\"}}},\"then\":{"
              "\"maxProperties\":1}}",
              "{\"mode\":\"x\",\"z\":0,\"a\":1}");
  require(validate(conditional) == HAL_ECONFIG && reports(conditional) == 1 &&
              same(&report_at(conditional, 0)->at, "/a"),
          "discarded not/if diagnostics and kept condition fields");
  run *order =
      new_run("{\"properties\":{\"a\":{\"items\":{\"minimum\":1}},\"fooBar\":"
              "false,\"fooBAR\":false}}",
              "{\"fooBar\":0,\"fooBAR\":0,\"a\":[0,0,0,0,0,0,0,0,0,0,0]}");
  require(validate(order) == HAL_ECONFIG && reports(order) == 13,
          "ordered reports");
  if (reports(order) == 13)
    require(same(&report_at(order, 9)->at, "/a/9") &&
                same(&report_at(order, 10)->at, "/a/10") &&
                same(&report_at(order, 11)->at, "/fooBAR"),
            "numeric pointer order");
  /* The C port initially exhausted scratch while joining this valid type
   * list. The previous validator returns the complete type diagnostic. */
  jb_string_t type_list = {0};
  setup(jb_string_init(&type_list, jb_heap_allocator()));
  setup(jh_append(&type_list, "{\"type\":["));
  for (size_t i = 0; i < 512; ++i) {
    if (i != 0)
      setup(jh_append(&type_list, ","));
    setup(jh_append(&type_list, "\"integer\""));
  }
  setup(jh_append(&type_list, "]}"));
  run *many_types = new_run(jb_string_data(&type_list), "false");
  many_types->limits.max_ref_depth = 2;
  many_types->limits.max_value_bytes = 7;
  many_types->limits.max_pointer_bytes = 32;
  many_types->limits.max_uri_bytes = 16;
  require(validate(many_types) == HAL_ECONFIG && reports(many_types) == 1 &&
              same(&report_at(many_types, 0)->code, "type") &&
              jb_string_size(&report_at(many_types, 0)->message) == 5643,
          "long type diagnostic fits the advertised workspace");
  /* Growth blocks in the arena used to hide a failure above 1536 values.
   * Keep the complete expected report independent of the renderer. */
  static const char *const rules[] = {"const", "enum", "const", "const",
                                      "enum"};
  static const size_t sizes[] = {1024, 1536, 2048};
  static const size_t limits[][2] = {{2, 128}, {4, 8}, {4, 32}, {2, 8}};
  jb_string_t expected = {0};
  setup(jb_string_init(&expected, jb_heap_allocator()));
  cleanup();
  for (size_t size = 0; size < COUNTOF(sizes); ++size) {
    for (size_t mode = 0; mode < COUNTOF(rules); ++mode) {
      if (mode == 2 && size != 0)
        continue;
      jb_string_clear(&type_list);
      jb_string_clear(&expected);
      setup(
          jh_append(&type_list, mode >= 3 ? "{\"propertyNames\":{\"" : "{\""));
      setup(jh_append(&type_list, rules[mode]));
      setup(jh_append(&type_list, mode == 2 ? "\":{" : "\":["));
      if (mode >= 3)
        setup(jh_append(&expected, "field name: "));
      setup(jh_append(&expected, mode == 1 || mode == 4 ? "expected one of "
                                                        : "expected "));
      setup(jh_append(&expected, mode == 2 ? "{" : "["));
      const size_t entries = mode == 2 ? 512 : sizes[size];
      for (size_t i = 0; i < entries; ++i) {
        if (i != 0) {
          setup(jh_append(&type_list, ","));
          setup(jh_append(&expected, ", "));
        }
        if (mode == 2) {
          char field[32];
          (void)snprintf(field, sizeof(field), "\"%zu\":", i);
          setup(jh_append(&type_list, field));
          (void)snprintf(field, sizeof(field), "'%zu': ", i);
          setup(jh_append(&expected, field));
        }
        setup(jh_append(&type_list, "\"abc\""));
        setup(jh_append(&expected, "'abc'"));
      }
      setup(jh_append(&type_list, mode == 2 ? "}}" : mode >= 3 ? "]}}" : "]}"));
      setup(jh_append(&expected, mode == 2 ? "}" : "]"));
      for (size_t limit = 0; limit < COUNTOF(limits); ++limit) {
        if ((limit == 3 && size != 0) || (mode == 2 && limit != 3))
          continue;
        run *large_report = new_run(jb_string_data(&type_list),
                                    mode >= 3 ? "{\"x\":0}" : "false");
        large_report->limits.max_ref_depth = limits[limit][0];
        large_report->limits.max_value_bytes =
            limit == 3 && mode >= 3 ? 32 : limits[limit][1];
        large_report->limits.max_pointer_bytes = 32;
        large_report->limits.max_uri_bytes = 16;
        if (mode == 1 && entries == 2048 && limit == 2) {
          /* Exact boundaries from the previous validator for this fixture. */
          large_report->limits.max_work = 71917;
          large_report->limits.max_report_bytes = 14377;
          require(validate(large_report) == HAL_EOVERFLOW &&
                      reports(large_report) == 0,
                  "large diagnostic preserves the work boundary");
          ++large_report->limits.max_work;
          --large_report->limits.max_report_bytes;
          require(validate(large_report) == HAL_EOVERFLOW &&
                      reports(large_report) == 0,
                  "large diagnostic preserves the report byte boundary");
          ++large_report->limits.max_report_bytes;
        }
        const hal_status_t status = validate(large_report);
        /* The propertyNames keyword exceeds an eight-byte value limit. */
        const bool limited =
            mode >= 3 && large_report->limits.max_value_bytes < 13;
        const bool matches =
            limited
                ? status == HAL_EOVERFLOW && reports(large_report) == 0
                : status == HAL_ECONFIG && reports(large_report) == 1 &&
                      same(&report_at(large_report, 0)->code, rules[mode]) &&
                      jh_span_equal(
                          jh_string_span(&report_at(large_report, 0)->message),
                          jh_string_span(&expected));
        if (!matches)
          (void)fprintf(
              stderr,
              "large report: mode=%zu entries=%zu depth=%zu value=%zu "
              "status=%s reports=%zu expected=%zu actual=%zu prefix=%.80s\n",
              mode, entries, large_report->limits.max_ref_depth,
              large_report->limits.max_value_bytes,
              hal_status_to_string(status), reports(large_report),
              jb_string_size(&expected),
              reports(large_report)
                  ? jb_string_size(&report_at(large_report, 0)->message)
                  : 0,
              reports(large_report)
                  ? jb_string_data(&report_at(large_report, 0)->message)
                  : "");
        require(matches,
                "large const/enum diagnostic fits the advertised workspace");
        cleanup();
      }
    }
  }
  jb_string_destroy(&expected);
  jb_string_destroy(&type_list);
  cleanup();
}
static hal_status_t load_other(void *user, jb_json_span_t base,
                               jb_json_span_t ref,
                               jh_json_schema_source_t *out) {
  require(jh_span_equal(base, jh_span("root.schema.json")) &&
              jh_span_equal(ref, jh_span("other.schema.json")),
          "loader receives document reference without fragment");
  document *other = user;
  const jh_json_schema_source_t source = {{"other.schema.json", 17},
                                          &other->doc};
  *out = source;
  return HAL_OK;
}
static void references_and_limits(void) {
  document other = {0};
  document_init(&other, "{\"$defs\":{\"value\":{\"minimum\":2}}}");
  run *external =
      new_run("{\"allOf\":[{\"$ref\":\"other.schema.json#/$defs/"
              "value\"},{\"$ref\":\"other.schema.json#/$defs/value\"}]}",
              "1");
  external->limits.max_schema_nodes =
      external->schema.doc.token_count + other.doc.token_count;
  require(validate_with(external, collect, NULL, load_other, &other) ==
                  HAL_ECONFIG &&
              reports(external) == 2,
          "unique external documents counted once");
  if (reports(external) == 2)
    require(same(&report_at(external, 0)->uri, "other.schema.json") &&
                same(&report_at(external, 0)->rule, "/$defs/value/minimum"),
            "external rule identity");
  run *unknown =
      new_run("{\"$defs\":{\"unused\":{\"dependentRequired\":{}}}}", "0");
  require(validate(unknown) == HAL_EUNSUPPORTED && reports(unknown) == 0,
          "unused unsupported keyword fails explicitly");
  run *cycle = new_run("{\"$ref\":\"#\"}", "0");
  require(validate(cycle) == HAL_EINVAL && reports(cycle) == 0,
          "non-progressing reference cycle rejected");
  run *fragment =
      new_run("{\"$defs\":{\"\":false},\"$ref\":\"#/%24defs/\"}", "0");
  require(validate(fragment) == HAL_ECONFIG && reports(fragment) == 1 &&
              same(&report_at(fragment, 0)->rule, "/$defs/"),
          "decoded URI fragment retains empty pointer token");
  run *recursive = new_run(
      "{\"$defs\":{\"child\":{\"type\":[\"null\",\"object\"],\"properties\":{"
      "\"child\":{\"$ref\":\"#/$defs/child\"}}}},\"$ref\":\"#/$defs/child\"}",
      "{\"child\":{\"child\":null}}");
  require(validate(recursive) == HAL_OK,
          "recursive schema progresses through instance children");
  run *limited = new_run("{\"required\":[\"a\",\"b\"]}", "{}");
  limited->limits.max_reports = 1;
  require(validate(limited) == HAL_EOVERFLOW && reports(limited) == 0,
          "report exhaustion publishes no prefix");
  limited->limits = defaults();
  limited->limits.max_work = 1;
  require(validate(limited) == HAL_EOVERFLOW && reports(limited) == 0,
          "work exhaustion publishes no prefix");
  limited->limits = defaults();
  limited->limits.max_pointer_bytes = 0;
  require(validate(limited) == HAL_EOVERFLOW && reports(limited) == 0,
          "pointer exhaustion publishes no truncated report");
  /* Keep the full backtracking search small if the match limit is broken. */
  run *regex = new_run("{\"pattern\":\"(a+)+$\"}", "\"aaaaaaaaaaaaaaaa!\"");
  regex->limits.regex_match_limit = 10;
  require(validate(regex) == HAL_EOVERFLOW && reports(regex) == 0,
          "regex exhaustion differs from no match");
  char long_subject[1028];
  long_subject[0] = '"';
  memset(long_subject + 1, 'a', 1024);
  memcpy(long_subject + 1025, "!\"", 3);
  run *restarts = new_run("{\"pattern\":\"a+b\"}", long_subject);
  restarts->limits.max_work = 1000000;
  require(validate(restarts) == HAL_EOVERFLOW && reports(restarts) == 0,
          "unanchored restarts charge repeated scans to the global budget");
  run *compiled = new_run("{\"pattern\":\"(?:ab){200}\"}", "\"\"");
  compiled->limits.max_regex_bytes = 4096;
  require(validate(compiled) == HAL_EOVERFLOW && reports(compiled) == 0,
          "compiled bytecode capacity is a limit error");
  run *expansion = new_run("{\"pattern\":\"(?:ab){1000}\"}", "\"\"");
  expansion->limits.max_work = 10000;
  require(validate(expansion) == HAL_EOVERFLOW && reports(expansion) == 0,
          "regex allocation bytes charge compile expansion to global work");
  char deep_pattern[96];
  memcpy(deep_pattern, "{\"pattern\":\"", 12);
  memset(deep_pattern + 12, '(', 40);
  deep_pattern[52] = 'a';
  memset(deep_pattern + 53, ')', 40);
  memcpy(deep_pattern + 93, "\"}", 3);
  run *compile_depth = new_run(deep_pattern, "\"a\"");
  require(validate(compile_depth) == HAL_EOVERFLOW &&
              reports(compile_depth) == 0,
          "compile recursion guard is a limit error");
  run *unsupported = new_run("{\"pattern\":\"\\\\w+\"}", "\"a\"");
  require(validate(unsupported) == HAL_EUNSUPPORTED,
          "unsupported Unicode regex class is explicit");
  static const char *const unsupported_classes[] = {
      "{\"pattern\":\"^[[:digit:]]$\"}", "{\"pattern\":\"^[]\\\\s]$\"}",
      "{\"pattern\":\"^[^]\\\\s]$\"}"};
  static const char *const values[] = {"\"5\"", "\"]\"", "\" \"",
                                       "\"\\u00a0\""};
  for (size_t i = 0; i < COUNTOF(unsupported_classes); ++i)
    for (size_t j = 0; j < COUNTOF(values); ++j) {
      run *unsupported_class = new_run(unsupported_classes[i], values[j]);
      require(validate(unsupported_class) == HAL_EUNSUPPORTED &&
                  reports(unsupported_class) == 0,
              "unsupported character class fails before matching");
    }
  run *escaped_class =
      new_run("{\"pattern\":\"^[\\\\]\\\\s]+$\"}", "\"] \\u00a0\"");
  require(validate(escaped_class) == HAL_OK,
          "escaped closing bracket keeps whitespace inside the class");
  document_destroy(&other);
  cleanup();
}
static hal_status_t return_status(void *user,
                                  const jh_json_schema_error_t *error) {
  (void)error;
  return *(hal_status_t *)user;
}
typedef struct {
  size_t calls;
} report_stop;
static hal_status_t stop_second_report(void *user,
                                       const jh_json_schema_error_t *error) {
  (void)error;
  report_stop *stop = user;
  return ++stop->calls == 2 ? HAL_EIO : HAL_OK;
}
static void callbacks_and_workspace(void) {
  run *bad = new_run("false", "0");
  require(validate_with(bad, NULL, NULL, NULL, NULL) == HAL_ECONFIG,
          "optional report preserves invalid result");
  require(validate_with(bad, jh_schema_test_unknown_status, NULL, NULL, NULL) ==
              HAL_EINTERNAL,
          "unknown numeric status from a C report callback");
  run *external = new_run("{\"$ref\":\"other.schema.json\"}", "0");
  require(validate_with(external, collect, NULL, jh_schema_test_unknown_load,
                        NULL) == HAL_EINTERNAL,
          "unknown numeric status from a C loader callback");
  const hal_status_t codes[] = {HAL_EIO, HAL_NONE, (hal_status_t)2};
  for (size_t i = 0; i < COUNTOF(codes); ++i) {
    hal_status_t code = codes[i];
    require(validate_with(bad, return_status, &code, NULL, NULL) ==
                (code == HAL_EIO ? HAL_EIO : HAL_EINTERNAL),
            "callback status normalization");
  }
  size_t n = 0;
  jh_json_schema_limits_t limits = defaults();
  setup(jh_json_schema_workspace_size(&limits, &n));
  void *scratch = malloc(n);
  if (scratch == NULL)
    setup(HAL_ENOMEM);
  const jh_json_schema_source_t schema = {{"root", 4}, &bad->schema.doc};
  const jb_json_workspace_t workspace = {scratch, n - 1};
  require(jh_json_schema_validate(&bad->instance.doc, &schema, NULL, NULL, NULL,
                                  NULL, workspace, &limits) == HAL_EOVERFLOW,
          "undersized workspace");
  /* A stopped reporter must leave caller-owned scratch reusable. */
  run *multiple = new_run("{\"required\":[\"z\",\"a\"]}", "{}");
  const jh_json_schema_source_t multiple_schema = {{"root", 4},
                                                   &multiple->schema.doc};
  const jb_json_workspace_t reused = {scratch, n};
  report_stop stop = {0};
  require(jh_json_schema_validate(&multiple->instance.doc, &multiple_schema,
                                  NULL, NULL, stop_second_report, &stop, reused,
                                  &limits) == HAL_EIO &&
              stop.calls == 2,
          "report callback failure stops and propagates");
  require(jh_json_schema_validate(&multiple->instance.doc, &multiple_schema,
                                  NULL, NULL, collect, &multiple->reports,
                                  reused, &limits) == HAL_ECONFIG &&
              reports(multiple) == 2 &&
              same(&report_at(multiple, 0)->at, "/a") &&
              same(&report_at(multiple, 1)->at, "/z"),
          "scratch reused after report failure preserves ordering");
  free(scratch);
  limits.max_schema_nodes = SIZE_MAX;
  n = 123;
  require(jh_json_schema_workspace_size(&limits, &n) == HAL_EOVERFLOW &&
              n == 123,
          "checked workspace arithmetic");
  cleanup();
}
int main(void) {
  equality_and_branches();
  references_and_limits();
  callbacks_and_workspace();
  require(heap_requests == 0,
          "validation never uses the default heap allocator");
  return failures == 0 ? 0 : 1;
}
