#ifndef JH_PROJECT_CONFIG_H
#define JH_PROJECT_CONFIG_H

/** @file jh_project_config.h
 * Reader of a project's hal_project_config.h, ported from
 * scripts/project_config.py: it evaluates the header the way the C
 * preprocessor does for one set of -D definitions and reports the macros,
 * variants, targets, files read, an #error and the names it cannot see.
 *
 * Integer arithmetic is signed int64_t. A literal or a result outside that
 * range, in an operand the condition computes, fails with [JH-CFG-SCOPE]
 * where the Python reader computes an unbounded value; a macro value outside
 * the range has no integer (JH_CONFIG_INTEGER_OUT_OF_RANGE). Operands C does
 * not compute (after a decided && or ||, the ?: branch not taken) are parsed
 * but not computed, as in Python. Non-ASCII letters and digits are not part of
 * numbers and names, as in C.
 *
 * Files are named as Path.resolve() of Python names them: absolute, with the
 * symbolic links of existing parts replaced by their targets, also by a
 * target that is missing, and without a separator after the last part.
 *
 * C11. Every function that can fail returns its status. One that takes a
 * failure record fills it, which replaces what the record held: exit code 2
 * for a header the reader rejects (HAL_ECONFIG, HAL_EPROTO for invalid UTF-8
 * or literals), 3 for a file it cannot read and 4 for the int64_t range, a jh
 * limit (HAL_EOVERFLOW) or memory. A rejection carries its [JH-CFG-*] code
 * and place (jh_fail_coded). The functions without a record only build text
 * or JSON and return the status of the jhbase container or writer.
 */
#include "jh_failure.h"
#include <jb/json_writer.h>
#include <jb/map.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One #define, or a -D definition with source "predefined". */
typedef struct {
  jb_string_t name, body, source;
  /** Parameter names of a function-like macro, empty entries removed;
   * jb_string_t elements. */
  jb_vector_t parameters;
  bool function_like;
} jh_config_macro_t;

/** One #define or #undef of a file the reader read, in any branch. */
typedef struct {
  jb_string_t file, name;
  /** For #define, the text after the name: parameters and body. */
  jb_string_t text;
  size_t line;
  bool undef;
} jh_config_directive_t;

/** One entry of JH_PROJECT_VARIANTS(X). */
typedef struct {
  jb_string_t id, description, source;
  /** Definitions in the -D form (NAME or NAME=VALUE); jb_string_t elements. */
  jb_vector_t definitions;
} jh_config_variant_t;

/** The unknown identifiers that decide whether or how name is defined. */
typedef struct {
  jb_string_t name;
  /** Identifiers in code point order; jb_string_t elements. */
  jb_vector_t unknown;
} jh_config_dependency_t;

/** Work limit of one read: the tokens that expanding conditions and tracking
 * dependencies visit together. Each jh_project_config_value() on its result
 * has a budget of the same size of its own, so a query never pays for another
 * one. Exceeding a budget fails with HAL_EOVERFLOW (exit 4).
 */
typedef struct {
  size_t max_work;
} jh_config_limits_t;

/** The limits of jh: 1000000 tokens. */
jh_config_limits_t jh_config_default_limits(void);

/** Result of evaluating the header for one set of predefined macros. Start
 * with jh_project_config_init and release with jh_project_config_destroy;
 * the fields are read-only for the caller.
 */
typedef struct {
  /** jh_config_macro_t elements in definition order: a redefinition keeps
   * the place of the first definition, #undef removes the name and a later
   * #define appends it. */
  jb_vector_t macros;
  /** Private: the index in macros of each name, and the number of places
   * #undef left in macros while the files are read. */
  jb_map_t macro_names;
  size_t macro_gaps;
  /** jh_config_variant_t elements in declaration order. */
  jb_vector_t variants;
  /** Files read, resolved, in first-read order; jb_string_t elements. */
  jb_vector_t files;
  /** HAL_TARGET_* selectors of JH_PROJECT_TARGETS(X); jb_string_t elements. */
  jb_vector_t targets;
  /** jh_config_directive_t elements: #define/#undef of every file in files,
   * active or not, in file order; the whole file, also past an #error. */
  jb_vector_t directives;
  /** jh_config_dependency_t elements in the order of their names. */
  jb_vector_t dependencies;
  /** The header stopped on #error: its text and place. */
  bool stopped;
  jb_string_t error_message, error_source;
  /** Work budget of each jh_project_config_value() call. */
  size_t max_work;
} jh_project_config_t;

