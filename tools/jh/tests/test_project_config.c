// jh_project_config_read on headers of a private directory: values, includes
// with #pragma once, #error, declarations, uncertainty, the int64_t range,
// invalid literals and the limits of jh where Python has none. A native test,
// so the gate also runs it under Valgrind and the sanitizers.
//
// Usage: jh_config_test <work dir> [<C compiler> <JaszczurHAL root>]
// With a GCC or Clang compiler the reader is also compared with the
// compiler's preprocessor (-E -dM): the equivalence headers below and every
// hal_project_config.h of the checkout outside tests/hardware, for each
// target selector and variant.
#include "jh_json_file.h"
#include "jh_project_config.h"
#include "jh_python_repr.h"
#include <jb/array.h>
#include <jb/file.h>
#include <jb/filesystem.h>
#include <jb/process.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
/* The failure of the last read or query. */
static jh_failure_t last;
/* Every path the test made: the strings stay where they are. */
static jb_vector_t paths;

static void expect(bool ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "project config: %s\n", what);
    ++failures;
  }
}

/* A step the test cannot go on without. */
static void must(hal_status_t status, const char *what) {
  if (status != HAL_OK) {
    fprintf(stderr, "project config: %s: %s %s\n", what,
            hal_status_to_string(status), jb_string_data(&last.message));
    exit(2);
  }
}

static jb_string_t text_make(void) {
  jb_string_t text = {0};
  must(jb_string_init(&text, jb_heap_allocator()), "string");
  return text;
}

/* The concatenation of the NULL-terminated arguments, kept in paths. */
static const char *joined(const char *first, ...) {
  jb_string_t text = text_make();
  va_list parts;
  va_start(parts, first);
  for (const char *part = first; part != NULL;
       part = va_arg(parts, const char *))
    must(jh_append(&text, part), "text");
  va_end(parts);
  must(jh_list_push(&paths, jh_string_span(&text)), "text");
  jb_string_destroy(&text);
  return jb_string_data(jh_list_at(&paths, jb_vector_size(&paths) - 1));
}

static const char *below(const char *directory, const char *name) {
  jb_string_t path = text_make();
  must(jh_absolute_path(name, directory, &path, &last), "path");
  const char *kept = joined(jb_string_data(&path), NULL);
  jb_string_destroy(&path);
  return kept;
}

static const char *write_bytes(const char *directory, const char *name,
                               const char *bytes, size_t length) {
  const char *path = below(directory, name);
  bool changed = false;
  expect(jb_file_write_atomic(path, bytes, length, false, &changed) == HAL_OK,
         name);
  return path;
}

static const char *write_file(const char *directory, const char *name,
                              const char *text) {
  return write_bytes(directory, name, text, strlen(text));
}

/* Read header with count -D definitions into the empty config. */
static hal_status_t read_with(jh_project_config_t *config, const char *header,
                              const char *const *definitions, size_t count,
                              const jh_config_limits_t *limits) {
  jb_vector_t list = {0};
  must(jh_list_init(&list), "definitions");
  for (size_t i = 0; i < count; ++i)
    must(jh_list_push(&list, jh_span(definitions[i])), "definitions");
  jh_project_config_init(config);
  const hal_status_t status =
      jh_project_config_read(header, &list, "", limits, config, &last);
  jh_list_destroy(&list);
  return status;
}

#define DEFINITIONS(...)                                                       \
  ((const char *const[]){__VA_ARGS__}),                                        \
      COUNTOF(((const char *const[]){__VA_ARGS__}))

/* A read that must succeed. */
static void load(jh_project_config_t *config, const char *header,
                 const char *const *definitions, size_t count) {
  must(read_with(config, header, definitions, count, NULL), header);
}

/* The status of reading header; the failure stays in last. */
static hal_status_t rejection(const char *header,
                              const char *const *definitions, size_t count,
                              const jh_config_limits_t *limits) {
  jh_project_config_t config = {0};
  const hal_status_t status =
      read_with(&config, header, definitions, count, limits);
  jh_project_config_destroy(&config);
  return status;
}

static bool message_has(const char *part) {
  return strstr(jb_string_data(&last.message), part) != NULL;
}

static bool text_is(const jb_string_t *text, const char *expected) {
  return jh_span_equal(jh_string_span(text), jh_span(expected));
}

/* Whether list holds exactly the NULL-terminated names, in order. */
static bool list_is(const jb_vector_t *list, ...) {
  va_list names;
  va_start(names, list);
  size_t index = 0;
  bool same = true;
  for (const char *name = va_arg(names, const char *); name != NULL;
       name = va_arg(names, const char *), ++index)
    same = same && list != NULL && index < jb_vector_size(list) &&
           text_is(jh_list_at(list, index), name);
  va_end(names);
  return same && index == (list == NULL ? 0 : jb_vector_size(list));
}

static const jh_config_macro_t *macro(const jh_project_config_t *config,
                                      const char *name) {
  return jh_project_config_find(config, jh_span(name));
}

static bool body_is(const jh_project_config_t *config, const char *name,
                    const char *body) {
  const jh_config_macro_t *item = macro(config, name);
  return item != NULL && text_is(&item->body, body);
}

static const jb_vector_t *uncertain(const jh_project_config_t *config,
                                    const char *name) {
  return jh_project_config_uncertain(config, jh_span(name));
}

/* The value of name in value, which the caller destroys; the status and the
 * failure of the query stay in last. */
static hal_status_t query(const jh_project_config_t *config, const char *name,
                          jh_config_value_t *value) {
  jh_config_value_init(value);
  return jh_project_config_value(config, jh_span(name), value, &last);
}

static bool integer_is(const jh_project_config_t *config, const char *name,
                       int64_t expected) {
  jh_config_value_t value = {0};
  const bool same = query(config, name, &value) == HAL_OK &&
                    value.kind == JH_CONFIG_INTEGER_VALUE &&
                    value.integer == expected;
  jh_config_value_destroy(&value);
  return same;
}

static bool kind_is(const jh_project_config_t *config, const char *name,
                    jh_config_integer_kind_t expected) {
  jh_config_value_t value = {0};
  const bool same =
      query(config, name, &value) == HAL_OK && value.kind == expected;
  jh_config_value_destroy(&value);
  return same;
}

/* Whether name has exactly one unresolved name, or none for NULL. */
static bool unresolved_is(const jh_project_config_t *config, const char *name,
                          const char *expected) {
  jh_config_value_t value = {0};
  const bool same = query(config, name, &value) == HAL_OK &&
                    list_is(&value.unresolved, expected, NULL);
  jh_config_value_destroy(&value);
  return same;
}

/* The next header feature at or after *index, which moves past it; NULL after
 * the last one. */
static const jh_config_macro_t *next_feature(const jh_project_config_t *config,
                                             size_t *index) {
  while (*index < jb_vector_size(&config->macros)) {
    const jh_config_macro_t *item =
        jb_vector_const_at(&config->macros, (*index)++);
    if (jh_config_macro_is_feature(item))
      return item;
  }
  return NULL;
}

static size_t feature_count(const jh_project_config_t *config) {
  size_t count = 0;
  for (size_t i = 0; next_feature(config, &i) != NULL;)
    ++count;
  return count;
}

/* Whether the features are exactly the NULL-terminated names, in order. */
static bool features_are(const jh_project_config_t *config, ...) {
  va_list names;
  va_start(names, config);
  bool same = true;
  const char *name = va_arg(names, const char *);
  const jh_config_macro_t *item = NULL;
  for (size_t i = 0; (item = next_feature(config, &i)) != NULL;) {
    same = same && name != NULL && text_is(&item->name, name);
    if (name != NULL)
      name = va_arg(names, const char *);
  }
  va_end(names);
  return same && name == NULL;
}

/* Whether both configurations define the same header features in the same
 * order. */
