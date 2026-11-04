#ifndef JH_GENERATED_OUTPUT_H
#define JH_GENERATED_OUTPUT_H

/** @file jh_generated_output.h
 * Publishing and checking one generated artifact: atomic replacement that
 * keeps an identical file, or a comparison that changes nothing and prints a
 * bounded unified diff of the drift. C11.
 */
#include "jh_failure.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Publish content at path atomically; changed reports whether the file
 * changed. A failure has exit code 3.
 */
hal_status_t jh_write_generated(const char *path, jb_json_span_t content,
                                bool *changed, jh_failure_t *failure);

/** jh_write_generated that also gives the file the POSIX permission bits
 * mode (at most 0777): an identical file with other bits gets only a chmod
 * and counts as changed (jb_file_write_atomic_mode). Windows ignores mode.
 */
hal_status_t jh_write_generated_mode(const char *path, jb_json_span_t content,
                                     uint32_t mode, bool *changed,
                                     jh_failure_t *failure);

/** Whether path, an artifact path from config/tooling/artifacts.json, stays
 * below the root it is joined to: nonempty, without a leading / or \, a
 * drive or a ".." segment between / and \ separators.
 */
bool jh_generated_path_relative(jb_json_span_t path);

/** Compare content with the file at path without changing anything. A missing
 * or different file is reported on stderr, the difference as a unified diff
 * of at most diff_limit lines labelled path and label, and current is false.
 * An unreadable file fails with exit code 3 (4 for its size or memory).
 */
hal_status_t jh_check_generated(const char *path, jb_json_span_t content,
                                size_t diff_limit, jb_json_span_t label,
                                bool *current, jh_failure_t *failure);

/** One line of a unified diff, prefix followed by text, without a line
 * terminator. Both are borrowed for the call. A status other than HAL_OK
 * stops the diff.
 */
typedef hal_status_t (*jh_diff_line_fn)(void *user, jb_json_span_t prefix,
                                        jb_json_span_t text);

/** Write the unified diff lines (context 3) between two line sequences, as
 * difflib.unified_diff with lineterm="" writes them: --- from and +++ to,
 * then the hunks. Equal inputs give no lines. Returns HAL_OK, the first
 * status of line other than HAL_OK, or the status of a container that cannot
 * grow (HAL_ENOMEM, HAL_EOVERFLOW).
 */
hal_status_t jh_unified_diff(const jb_json_span_t *from_lines,
                             size_t from_count, const jb_json_span_t *to_lines,
                             size_t to_count, jb_json_span_t from,
                             jb_json_span_t to, jh_diff_line_fn line,
                             void *user);

#ifdef __cplusplus
}
#endif
#endif