typedef enum {
  JH_CONFIG_INTEGER_NONE,        /**< Not an integer here. */
  JH_CONFIG_INTEGER_VALUE,       /**< integer holds the value. */
  JH_CONFIG_INTEGER_OUT_OF_RANGE /**< Outside the int64_t arithmetic of jh. */
} jh_config_integer_kind_t;

/** What the build reads of one macro: the names its value uses that the
 * configuration cannot resolve, and its integer value. Start with
 * jh_config_value_init and release with jh_config_value_destroy.
 */
typedef struct {
  jh_config_integer_kind_t kind;
  int64_t integer;
  /** Names in code point order; jb_string_t elements. */
  jb_vector_t unresolved;
} jh_config_value_t;

/** Start an empty configuration without allocating. config is required and
 * must be zero or destroyed. */
void jh_project_config_init(jh_project_config_t *config);
/** Release everything a configuration holds and reset it to zero. A zero
 * configuration is harmless. */
void jh_project_config_destroy(jh_project_config_t *config);

/** Evaluate header (empty: none) with -D definitions (NAME or NAME=VALUE in
 * UTF-8, jb_string_t elements; NULL: none). preinclude (empty: none) is read
 * first, as the hook loads jh_hardware.h before the project header. A header
 * that does not exist or is no regular file is an empty configuration; one
 * the system cannot inspect fails like one it cannot read (exit code 3,
 * memory 4). limits may be NULL for the defaults.
 * config must be empty; destroy it after success and after failure. Each
 * macro is scanned once per dependency question, so the work grows with the
 * size of the header, not with the number of paths between its macros. Of a
 * file only the lines with directives stay in memory while the files it
 * includes are read.
 */
hal_status_t
jh_project_config_read(const char *header, const jb_vector_t *definitions,
                       const char *preinclude, const jh_config_limits_t *limits,
                       jh_project_config_t *config, jh_failure_t *failure);

/** #define/#undef of one file, active or not, without evaluating it:
 * jh_config_directive_t elements appended to directives, which the caller
 * initialized. Nothing for a path that does not exist or is no regular file;
 * a path the system cannot inspect fails. Release the elements with
 * jh_config_directives_destroy.
 */
hal_status_t jh_project_config_directives(const char *path,
                                          jb_vector_t *directives,
                                          jh_failure_t *failure);
/** Start an empty list of jh_config_directive_t. */
hal_status_t jh_config_directives_init(jb_vector_t *directives);
/** Release the directives and the list. */
void jh_config_directives_destroy(jb_vector_t *directives);

/** The macro name defines, or NULL. The pointer is valid until the
 * configuration is destroyed. */
const jh_config_macro_t *
jh_project_config_find(const jh_project_config_t *config, jb_json_span_t name);
/** Identifiers deciding name that the reader cannot see (jb_string_t elements
 * in code point order), or NULL when there is none. */
const jb_vector_t *
jh_project_config_uncertain(const jh_project_config_t *config,
                            jb_json_span_t name);
/** Whether macro is a HAL feature definition made by the header itself. */
bool jh_config_macro_is_feature(const jh_config_macro_t *macro);

/** Start an empty value. */
void jh_config_value_init(jh_config_value_t *value);
/** Release the names of a value and reset it to zero. */
void jh_config_value_destroy(jh_config_value_t *value);
/** The unresolved names and the integer of the object-like macro name,
 * resolving its names once; an empty value for any other name. value must be
 * empty. A body the reader cannot evaluate has no integer; an invalid literal
 * (HAL_EPROTO) and the limits of jh (HAL_EOVERFLOW) fail.
 */
hal_status_t jh_project_config_value(const jh_project_config_t *config,
                                     jb_json_span_t name,
                                     jh_config_value_t *value,
                                     jh_failure_t *failure);

/** The config.inspect result of jh for a configuration read from header and
 * preinclude (as the caller spells them): one JSON document and a line feed,
 * replacing out. A macro whose value fails carries the failure in its entry.
 */
hal_status_t jh_project_config_json(const jh_project_config_t *config,
                                    const char *header, const char *preinclude,
                                    jb_string_t *out, jh_failure_t *failure);
/** The macros as #define lines, replacing out: what config inspect prints
 * without --json. */
hal_status_t jh_project_config_text(const jh_project_config_t *config,
                                    jb_string_t *out);
/** Write the member "directives": an array with file, line, directive, name
 * and text of each jh_config_directive_t. */
hal_status_t jh_config_write_directives(jb_json_writer_t *writer,
                                        const jb_vector_t *directives);

#ifdef __cplusplus
}
#endif
#endif