static bool features_match(const jh_project_config_t *left,
                           const jh_project_config_t *right) {
  for (size_t i = 0, j = 0;;) {
    const jh_config_macro_t *first = next_feature(left, &i);
    const jh_config_macro_t *second = next_feature(right, &j);
    if (first == NULL || second == NULL)
      return first == second;
    if (!jh_span_equal(jh_string_span(&first->name),
                       jh_string_span(&second->name)))
      return false;
  }
}

/* Whether both lists hold the same names in the same order. */
static bool lists_match(const jb_vector_t *left, const jb_vector_t *right) {
  bool same = jb_vector_size(left) == jb_vector_size(right);
  for (size_t i = 0; same && i < jb_vector_size(left); ++i)
    same = jh_span_equal(jh_string_span(jh_list_at(left, i)),
                         jh_string_span(jh_list_at(right, i)));
  return same;
}

static const jh_config_variant_t *variant(const jh_project_config_t *config,
                                          size_t index) {
  return jb_vector_const_at(&config->variants, index);
}

static void values_and_includes(const char *directory) {
  write_file(directory, "common.h",
             "#pragma once\n#ifdef SHARED\n#error read twice\n#endif\n"
             "#define SHARED 2000U // shared timeout\n");
  const char *header = write_file(
      directory, "hal_project_config.h",
      "#include \"common.h\"\n#include \"common.h\"\n"
      "#define JH_PROJECT_TARGETS(X) X(HAL_TARGET_RP2040) X(HAL_TARGET_MOCK)\n"
      "#define JH_PROJECT_VARIANTS(X) X(WIDE, \"Wide panel\", PANEL = 2, "
      "EXTRA)\n"
      "#define A (-7 / 2) /* C division */\n#define B (-7 % 2)\n"
      "#if PANEL == 2 && defined(EXTRA) ? 1 : (1 / 0)\n#define "
      "HAL_ENABLE_ST7796S\n"
      "#else\n#define HAL_ENABLE_ILI9341\n#endif\n"
      "#if defined(SDK_ONLY)\n#define HAL_ENABLE_WIFI\n#endif\n"
      "#define SIZE (FLASH_BYTES / 256)\n#define KIB(x) ((x) * 1024)\n");
  jh_project_config_t config = {0};
  load(&config, header, DEFINITIONS("HAL_TARGET_RP2040", "PANEL=2", "EXTRA"));
  expect(jb_vector_size(&config.files) == 2 && !config.stopped,
         "#pragma once reads the included file once");
  expect(body_is(&config, "SHARED", "2000U") &&
             integer_is(&config, "SHARED", 2000),
         "included value without its comment");
  expect(integer_is(&config, "A", -3) && integer_is(&config, "B", -1),
         "C division and remainder");
  expect(macro(&config, "HAL_ENABLE_ST7796S") != NULL &&
             macro(&config, "HAL_ENABLE_ILI9341") == NULL,
         "variant switch condition");
  expect(
      jb_vector_size(&config.targets) == 2 &&
          jb_vector_size(&config.variants) == 1 &&
          list_is(&variant(&config, 0)->definitions, "PANEL=2", "EXTRA", NULL),
      "declarations");
  expect(list_is(uncertain(&config, "HAL_ENABLE_WIFI"), "SDK_ONLY", NULL),
         "a name decided by an unknown identifier");
  expect(unresolved_is(&config, "SIZE", "FLASH_BYTES") &&
             kind_is(&config, "SIZE", JH_CONFIG_INTEGER_NONE),
         "a value built from an undefined name");
  const jh_config_macro_t *function = macro(&config, "KIB");
  expect(function != NULL && function->function_like &&
             jb_vector_size(&function->parameters) == 1,
         "function-like macro");
  expect(feature_count(&config) == 1, "header features");
  // The header first, in read order, then the included file.
  const size_t count = jb_vector_size(&config.directives);
  const jh_config_directive_t *directives =
      jb_vector_const_data(&config.directives);
  const jh_config_directive_t *kib = NULL;
  for (size_t i = 0; i < count; ++i)
    if (text_is(&directives[i].name, "KIB"))
      kib = &directives[i];
  expect(count == 10 && text_is(&directives[0].name, "JH_PROJECT_TARGETS") &&
             text_is(&directives[count - 1].name, "SHARED") &&
             text_is(&directives[count - 1].text, "2000U") &&
             directives[count - 1].line == 5 && !directives[count - 1].undef &&
             kib != NULL && text_is(&kib->text, "(x) ((x) * 1024)") &&
             kib->line == 16,
         "directives of every file read, in any branch");
  jh_project_config_destroy(&config);
  // A redefinition replaces the value where the name was defined first.
  load(&config,
       write_file(directory, "redefined.h",
                  "#define FIRST 1\n#define SECOND 2\n#define FIRST 3\n"),
       NULL, 0);
  const jh_config_macro_t *macros = jb_vector_const_data(&config.macros);
  expect(
      jb_vector_size(&config.macros) == 2 &&
          text_is(&macros[0].name, "FIRST") && text_is(&macros[0].body, "3") &&
          text_is(&macros[1].name, "SECOND") && integer_is(&config, "FIRST", 3),
      "a redefinition keeps the place of the name");
  jh_project_config_destroy(&config);
}

typedef struct {
  const char *text, *part;
} rejected_t;

static void stop_and_rejections(const char *directory) {
  const char *stopped =
      write_file(directory, "stop.h",
                 "#define BEFORE 1\n#if 1\n#error \"stop here\"\n"
                 "#define AFTER 1\n");
  jh_project_config_t config = {0};
  load(&config, stopped, NULL, 0);
  expect(config.stopped && text_is(&config.error_message, "\"stop here\"") &&
             macro(&config, "AFTER") == NULL,
         "#error stops the read");
  jh_project_config_destroy(&config);
  // A name that runs into a digit is no directive, as in the Python reader.
  load(&config,
       write_file(directory, "digit.h", "#error1 never\n#define AFTER 1\n"),
       NULL, 0);
  expect(!config.stopped && macro(&config, "AFTER") != NULL,
         "a directive name followed by a digit");
  jh_project_config_destroy(&config);
  static const rejected_t rejected[] = {
      {"#include <stdint.h>\n", "only #include"},
      {"#if 1\n#define A\n", "unterminated #if"},
      {"#endif\n", "#endif without #if"},
      {"#define F(x) x\n#if F(1)\n#endif\n", "function-like macro F"},
      {"#if 1 +\n#endif\n", "expected operand"},
      {"#if 1 && (1 / 0)\n#endif\n", "division by zero"},
      {"#if 1 @\n#endif\n", "cannot evaluate ' @'"},
      {"#if FOO\n#define JH_PROJECT_VARIANTS(X) X(A, \"A\", B)\n#endif\n",
       "unconditionally"},
      {"#define JH_PROJECT_VARIANTS(X) X(bench, \"Bench\", B)\n",
       "capital letters"},
      {"#defin\xc3\xa9 X\n", "non-ASCII character after #defin"},
  };
  for (size_t i = 0; i < COUNTOF(rejected); ++i) {
    const hal_status_t status = rejection(
        write_file(directory, "rejected.h", rejected[i].text), NULL, 0, NULL);
    expect(status == HAL_ECONFIG && last.exit_code == 2 &&
               message_has(rejected[i].part),
           rejected[i].part);
  }
  // A rejection names its code and place apart from the message.
  expect(rejection(write_file(directory, "rejected.h", "#endif\n"), NULL, 0,
                   NULL) == HAL_ECONFIG &&
             text_is(&last.code, "JH-CFG-SCOPE") &&
             text_is(&last.source,
                     joined(below(directory, "rejected.h"), ":1", NULL)),
         "code and place of a rejection");
  expect(rejection(stopped, DEFINITIONS("A =1"), NULL) == HAL_ECONFIG &&
             text_is(&last.message,
                     "predefined: [JH-CFG-VALUE] invalid definition 'A =1'"),
         "invalid predefined definition");
}

