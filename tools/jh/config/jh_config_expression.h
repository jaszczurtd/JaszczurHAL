#ifndef JH_CONFIG_EXPRESSION_H
#define JH_CONFIG_EXPRESSION_H

/** @file jh_config_expression.h
 * Lexical operations and signed 64-bit expressions of hal_project_config.h.
 * C11; spans borrow their bytes for the call. Macro expansion belongs to the
 * caller. Diagnostics contain details only; the reader adds the source and
 * JH-CFG-SCOPE code.
 */
#include "jh_failure.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Whether c is an ASCII decimal digit. */
bool jh_config_digit(char c);
/** Whether c starts an ASCII preprocessor identifier. */
bool jh_config_name_start(char c);
/** Whether c continues an ASCII preprocessor identifier. */
bool jh_config_name_part(char c);
/** Whether text is a nonempty ASCII preprocessor identifier. */
bool jh_config_identifier(jb_json_span_t text);
/** Decode the scalar at offset, which must be within text. An invalid UTF-8
 * byte is its own scalar. length receives the number of consumed bytes.
 */
uint32_t jh_config_scalar_at(jb_json_span_t text, size_t offset,
                             size_t *length);
/** Python str.isspace() at offset, which must be within text. length receives
 * the scalar length even for a non-space. Invalid UTF-8 bytes are interpreted
 * as individual scalar values, as by jh_config_scalar_at.
 */
bool jh_config_space_at(jb_json_span_t text, size_t offset, size_t *length);
/** Position after consecutive whitespace starting at offset (at most length).
 */
size_t jh_config_skip_space(jb_json_span_t text, size_t offset);
/** Borrow text without trailing whitespace. */
jb_json_span_t jh_config_rstrip(jb_json_span_t text);

/** Tokenize a condition, trimming Python whitespace on both ends. tokens is
 * a list initialized by jh_list_init; success replaces its contents with
 * owned strings, failure leaves it unchanged. Empty input gives an empty list.
 * Returns HAL_ECONFIG (exit 2) for an invalid token, HAL_EINVAL (exit 3) for
 * missing arguments, or a storage failure (exit 4). failure is required and
 * initialized. Input may borrow bytes from tokens.
 */
hal_status_t jh_config_tokenize(jb_json_span_t input, jb_vector_t *tokens,
                                jh_failure_t *failure);

/** Evaluate count complete tokens after macro expansion; identifiers become
 * zero. Tokens must be nonempty spans produced by jh_config_tokenize (or
 * equivalent whole tokens). The array may be NULL only for count zero.
 * value changes only on success. range_error is set false on entry, true
 * only for arithmetic outside int64_t (HAL_EOVERFLOW, exit 4), with its
 * expression in failure.message. Nesting past 256 constructs also returns
 * HAL_EOVERFLOW but leaves range_error false. Syntax/division by zero return
 * HAL_ECONFIG; invalid octal literals HAL_EPROTO (both exit 2). Skipped
 * operands are parsed without arithmetic. Missing arguments give HAL_EINVAL;
 * storage failures exit 4. All output pointers are required; failure must
 * be initialized. No output may overlap the input or another output.
 */
hal_status_t jh_config_expression(const jb_json_span_t *tokens, size_t count,
                                  int64_t *value, bool *range_error,
                                  jh_failure_t *failure);

#ifdef __cplusplus
}
#endif
#endif
