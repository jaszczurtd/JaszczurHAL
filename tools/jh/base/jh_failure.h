#ifndef JH_FAILURE_H
#define JH_FAILURE_H

/** @file jh_failure.h
 * The failure a jh operation records for its caller: the status, the exit
 * code of the command and a message. The C modules of jh return the status
 * and fill a record the caller owns. Also the span helpers and lists of owned
 * strings those modules share. C11.
 */
#include <jb/array.h>
#include <jb/json.h>
#include <jb/string.h>
#include <jb/vector.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One recorded failure; status stays HAL_OK until a failure is recorded.
 * A jhbase container that cannot grow leaves message empty, with exit code 4
 * and its status (HAL_ENOMEM or HAL_EOVERFLOW).
 */
typedef struct {
  hal_status_t status;
  int exit_code;
  jb_string_t message;
} jh_failure_t;

/** Start an empty record whose message uses the host heap. failure is
 * required and must be zero or destroyed.
 */
void jh_failure_init(jh_failure_t *failure);
/** Release the message and reset the record to zero. A zero record is
 * harmless.
 */
void jh_failure_destroy(jh_failure_t *failure);

/** Record status and exit_code with the concatenation of count message parts,
 * replacing an earlier record; a part may quote the earlier message. When the
 * message cannot be stored, the record becomes the failure of its storage
 * (jh_fail_storage). Returns the recorded status.
 */
hal_status_t jh_fail(jh_failure_t *failure, hal_status_t status, int exit_code,
                     const jb_json_span_t *parts, size_t count);
/** jh_fail with one NUL-terminated message. */
hal_status_t jh_fail_text(jh_failure_t *failure, hal_status_t status,
                          int exit_code, const char *message);
/** Record a failed jhbase container operation: status, exit code 4 and no
 * message. Returns status; HAL_OK records nothing.
 */
hal_status_t jh_fail_storage(jh_failure_t *failure, hal_status_t status);
/** Record HAL_EINVAL with exit code 3 for a call that lacks a required
 * argument. With a NULL failure only HAL_EINVAL is returned.
 */
hal_status_t jh_fail_arguments(jh_failure_t *failure);
/** HAL_OK for HAL_OK; otherwise jh_fail_text(failure, status, exit_code,
 * message).
 */
hal_status_t jh_require(jh_failure_t *failure, hal_status_t status,
                        int exit_code, const char *message);

/** The span of a NUL-terminated string. text is required. */
jb_json_span_t jh_span(const char *text);
/** Whether two spans hold the same bytes. */
bool jh_span_equal(jb_json_span_t a, jb_json_span_t b);
/** The bytes of a jhbase string as a span. text is required. */
jb_json_span_t jh_string_span(const jb_string_t *text);
/** Order of two spans by their bytes: code point order for UTF-8. */
int jh_span_compare(jb_json_span_t a, jb_json_span_t b);
/** The file name of path without its last suffix, as Python's Path.stem:
 * the part after the last slash or backslash, up to its last dot unless that
 * dot starts the name. The result points into path.
 */
jb_json_span_t jh_span_path_stem(jb_json_span_t path);
/** The value of an ASCII hexadecimal digit (0-9, a-f or A-F), or -1 for any
 * other byte. */
int jh_hex_digit(char c);

/* Text built piece by piece: each jh_put* appends only while *status is
 * HAL_OK and stores the result of the append in *status, so a sequence of
 * calls needs one check at its end. */

/** Append the NUL-terminated text to out. */
hal_status_t jh_append(jb_string_t *out, const char *text);
/** Append the bytes of text; an empty span appends nothing. */
hal_status_t jh_append_span(jb_string_t *out, jb_json_span_t text);
/** jh_append while *status is HAL_OK. */
void jh_put(jb_string_t *out, hal_status_t *status, const char *text);
/** jh_append_span while *status is HAL_OK. */
void jh_put_span(jb_string_t *out, hal_status_t *status, jb_json_span_t text);
/** Replace out with the concatenation of count parts. */
hal_status_t jh_join(jb_string_t *out, const jb_json_span_t *parts,
                     size_t count);
/** An array of spans and its count, the parts and count arguments of
 * jh_join, jh_fail and jh_list_push_parts. C only (compound literal). */
#define JH_PARTS(...)                                                          \
  ((const jb_json_span_t[]){__VA_ARGS__}),                                     \
      COUNTOF(((const jb_json_span_t[]){__VA_ARGS__}))
/** A jb_json_sink_fn appending to the jb_string_t at user. */
hal_status_t jh_string_sink(void *user, const char *data, size_t length);
/** The decimal text of value while *status is HAL_OK. */
void jh_put_int64(jb_string_t *out, hal_status_t *status, int64_t value);

/* Lists of owned strings: jb_vector_t of jb_string_t on the host heap. */

/** Start an empty list. */
hal_status_t jh_list_init(jb_vector_t *list);
/** The string at index, which must be in range. */
const jb_string_t *jh_list_at(const jb_vector_t *list, size_t index);
/** Append a copy of text. */
hal_status_t jh_list_push(jb_vector_t *list, jb_json_span_t text);
/** Append the concatenation of count parts. */
hal_status_t jh_list_push_parts(jb_vector_t *list, const jb_json_span_t *parts,
                                size_t count);
/** Sort the strings in code point order. */
void jh_list_sort(jb_vector_t *list);
/** Whether the list holds text. */
bool jh_list_contains(const jb_vector_t *list, jb_json_span_t text);
/** Release every string and the list. */
void jh_list_destroy(jb_vector_t *list);

#ifdef __cplusplus
}
#endif
#endif