typedef struct {
  const char *condition, *detail;
} range_t;

static void range_and_literals(const char *directory) {
  const char *wide =
      write_file(directory, "wide.h",
                 "#define BIG 0xFFFFFFFFFFFFFFFFull\n"
                 "#define LOW (-9223372036854775807 - 1)\n"
                 "#if 0 && 0xFFFFFFFFFFFFFFFF\n#define SKIPPED\n#endif\n");
  jh_project_config_t config = {0};
  load(&config, wide, NULL, 0);
  expect(kind_is(&config, "BIG", JH_CONFIG_INTEGER_OUT_OF_RANGE),
         "a value outside int64_t has no integer");
  expect(integer_is(&config, "LOW", INT64_MIN), "INT64_MIN");
  expect(macro(&config, "SKIPPED") == NULL, "an operand C skips");
  jh_project_config_destroy(&config);
  expect(rejection(write_file(directory, "overflow.h", "#if 1 << 63\n#endif\n"),
                   NULL, 0, NULL) == HAL_EOVERFLOW &&
             last.exit_code == 4 &&
             message_has("1 << 63 exceeds the signed 64-bit range"),
         "a computed condition outside int64_t");
  // Every computed operation outside int64_t, with its operands.
  static const range_t ranges[] = {
      {"0xFFFFFFFFFFFFFFFF > 0", "integer 0xFFFFFFFFFFFFFFFF"},
      {"9223372036854775807 + 1", "9223372036854775807 + 1"},
      {"(-9223372036854775807 - 1) - 1", "-9223372036854775808 - 1"},
      {"-(-9223372036854775807 - 1)", "-(-9223372036854775808)"},
      {"4294967296 * 4294967296", "4294967296 * 4294967296"},
      {"1 << 63", "1 << 63"},
      {"(-9223372036854775807 - 1) / -1", "-9223372036854775808 / -1"},
  };
  for (size_t i = 0; i < COUNTOF(ranges); ++i) {
    const char *header = write_file(
        directory, "range.h",
        joined("#if ", ranges[i].condition, "\n#define HIT 1\n#endif\n", NULL));
    const hal_status_t status = rejection(header, NULL, 0, NULL);
    expect(status == HAL_EOVERFLOW && last.exit_code == 4 &&
               text_is(&last.message,
                       joined(header, ":1: [JH-CFG-SCOPE] ", ranges[i].detail,
                              " exceeds the signed 64-bit range of jh", NULL)),
           ranges[i].condition);
  }
  load(&config,
       write_file(directory, "exact.h",
                  "#if (1 << 62) * -2 == -9223372036854775807 - 1 && "
                  "(-1 << 63) < 0 && (-9223372036854775807 - 1) % -1 == 0 && "
                  "-5 >> 1 == -3 && 1 >> 64 == 0 && -1 >> 99 == -1\n"
                  "#define HIT 1\n#endif\n"),
       NULL, 0);
  expect(macro(&config, "HIT") != NULL, "edges of the int64_t range");
  jh_project_config_destroy(&config);
  // Python's int(text, 8) raises ValueError, which no reader turns into "no
  // value", also in a macro the build only reads.
  load(&config, write_file(directory, "octal.h", "#define BAD 09\n"), NULL, 0);
  jh_config_value_t value = {0};
  expect(query(&config, "BAD", &value) == HAL_EPROTO && message_has("'09'"),
         "invalid octal literal");
  jh_config_value_destroy(&value);
  jh_project_config_destroy(&config);
}

/* count copies of byte. */
static const char *repeated(char byte, size_t count) {
  jb_string_t text = text_make();
  must(jb_string_resize(&text, count, byte), "text");
  const char *kept = joined(jb_string_data(&text), NULL);
  jb_string_destroy(&text);
  return kept;
}

/* "#define M<n> M<n-1> + M<n-1>" for n from 1 to levels: M<levels> reaches
 * M0 on 2^levels paths. With twice false, "#define M<n> M<n-1>": one path
 * that nests levels + 1 macros. */
static const char *macro_chain(int levels, bool twice) {
  jb_string_t text = text_make();
  for (int level = 1; level <= levels; ++level) {
    char line[64];
    const int written =
        twice ? snprintf(line, sizeof line, "#define M%d M%d + M%d\n", level,
                         level - 1, level - 1)
              : snprintf(line, sizeof line, "#define M%d M%d\n", level,
                         level - 1);
    must(written > 0 && (size_t)written < sizeof line ? HAL_OK : HAL_EOVERFLOW,
         "chain");
    must(jh_append(&text, line), "chain");
  }
  const char *chain = joined(jb_string_data(&text), NULL);
  jb_string_destroy(&text);
  return chain;
}

static void work_limits(const char *directory) {
  expect(rejection(write_file(directory, "nested.h",
                              joined("#if ", repeated('(', 300), "1",
                                     repeated(')', 300), "\n#endif\n", NULL)),
                   NULL, 0, NULL) == HAL_EOVERFLOW &&
             message_has("nests more"),
         "condition nesting limit");
  // Each macro uses the previous one twice: 2^40 paths, 40 macros. M0 stays
  // unresolved in these headers, so nothing expands them: the reader only
  // tracks what each macro depends on.
  const char *chain = macro_chain(40, true);
  jh_project_config_t sdk = {0};
  load(&sdk,
       write_file(directory, "chain.h",
                  joined("#define M0 SDK_VALUE\n", chain,
                         "#if 1\n#elif M40\n#endif\n", NULL)),
       NULL, 0);
  expect(unresolved_is(&sdk, "M40", "SDK_VALUE") &&
             kind_is(&sdk, "M40", JH_CONFIG_INTEGER_NONE) &&
             unresolved_is(&sdk, "M39", "SDK_VALUE"),
         "dependency tracking scans each macro once");
  jh_project_config_destroy(&sdk);
  // Dependency tracking alone spends the query budget too.
  const jh_config_limits_t hundred = {100};
  jh_project_config_t tracked = {0};
  must(read_with(&tracked,
                 write_file(directory, "tracked.h",
                            joined("#define M0 SDK_VALUE\n", chain, NULL)),
                 NULL, 0, &hundred),
       "tracked.h");
  expect(unresolved_is(&tracked, "M2", "SDK_VALUE"),
         "a small dependency question");
  jh_config_value_t value = {0};
  expect(query(&tracked, "M40", &value) == HAL_EOVERFLOW &&
             jb_vector_size(&value.unresolved) == 0,
         "dependency budget status");
  jh_config_value_destroy(&value);
  expect(unresolved_is(&tracked, "M2", "SDK_VALUE"),
         "a dependency question after an exceeded one");
  jh_project_config_destroy(&tracked);
  // Expansion visits every path. M16 and M15 need far more than 5000 tokens,
  // yet their whole expansion stays small: a reader that stops charging the
  // budget fails these checks at once instead of allocating without an end.
  const char *expanded = macro_chain(16, true);
  const jh_config_limits_t small = {5000};
  expect(
      rejection(write_file(directory, "exponential.h",
                           joined("#define M0 1\n", expanded,
                                  "#if M16\n#endif\n", NULL)),
                NULL, 0, &small) == HAL_EOVERFLOW &&
          message_has("expansion and dependency tracking exceed 5000 tokens"),
      "expansion work limit");
  // Each query has a budget of its own: the queries that exceed it fail
  // alone, and a constant reads the same before and after them.
  static const char constant[] = "#define HAL_RP_CORE1_STACK_SIZE 2048\n";
  for (int constant_first = 0; constant_first < 2; ++constant_first) {
    const char *source =
        constant_first != 0
            ? joined(constant, "#define M0 1\n", expanded, NULL)
            : joined("#define M0 1\n", expanded, constant, NULL);
    jh_project_config_t queries = {0};
    must(read_with(&queries, write_file(directory, "queries.h", source), NULL,
                   0, &small),
         "queries.h");
    for (int round = 0; round < 3; ++round) {
      static const char *const expensive[] = {"M16", "M15"};
      for (size_t i = 0; i < COUNTOF(expensive); ++i) {
        expect(query(&queries, expensive[i], &value) == HAL_EOVERFLOW &&
                   message_has("exceed 5000 tokens"),
               "query budget");
        jh_config_value_destroy(&value);
      }
      expect(integer_is(&queries, "HAL_RP_CORE1_STACK_SIZE", 2048) &&
                 integer_is(&queries, "M3", 8),
             "queries do not share a budget");
    }
    jh_project_config_destroy(&queries);
  }
  expect(rejection(write_file(directory, "invalid.h", "\xff\n"), NULL, 0,
                   NULL) == HAL_EPROTO &&
             last.exit_code == 2,
         "invalid UTF-8");
  // A condition may nest 1000 macros, the depth of the expansion: M999 with
  // M0 is the last one that fits. The whole expansion is one token a level.
  const char *ladder = macro_chain(1000, false);
  jh_project_config_t nested = {0};
  load(&nested,
       write_file(directory, "ladder.h",
                  joined("#define M0 1\n", ladder,
                         "#if M999\n#define REACHED\n#endif\n", NULL)),
       NULL, 0);
  expect(macro(&nested, "REACHED") != NULL, "1000 nested macros");
  jh_project_config_destroy(&nested);
  expect(rejection(write_file(directory, "ladder.h",
                              joined("#define M0 1\n", ladder,
                                     "#if M1000\n#endif\n", NULL)),
                   NULL, 0, NULL) == HAL_EOVERFLOW &&
             last.exit_code == 4 &&
             message_has("macros nest more than 1000 levels"),
         "macro nesting limit");
  // The header may include 16 files one inside the other.
  for (int level = 1; level <= 17; ++level) {
    char name[32], next[64];
    (void)snprintf(name, sizeof name, "deep%d.h", level);
    (void)snprintf(next, sizeof next, "#include \"deep%d.h\"\n", level + 1);
    write_file(directory, name, level < 16 ? next : "#define DEEPEST 1\n");
  }
  const char *outer = write_file(directory, "deep.h", "#include \"deep1.h\"\n");
  load(&nested, outer, NULL, 0);
  expect(macro(&nested, "DEEPEST") != NULL &&
             jb_vector_size(&nested.files) == 17,
         "16 nested includes");
  jh_project_config_destroy(&nested);
  write_file(directory, "deep16.h", "#include \"deep17.h\"\n");
  expect(rejection(outer, NULL, 0, NULL) == HAL_ECONFIG &&
             last.exit_code == 2 && message_has("includes nest too deeply"),
         "include depth limit");
}

