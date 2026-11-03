#ifndef JH_FAILURE_H
#define JH_FAILURE_H

/** @file jh_failure.h
 * The failure a jh operation records for its caller: the status, the exit
 * code of the command and a message. The C modules of jh return the status
 * and fill a record the caller owns. Also the span helpers those modules
 * share. C11.
 */
#include <jb/json.h>
#include <jb/string.h>
#include <stdbool.h>

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
 * message. Returns status.
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

#ifdef __cplusplus
}
#endif
#endif
