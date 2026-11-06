#ifndef JH_JSON_CHECK_H
#define JH_JSON_CHECK_H

/** @file jh_json_check.h
 * Validation failures of the registry readers with the wording of
 * scripts/codegen_support.py: "<path>: <json path>: got <repr>; expected
 * <text>", required and allowed field sets, and JSON files with an object at
 * their root. Each failure records HAL_ECONFIG with exit code 2; a failed
 * container operation records the storage failure instead. C11.
 */
#include "jh_failure.h"
#include "jh_json_file.h"
#include <jb/json_tree.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Record the concatenation of count parts as a validation failure. */
hal_status_t jh_check_fail(jh_failure_t *failure, const jb_json_span_t *parts,
                           size_t count);

/** "<path>: <json_path>: got <got>; expected <expected>", where got is the
 * repr text the caller made. expected is a NUL-terminated string.
 */
hal_status_t jh_check_got(jh_failure_t *failure, jb_json_span_t path,
                          jb_json_span_t json_path, jb_json_span_t got,
                          const char *expected);

/** jh_check_got with Python's repr of value, or None when value is NULL or
 * a view jhbase rejects, such as the zero view of a member that is absent.
 */
hal_status_t jh_check_value(jh_failure_t *failure, jb_json_span_t path,
                            jb_json_span_t json_path,
                            const jb_json_tree_view_t *value,
                            const char *expected);

/** jh_check_got with the repr of a list of strings (jh_repr_list). */
hal_status_t jh_check_list(jh_failure_t *failure, jb_json_span_t path,
                           jb_json_span_t json_path, const jb_vector_t *list,
                           const char *expected);

/** require_exact_fields: value is an object that has every name of required
 * and no name outside allowed. Missing names win over unknown ones; both
 * appear sorted, as "required fields [...]" or "only fields [...]" with the
 * sorted names of the rule.
 */
hal_status_t
jh_check_exact_fields(jh_failure_t *failure, jb_json_span_t path,
                      jb_json_span_t json_path, jb_json_tree_view_t value,
                      const char *const *required, size_t required_count,
                      const char *const *allowed, size_t allowed_count);

/** "<path>: invalid JSON: <reason>" for a failed jh_json_read_value, with
 * the line and column where records a parse error; memory exhaustion records
 * the storage failure.
 */
hal_status_t jh_check_json_error(jh_failure_t *failure, jb_json_span_t path,
                                 const jh_json_location_t *where,
                                 const jh_failure_t *reason);

/** load_json_object: path, at most limit bytes, read as json.loads reads it,
 * with an object at its root: "<path>: invalid JSON: <reason>" (with the line
 * and column of a parse error) or "<path>: $: got <value>; expected an
 * object". *tree receives the tree; destroy it with jb_json_tree_destroy
 * whenever it is not NULL. All pointers are required.
 */
hal_status_t jh_check_load_object(const char *path, size_t limit,
                                  jb_json_tree_t **tree,
                                  jb_json_tree_view_t *root,
                                  jh_failure_t *failure);

/** Python's repr of a list of strings, ['a', 'b'], appended to out. */
hal_status_t jh_repr_list(jb_string_t *out, const jb_vector_t *list);

/** Whether value is a string; text receives its bytes. */
bool jh_json_string(jb_json_tree_view_t value, jb_json_span_t *text);
/** The members of an object or the items of an array; zero otherwise. */
size_t jh_json_count(jb_json_tree_view_t value);
/** isinstance(value, int) and not bool: an integer token (no fraction or
 * exponent) within int64_t, in *out. */
bool jh_json_int64(jb_json_tree_view_t value, int64_t *out);
/** The name of an object member; empty for any other view. */
jb_json_span_t jh_json_key(jb_json_tree_view_t member);
/** The member or item at index of an object or array; a zero view past the
 * end. */
jb_json_tree_view_t jh_json_item(jb_json_tree_view_t container, size_t index);
/** out becomes base followed by the NUL-terminated suffix. */
hal_status_t jh_json_path_join(jb_string_t *out, jb_json_span_t base,
                               const char *suffix);
/** out becomes "<base>[<index>]". */
hal_status_t jh_json_path_index(jb_string_t *out, jb_json_span_t base,
                                size_t index);

#ifdef __cplusplus
}
#endif
#endif