// ── Cases of the former Python reader tests ─────────────────────────────────

static const char defaults_header[] =
    "#pragma once\n"
    "#ifndef HAL_RP_FLASH_EEPROM_SIZE\n"
    "#define HAL_RP_FLASH_EEPROM_SIZE (8 * 1024)\n"
    "#endif\n"
    "#define BASE 4\n"
    "#if BASE * 2 == 8 && !defined(MISSING)\n"
    "#define HAL_ENABLE_KV\n"
    "#elif 1\n"
    "#define HAL_ENABLE_I2C\n"
    "#endif\n"
    "#if defined(HAL_TARGET_RP2350_ARM) || defined HAL_TARGET_RP2040\n"
    "#define HAL_RP_CORE0_STACK_SIZE (BASE * 1024)\n"
    "#else\n"
    "#define HAL_RP_CORE0_STACK_SIZE 2048\n"
    "#endif\n"
    "#ifdef HAL_ENABLE_KV\n"
    "#undef BASE\n"
    "#define BASE 5\n"
    "#endif\n"
    "#if 0\n"
    "#if 1 / 0\n"
    "#define NEVER\n"
    "#endif\n"
    "#elif BASE == 5\n"
    "#define HAL_ENABLE_SPI 1\n"
    "#endif\n"
    "#if 1\n"
    "#define FIRST_TAKEN\n"
    "#elif 0\n"
    "#define NEVER_A\n"
    "#elif 1\n"
    "#define NEVER_B\n"
    "#else\n"
    "#define NEVER_C\n"
    "#endif\n"
    "#define KEPT 1\n"
    "#if 0\n"
    "#undef KEPT\n"
    "#endif\n";

static const char variants_header[] =
    "#pragma once\n"
    "#define JH_PROJECT_VARIANTS(X) \\\n"
    "  X(PROBE, \"Smoke test\", PROBE_ONLY=1) \\\n"
    "  X(WIDE, \"Wide panel\", PANEL = 2, EXTRA)\n"
    "#if !PROBE_ONLY\n"
    "#define HAL_ENABLE_DMA_PWM_AUDIO\n"
    "#endif\n"
    "#if PANEL == 2\n"
    "#define HAL_ENABLE_ST7796S\n"
    "#else\n"
    "#define HAL_ENABLE_ILI9341\n"
    "#endif\n"
    "#if defined(EXTRA) && PANEL > 1 ? 1 : 0\n"
    "#define HAL_ENABLE_APP_TASK1\n"
    "#endif\n"
    "#if PANEL == 2 && !defined(HAL_TARGET_RP2350_ARM)\n"
    "#error \"wide panel needs RP2350\"\n"
    "#endif\n";

static const char literals_header[] =
    "#pragma once\n"
    "#if 0 && (1 / 0)\n"
    "#define A 1\n"
    "#endif\n"
    "#if 1 || (1 % 0)\n"
    "#define B 1\n"
    "#endif\n"
    "#if 1 ? 2 : (1 / 0)\n"
    "#define C 1\n"
    "#endif\n"
    "#if 0 ? (1 / 0) : 0\n"
    "#define D 1\n"
    "#endif\n"
    "#if 1u && 0x10 == 16 && 'a' == 97 && 0b11 == 3\n"
    "#define HAL_ENABLE_CRC\n"
    "#endif\n"
    "#if 0 && (1 << -1)\n"
    "#define E 1\n"
    "#endif\n"
    "#if 1 ? 1 : (1 >> -1)\n"
    "#define F 1\n"
    "#endif\n";

static const char arithmetic_header[] =
    "#pragma once\n"
    "#define A (-7 / 2) // C truncates\n"
    "#define B (-7 % 2) /* the sign of the dividend */\n"
    "#define C (1 << 4 | 0x0F & 0b1010)\n"
    "#define D 'A'\n"
    "#if A == -3 && B == -1 && C == 26 && D == 65 && 010 == 8 && 10u == 10\n"
    "#define HAL_ENABLE_CRC\n"
    "#endif\n"
    "#if ~0 == -1 && !(3 > 4) && (2 >= 2) && (1 != 2)\n"
    "#define HAL_ENABLE_TIME\n"
    "#endif\n"
    "#define SELF (SELF + 1)\n"
    "#if SELF == 1\n"
    "#define SELF_IS_A_NAME\n"
    "#endif\n";

typedef struct {
  const char *name, *text;
} equivalence_t;

static const equivalence_t equivalence[] = {
    {"defaults_and_conditions", defaults_header},
    {"variant_switches", variants_header},
    {"short_circuit_and_literals", literals_header},
    {"arithmetic", arithmetic_header}};

/* Sets of -D definitions, each ended by NULL. */
static const char *const predefined_sets[][4] = {
    {"HAL_TARGET_RP2040", NULL},
    {"HAL_TARGET_RP2350_ARM", NULL},
    {"HAL_TARGET_STM32G474", "PROBE_ONLY=1", NULL},
    {"HAL_TARGET_RP2350_ARM", "PANEL=2", "EXTRA", NULL},
    {"HAL_TARGET_RP2040", "PANEL=2", NULL}};

static void rejected_declarations(const char *directory) {
  static const rejected_t rejected[] = {
      {"#ifndef G\n#define G\n#else\n#define JH_PROJECT_VARIANTS(X) X(A, "
       "\"A\", B)\n#endif\n",
       "unconditionally"},
      {"#ifndef G\n#define G\n#endif\n#ifndef H\n#define "
       "JH_PROJECT_VARIANTS(X) X(A, \"A\", B)\n#endif\n",
       "unconditionally"},
      {"#ifndef G\n#define OTHER\n#define JH_PROJECT_VARIANTS(X) X(A, \"A\", "
       "B)\n#endif\n",
       "unconditionally"},
      {"#define JH_PROJECT_VARIANTS X(A, \"A\", B)\n", "one parameter"},
      {"#define JH_PROJECT_VARIANTS(X) X(A, \"A\")\n",
       "at least one definition"},
      {"#define JH_PROJECT_VARIANTS(X) X(A-B, \"A\", B)\n", "capital letters"},
      {"#define JH_PROJECT_VARIANTS(X) X(A, \"A\", B) X(A, \"Again\", C)\n",
       "must be unique"},
      {"#define JH_PROJECT_VARIANTS(X) X(A, A, B)\n", "string description"},
      {"#define JH_PROJECT_VARIANTS(X) X(Bench, \"Bench\", B)\n",
       "capital letters"},
      {"#define JH_PROJECT_VARIANTS(X) X(_BENCH, \"Bench\", B)\n",
       "capital letters"},
      {"#define JH_PROJECT_VARIANTS(X) X(1BENCH, \"Bench\", B)\n",
       "capital letters"},
      {"#define JH_PROJECT_VARIANTS(X) X(A, \"A\", HAL_TARGET_RP2040)\n",
       "invalid definition"},
      {"#define JH_PROJECT_VARIANTS(X) Y(a, \"A\", B)\n", "only X("},
      {"#define JH_PROJECT_TARGETS(X) X(HAL_TARGET_RP2040) "
       "X(HAL_TARGET_RP2040)\n",
       "listed once"},
  };
  for (size_t i = 0; i < COUNTOF(rejected); ++i)
    expect(rejection(write_file(directory, "rejected2.h", rejected[i].text),
                     NULL, 0, NULL) == HAL_ECONFIG &&
               message_has(rejected[i].part),
           rejected[i].part);
  // A declaration belongs to the project header, not to a file it includes.
  write_file(directory, "declared.h",
             "#define JH_PROJECT_VARIANTS(X) X(A, \"A\", B)\n");
  expect(rejection(
             write_file(directory, "rejected2.h", "#include \"declared.h\"\n"),
             NULL, 0, NULL) == HAL_ECONFIG &&
             message_has("unconditionally"),
         "a declaration in an included file");
}

static void former_python_cases(const char *directory) {
  jh_project_config_t config = {0}, other = {0};
  // Values the build reads are integers, evaluated the way C does.
  load(&config,
       write_file(directory, "arithmetic/hal_project_config.h",
                  arithmetic_header),
       NULL, 0);
  expect(integer_is(&config, "A", -3) && integer_is(&config, "B", -1),
         "C division");
  expect(integer_is(&config, "C", 26), "bitwise precedence");
  expect(macro(&config, "SELF_IS_A_NAME") != NULL,
         "a macro that names itself is a name there");
  jh_project_config_destroy(&config);
  const char *defaults_path =
      write_file(directory, "defaults/hal_project_config.h", defaults_header);
  load(&config, defaults_path, DEFINITIONS("HAL_TARGET_RP2040"));
  expect(integer_is(&config, "HAL_RP_FLASH_EEPROM_SIZE", 8192),
         "nested arithmetic");
  // BASE was redefined under a feature that MISSING decides, so neither the
  // reader nor CMake can know the stack size the compiler will use.
  expect(kind_is(&config, "HAL_RP_CORE0_STACK_SIZE", JH_CONFIG_INTEGER_NONE) &&
             unresolved_is(&config, "HAL_RP_CORE0_STACK_SIZE", "MISSING"),
         "a value behind an undecided redefinition");
  expect(features_are(&config, "HAL_ENABLE_KV", "HAL_ENABLE_SPI", NULL),
         "features are the header's own definitions");
  jh_project_config_destroy(&config);
  load(&config,
       write_file(directory, "later/hal_project_config.h",
                  "#define BASE 4\n#define SIZE (BASE * 1024)\n#undef BASE\n"
                  "#define BASE 5\n"),
       NULL, 0);
  expect(integer_is(&config, "SIZE", 5120) &&
             unresolved_is(&config, "SIZE", NULL),
         "a macro uses the later value");
  jh_project_config_destroy(&config);
  load(&config, defaults_path, DEFINITIONS("HAL_ENABLE_CRC"));
  expect(macro(&config, "HAL_ENABLE_CRC") != NULL &&
             !jh_config_macro_is_feature(macro(&config, "HAL_ENABLE_CRC")),
         "a -D feature is not a header feature");
  jh_project_config_destroy(&config);

  // Variants come from the X-macro, whatever the target; a formatted
  // "PANEL = 2" reaches the build as the -D form. CRLF reads the same.
  const char *variants_path =
      write_file(directory, "variants/hal_project_config.h", variants_header);
  load(&config, variants_path, NULL, 0);
  expect(
      jb_vector_size(&config.variants) == 2 &&
          text_is(&variant(&config, 0)->id, "PROBE") &&
          text_is(&variant(&config, 0)->description, "Smoke test") &&
          list_is(&variant(&config, 0)->definitions, "PROBE_ONLY=1", NULL) &&
          list_is(&variant(&config, 1)->definitions, "PANEL=2", "EXTRA", NULL),
      "variants");
  expect(uncertain(&config, "HAL_ENABLE_ST7796S") == NULL,
         "a declared variant switch is a known input");
  jh_project_config_destroy(&config);
  jb_string_t crlf = text_make();
  for (const char *c = variants_header; *c != '\0'; ++c)
    must(*c == '\n' ? jh_append(&crlf, "\r\n") : jb_string_append(&crlf, c, 1),
         "crlf");
  const char *crlf_path = write_file(
      directory, "variants-crlf/hal_project_config.h", jb_string_data(&crlf));
  jb_string_destroy(&crlf);
  for (int wide = 0; wide < 2; ++wide) {
    static const char *const definitions[] = {"HAL_TARGET_RP2350_ARM",
                                              "PANEL=2", "EXTRA"};
    const size_t count = wide != 0 ? COUNTOF(definitions) : 0;
    load(&config, variants_path, definitions, count);
    load(&other, crlf_path, definitions, count);
    expect(jb_vector_size(&other.variants) == 2 &&
               jb_vector_size(&config.variants) == 2 &&
               lists_match(&variant(&other, 1)->definitions,
                           &variant(&config, 1)->definitions) &&
               features_match(&other, &config),
           "a CRLF header reads the same");
    jh_project_config_destroy(&other);
    jh_project_config_destroy(&config);
  }
  load(&config, variants_path, DEFINITIONS("HAL_TARGET_RP2040", "PANEL=2"));
  expect(config.stopped &&
             strstr(jb_string_data(&config.error_message), "RP2350") != NULL,
         "variant #error");
  jh_project_config_destroy(&config);

  // The file's own include guard does not make a declaration conditional.
  load(&config,
       write_file(
           directory, "guarded/hal_project_config.h",
           "#ifndef HAL_PROJECT_CONFIG_H\n#define HAL_PROJECT_CONFIG_H\n"
           "#define JH_PROJECT_TARGETS(X) X(HAL_TARGET_RP2040)\n"
           "#define JH_PROJECT_VARIANTS(X) X(PROBE, \"Probe\", PROBE_ONLY=1)\n"
           "#if PROBE_ONLY\n#define HAL_ENABLE_CRC\n#endif\n#endif\n"),
       DEFINITIONS("HAL_TARGET_RP2040", "PROBE_ONLY=1"));
  expect(list_is(&config.targets, "HAL_TARGET_RP2040", NULL) &&
             jb_vector_size(&config.variants) == 1 &&
             macro(&config, "HAL_ENABLE_CRC") != NULL,
         "declarations inside an include guard");
  jh_project_config_destroy(&config);

  // Macros decided by an identifier the reader cannot see.
  load(&config,
       write_file(
           directory, "uncertain/hal_project_config.h",
           "#if defined(PICO_RP2040)\n#define HAL_RP_CORE1_STACK_SIZE 4096\n"
           "#endif\n#ifdef SDK_ONLY\n#define HAL_ENABLE_WIFI\n#else\n"
           "#define HAL_ENABLE_UART\n#endif\n#if defined(OTHER)\n"
           "#define LOCAL_ONLY 1\n#endif\n#undef HAL_RP_CORE1_STACK_SIZE\n"
           "#define HAL_RP_CORE1_STACK_SIZE 2048\n"
           "#ifdef HAL_ENABLE_KV\n#define WITH_KV 1\n#endif\n"),
       NULL, 0);
  expect(list_is(uncertain(&config, "HAL_ENABLE_WIFI"), "SDK_ONLY", NULL) &&
             list_is(uncertain(&config, "HAL_ENABLE_UART"), "SDK_ONLY", NULL) &&
             list_is(uncertain(&config, "LOCAL_ONLY"), "OTHER", NULL) &&
             uncertain(&config, "HAL_RP_CORE1_STACK_SIZE") == NULL,
         "dependencies of inactive, else and later exact definitions");
  // The build decides the features: their names are no unknown inputs.
  expect(uncertain(&config, "WITH_KV") == NULL,
         "a feature name is a known input");
  jh_project_config_destroy(&config);
  // A file read before the project header alone tells nothing about what
  // decides its names.
  jh_project_config_init(&config);
  must(jh_project_config_read(
           "", NULL, below(directory, "uncertain/hal_project_config.h"), NULL,
           &config, &last),
       "preinclude alone");
  expect(macro(&config, "HAL_ENABLE_UART") != NULL &&
             jb_vector_size(&config.dependencies) == 0,
         "a preinclude alone has no dependencies");
  jh_project_config_destroy(&config);

  // Values the compiler computes from names this configuration does not
  // define have no value here, also through a local alias.
  load(&config,
       write_file(directory, "sdk/hal_project_config.h",
                  "#define FLASH_BYTES PICO_FLASH_SIZE_BYTES\n"
                  "#define HAL_RP_FLASH_EEPROM_SIZE (FLASH_BYTES / 256)\n"
                  "#define HAL_RP_CORE1_STACK_SIZE (2u * 1024u)\n"
                  "#define HAL_EEPROM_TYPE EEPROM_TYPE_FLASH\n"
                  "#define KIB(x) ((x) * 1024)\n"
                  "#define HAL_RP_FLASH_LITTLEFS_SIZE KIB(64)\n"
                  "#define TEXT \"PICO_FLASH_SIZE_BYTES\"\n"),
       NULL, 0);
  expect(kind_is(&config, "HAL_RP_FLASH_EEPROM_SIZE", JH_CONFIG_INTEGER_NONE) &&
             unresolved_is(&config, "HAL_RP_FLASH_EEPROM_SIZE",
                           "PICO_FLASH_SIZE_BYTES") &&
             integer_is(&config, "HAL_RP_CORE1_STACK_SIZE", 2048) &&
             unresolved_is(&config, "HAL_EEPROM_TYPE", "EEPROM_TYPE_FLASH") &&
             unresolved_is(&config, "HAL_RP_FLASH_LITTLEFS_SIZE", "KIB") &&
             unresolved_is(&config, "TEXT", NULL),
         "unresolved values: SDK alias, C name, function-like macro, string");
  jh_project_config_destroy(&config);

  // A condition decided through a local alias depends on what the alias
  // expands to; numbers and character literals are not names.
  load(&config,
       write_file(directory, "aliased/hal_project_config.h",
                  "#define ON_RP2040 PICO_RP2040\n"
                  "#if ON_RP2040\n#define HAL_ENABLE_KV\n#endif\n"
                  "#if 1u && 0x10 == 16 && 'a' == 97\n#define "
                  "HAL_ENABLE_CRC\n#endif\n"),
       DEFINITIONS("HAL_TARGET_RP2040"));
  load(&other,
       write_file(directory, "relayed/hal_project_config.h",
                  "#if defined(SDK_SWITCH)\n#define LOCAL_SWITCH 1\n#endif\n"
                  "#if LOCAL_SWITCH\n#define HAL_ENABLE_KV\n#endif\n"),
       NULL, 0);
  expect(list_is(uncertain(&config, "HAL_ENABLE_KV"), "PICO_RP2040", NULL) &&
             list_is(uncertain(&other, "HAL_ENABLE_KV"), "SDK_SWITCH", NULL) &&
             macro(&config, "HAL_ENABLE_CRC") != NULL &&
             uncertain(&config, "HAL_ENABLE_CRC") == NULL,
         "conditions through an alias and a relayed switch");
  jh_project_config_destroy(&other);
  jh_project_config_destroy(&config);

  // C does not evaluate the operand a decided && or || skips, nor the ?:
  // branch it does not take.
  load(&config,
       write_file(
           directory, "short/hal_project_config.h",
           "#if 0 && (1 / 0)\n#define A\n#endif\n#if 1 || (1 % 0)\n#define B\n"
           "#endif\n#if 1 ? 2 : (1 / 0)\n#define C\n#endif\n"
           "#if 0 ? (1 / 0) : 0\n#define D\n#endif\n"),
       NULL, 0);
  expect(macro(&config, "A") == NULL && macro(&config, "B") != NULL &&
             macro(&config, "C") != NULL && macro(&config, "D") == NULL,
         "short-circuit evaluation");
  jh_project_config_destroy(&config);

  // Quoted includes resolve next to the including file; nothing else does.
  const char *shared =
      write_file(directory, "shared/common.h",
                 "#pragma once\n#define SHARED_TIMEOUT 2000U\n");
  load(&config,
       write_file(directory, "project/hal_project_config.h",
                  "#pragma once\n#include \"../shared/common.h\"\n"
                  "#include \"../shared/common.h\"\n"),
       NULL, 0);
  expect(body_is(&config, "SHARED_TIMEOUT", "2000U") &&
             jb_vector_size(&config.files) == 2 &&
             text_is(jh_list_at(&config.files, 1), shared),
         "relative include and its inputs");
  jh_project_config_destroy(&config);

  rejected_declarations(directory);

  load(&config, below(directory, "missing.h"), DEFINITIONS("HAL_TARGET_MOCK"));
  expect(jb_vector_size(&config.files) == 0 &&
             jb_vector_size(&config.variants) == 0 &&
             macro(&config, "HAL_TARGET_MOCK") != NULL,
         "a missing header is empty and predefined macros stay visible");
#ifndef _WIN32
  // A header the system cannot inspect is no missing header: here a name
  // longer than a file system stores.
  const char *unseen = below(directory, joined(repeated('x', 300), ".h", NULL));
  expect(rejection(unseen, NULL, 0, NULL) == HAL_EOVERFLOW &&
             last.exit_code == 4 && text_is(&last.code, "JH-CFG-SCOPE") &&
             message_has("cannot read"),
         "a header that cannot be inspected");
  jb_vector_t unseen_directives = {0};
  must(jh_config_directives_init(&unseen_directives), "directives");
  expect(jh_project_config_directives(unseen, &unseen_directives, &last) ==
                 HAL_EOVERFLOW &&
             last.exit_code == 4,
         "directives of a file that cannot be inspected");
  jh_config_directives_destroy(&unseen_directives);
#endif
  jh_project_config_destroy(&config);

  // Active negative shifts follow the GCC extension; see the preprocessor
  // comparison for GCC itself.
  load(&config,
       write_file(directory, "shifts/hal_project_config.h",
                  "#if (1 << -1) == 0 && (8 >> -1) == 16\n"
                  "#define GCC_NEGATIVE_SHIFT 1\n#endif\n"),
       NULL, 0);
  expect(macro(&config, "GCC_NEGATIVE_SHIFT") != NULL, "GCC negative shifts");
  jh_project_config_destroy(&config);
}

// ── Comparison with the compiler's preprocessor ─────────────────────────────

typedef struct {
  jb_string_t out, err;
} captured_t;

static hal_status_t capture(void *user, jb_process_stream_t stream,
                            const unsigned char *data, size_t length) {
  captured_t *text = user;
  return jb_string_append(stream == JB_PROCESS_STDOUT ? &text->out : &text->err,
                          (const char *)data, length);
}

/* text with every run of whitespace as one space and none at its ends. */
static void normalized(jb_json_span_t text, jb_string_t *out) {
  bool space = false;
  jb_string_clear(out);
  for (size_t i = 0; i < text.length; ++i) {
    const char c = text.data[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
        c == '\v') {
      space = jb_string_size(out) != 0;
      continue;
    }
    if (space)
      must(jb_string_append(out, " ", 1), "text");
    space = false;
    must(jb_string_append(out, &c, 1), "text");
  }
}

static bool contains(jb_json_span_t text, jb_json_span_t part) {
  for (size_t i = 0; i + part.length <= text.length; ++i)
    if (memcmp(text.data + i, part.data, part.length) == 0)
      return true;
  return false;
}

/* The macros of one preprocessor run: names and normalized values. */
typedef struct {
  jb_vector_t names, values;
} compiled_t;

static void compiled_destroy(compiled_t *macros) {
  jh_list_destroy(&macros->names);
  jh_list_destroy(&macros->values);
}

static const jb_string_t *compiled_value(const compiled_t *macros,
                                         jb_json_span_t name) {
  // The last definition of a name counts, as in the output of -dM.
  for (size_t i = jb_vector_size(&macros->names); i > 0; --i)
    if (jh_span_equal(jh_string_span(jh_list_at(&macros->names, i - 1)), name))
      return jh_list_at(&macros->values, i - 1);
  return NULL;
}

/* Macros the compiler defines after the header, or false when it stops.
 * count definitions; macros is released by the caller either way. */
static bool compiler_macros(const char *compiler, const char *work,
                            const char *header, const char *const *predefined,
                            size_t count, compiled_t *macros) {
  *macros = (compiled_t){0};
  must(jh_list_init(&macros->names), "macros");
  must(jh_list_init(&macros->values), "macros");
  const char *probe =
      write_file(work, "probe/probe.c", "#include \"hal_project_config.h\"\n");
  jb_string_t parent = text_make();
  must(jh_path_parent(header, &parent, &last), "header directory");
  jb_vector_t arguments = {0};
  must(jb_vector_init(&arguments, sizeof(const char *), jb_heap_allocator()),
       "arguments");
  const char *const fixed[] = {
      "-E", "-dM", "-x",
      "c",  probe, joined("-I", jb_string_data(&parent), NULL)};
  must(jb_vector_append(&arguments, fixed, COUNTOF(fixed)), "arguments");
  for (size_t i = 0; i <= count; ++i) {
    const char *word = i < count ? joined("-D", predefined[i], NULL) : NULL;
    must(jb_vector_append(&arguments, &word, 1), "arguments");
  }
  captured_t text = {text_make(), text_make()};
  jb_process_options_t options = {0};
  options.executable = compiler;
  options.arguments = jb_vector_const_data(&arguments);
  options.working_directory = work;
  options.timeout_ms = 60000;
  options.output = capture;
  options.context = &text;
  jb_process_result_t result = {0};
  must(jb_process_run(&options, &result), "cannot run the compiler");
  const bool defined = result.exit_code == 0;
  if (!defined)
    expect(strstr(jb_string_data(&text.err), "#error") != NULL,
           "the preprocessor failed without #error");
  const jb_json_span_t out = jh_string_span(&text.out);
  jb_string_t value = text_make();
  for (size_t begin = 0; defined && begin < out.length;) {
    const char *newline = memchr(out.data + begin, '\n', out.length - begin);
    const size_t end =
        newline != NULL ? (size_t)(newline - out.data) : out.length;
    const jb_json_span_t line = {out.data + begin, end - begin};
    begin = end + 1;
    if (line.length < 8 || memcmp(line.data, "#define ", 8) != 0)
      continue;
    size_t name_end = 8;
    while (name_end < line.length && line.data[name_end] != ' ' &&
           line.data[name_end] != '(')
      ++name_end;
    size_t rest = name_end;
    if (rest < line.length && line.data[rest] == '(') {
      const char *close = memchr(line.data + rest, ')', line.length - rest);
      rest = close != NULL ? (size_t)(close - line.data) + 1 : line.length;
    }
    const jb_json_span_t name = {line.data + 8, name_end - 8};
    const jb_json_span_t body = {line.data + rest, line.length - rest};
    normalized(body, &value);
    must(jh_list_push(&macros->names, name), "macros");
    must(jh_list_push(&macros->values, jh_string_span(&value)), "macros");
  }
  jb_string_destroy(&value);
  jb_string_destroy(&text.out);
  jb_string_destroy(&text.err);
  jb_vector_destroy(&arguments);
  jb_string_destroy(&parent);
  return defined;
}

static void differs(const char *label, const char *what) {
  fprintf(stderr, "project config: %s: %s\n", label, what);
  ++failures;
}

/* The reader and the preprocessor agree on #error and on every macro the
 * reader defines or the header or the definitions name. */
static void compare_with_compiler(const char *compiler, const char *work,
                                  const char *header, jb_json_span_t text,
                                  const char *const *predefined, size_t count,
                                  const char *label) {
  jh_project_config_t config = {0};
  compiled_t expected = {0};
  load(&config, header, predefined, count);
  const bool defined =
      compiler_macros(compiler, work, header, predefined, count, &expected);
  if (config.stopped != !defined)
    differs(label, "#error differs");
  jb_string_t joined_definitions = text_make(), body = text_make();
  for (size_t i = 0; i < count; ++i) {
    must(jh_append(&joined_definitions, predefined[i]), "definitions");
    must(jh_append(&joined_definitions, " "), "definitions");
  }
  // The names to compare: of the reader, and of the compiler where the
  // header or the definitions mention them.
  jb_vector_t names = {0};
  must(jh_list_init(&names), "names");
  for (size_t i = 0; defined && i < jb_vector_size(&config.macros); ++i)
    must(jh_list_push(
             &names, jh_string_span(&((const jh_config_macro_t *)
                                          jb_vector_const_at(&config.macros, i))
                                         ->name)),
         "names");
  for (size_t i = 0; defined && i < jb_vector_size(&expected.names); ++i) {
    const jb_json_span_t name = jh_string_span(jh_list_at(&expected.names, i));
    if ((contains(text, name) ||
         contains(jh_string_span(&joined_definitions), name)) &&
        !jh_list_contains(&names, name))
      must(jh_list_push(&names, name), "names");
  }
  for (size_t i = 0; config.stopped == !defined && i < jb_vector_size(&names);
       ++i) {
    const jb_json_span_t name = jh_string_span(jh_list_at(&names, i));
    const jh_config_macro_t *item = jh_project_config_find(&config, name);
    const jb_string_t *found = compiled_value(&expected, name);
    if (item != NULL && !item->function_like)
      normalized(jh_string_span(&item->body), &body);
    if (item == NULL || found == NULL ||
        (!item->function_like &&
         !jh_span_equal(jh_string_span(&body), jh_string_span(found)))) {
      fprintf(stderr, "project config: %s: %s differs from the preprocessor\n",
              label, name.data);
      ++failures;
    }
  }
  jh_list_destroy(&names);
  jb_string_destroy(&body);
  jb_string_destroy(&joined_definitions);
  compiled_destroy(&expected);
  jh_project_config_destroy(&config);
}

static hal_status_t list_entry(void *context, const char *name,
                               const jb_fs_info_t *info) {
  (void)info;
  return jh_list_push(context, jh_span(name));
}

/* Append the hal_project_config.h files below directory, whose path from the
 * checkout root is relative ("" for the root itself): not through a linked
 * directory, as recursive_directory_iterator, and not below tests/hardware,
 * whose fixtures the default tests do not read. */
static void find_headers(const char *directory, const char *relative,
                         jb_vector_t *found) {
  static const char *const skipped[] = {".build", ".git", "build",
                                        "third_party", "node_modules"};
  jb_vector_t names = {0};
  must(jh_list_init(&names), "listing");
  must(jb_fs_list(directory, list_entry, &names), directory);
  for (size_t i = 0; i < jb_vector_size(&names); ++i) {
    const char *name = jb_string_data(jh_list_at(&names, i));
    const char *path = below(directory, name);
    const char *inner =
        relative[0] != '\0' ? joined(relative, "/", name, NULL) : name;
    jb_fs_info_t entry = {0}, target = {0};
    // An entry removed since the listing is none; any other failure of the
    // search would hide headers from the comparison.
    const hal_status_t seen = jb_fs_status(path, false, &entry);
    if (seen == HAL_ENOENT)
      continue;
    must(seen, path);
    if (entry.kind == JB_FS_DIRECTORY) {
      bool skip = strcmp(inner, "tests/hardware") == 0;
      for (size_t j = 0; j < COUNTOF(skipped); ++j)
        skip = skip || strcmp(name, skipped[j]) == 0;
      if (!skip)
        find_headers(path, inner, found);
    } else if (strcmp(name, "hal_project_config.h") == 0 &&
               jb_fs_status(path, true, &target) == HAL_OK &&
               target.kind == JB_FS_REGULAR) {
      must(jh_list_push(found, jh_span(path)), "headers");
    }
  }
  jh_list_destroy(&names);
}

static size_t set_size(const char *const *set) {
  size_t count = 0;
  while (set[count] != NULL)
    ++count;
  return count;
}

static void preprocessor(const char *compiler, const char *root,
                         const char *directory) {
  for (size_t i = 0; i < COUNTOF(equivalence); ++i) {
    const char *header = write_file(directory,
                                    joined("equivalence/", equivalence[i].name,
                                           "/hal_project_config.h", NULL),
                                    equivalence[i].text);
    for (size_t j = 0; j < COUNTOF(predefined_sets); ++j)
      compare_with_compiler(compiler, directory, header,
                            jh_span(equivalence[i].text), predefined_sets[j],
                            set_size(predefined_sets[j]), equivalence[i].name);
  }
  // GCC accepts negative shift counts in active operands; Clang does not.
  compiled_t gcc = {0};
  if (compiler_macros(compiler, directory,
                      below(directory, "shifts/hal_project_config.h"), NULL, 0,
                      &gcc) &&
      compiled_value(&gcc, jh_span("__GNUC__")) != NULL &&
      compiled_value(&gcc, jh_span("__clang__")) == NULL)
    expect(compiled_value(&gcc, jh_span("GCC_NEGATIVE_SHIFT")) != NULL,
           "GCC preprocessor negative-shift behaviour changed");
  compiled_destroy(&gcc);

  // Every hal_project_config.h of the checkout outside tests/hardware, as each
  // target selector and variant reads it.
  static const char *const selectors[] = {
      "HAL_TARGET_RP2040",       "HAL_TARGET_RP2350_ARM",
      "HAL_TARGET_RP2350_RISCV", "HAL_TARGET_STM32G474",
      "HAL_TARGET_ESP32",        "HAL_TARGET_ESP32_S3",
      "HAL_TARGET_MOCK"};
  jb_vector_t headers = {0}, definitions = {0};
  must(jh_list_init(&headers), "headers");
  must(jb_vector_init(&definitions, sizeof(const char *), jb_heap_allocator()),
       "definitions");
  find_headers(root, "", &headers);
  jh_list_sort(&headers);
  expect(jb_vector_size(&headers) > 20,
         "too few project headers in the checkout");
  size_t builds = 0;
  for (size_t i = 0; i < jb_vector_size(&headers); ++i) {
    const char *header = jb_string_data(jh_list_at(&headers, i));
    jb_file_data_t text = {0};
    must(jb_file_read(header, 16 * 1024 * 1024, &text),
         "cannot read a project header");
    const jb_json_span_t source = {(const char *)text.data, text.length};
    jh_project_config_t declared = {0};
    load(&declared, header, NULL, 0);
    for (size_t s = 0; s < COUNTOF(selectors); ++s) {
      // The selector alone, then with the definitions of each variant.
      for (size_t v = 0; v <= jb_vector_size(&declared.variants); ++v) {
        jb_vector_clear(&definitions);
        must(jb_vector_append(&definitions, &selectors[s], 1), "definitions");
        const jb_vector_t *extra =
            v == 0 ? NULL : &variant(&declared, v - 1)->definitions;
        for (size_t d = 0; extra != NULL && d < jb_vector_size(extra); ++d) {
          const char *definition = jb_string_data(jh_list_at(extra, d));
          must(jb_vector_append(&definitions, &definition, 1), "definitions");
        }
        compare_with_compiler(compiler, directory, header, source,
                              jb_vector_const_data(&definitions),
                              jb_vector_size(&definitions), header);
        ++builds;
      }
    }
    jh_project_config_destroy(&declared);
    jb_file_release(&text);
  }
  printf("the reader matches the preprocessor on %zu headers, %zu builds\n",
         jb_vector_size(&headers), builds);
  jb_vector_destroy(&definitions);
  jh_list_destroy(&headers);
}

static bool repr_is(const char *text, const char *expected) {
  jb_string_t quoted = text_make();
  must(jh_python_repr_append(&quoted, jh_span(text)), "repr");
  const bool same = text_is(&quoted, expected);
  jb_string_destroy(&quoted);
  return same;
}

static void repr(void) {
  expect(repr_is("a'b", "\"a'b\"") && repr_is("a\"'b", "'a\"\\'b'"),
         "repr quotes");
  expect(repr_is("\t\x01\xc2\xa0\xc4\x85", "'\\t\\x01\\xa0\xc4\x85'"),
         "repr escapes");
}

int main(int argc, char **argv) {
  if (argc != 2 && argc != 4) {
    fputs("usage: jh_config_test <work directory> [<C compiler> "
          "<JaszczurHAL root>]\n",
          stderr);
    return 2;
  }
  jh_failure_init(&last);
  must(jh_list_init(&paths), "paths");
  const char *directory = below(argv[1], "project-config-test");
  write_bytes(directory, "marker", "", 0);
  values_and_includes(directory);
  stop_and_rejections(directory);
  range_and_literals(directory);
  work_limits(directory);
  former_python_cases(directory);
  repr();
  if (argc == 4)
    preprocessor(argv[2], argv[3], directory);
  jh_list_destroy(&paths);
  jh_failure_destroy(&last);
  if (failures != 0)
    return 1;
  puts("project configuration reader tests passed");
  return 0;
}
