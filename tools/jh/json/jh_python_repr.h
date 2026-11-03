#ifndef JH_PYTHON_REPR_H
#define JH_PYTHON_REPR_H

/** @file jh_python_repr.h
 * repr() of a Python str, which the diagnostics of the jh commands quote the
 * way the Python HAL tools did. C11.
 */
#include <jb/json.h>
#include <jb/string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Receives the quoted text piece by piece: piece is the escaped form of
 * consumed input bytes (zero for the opening and closing quote) and is
 * borrowed for the call. A status other than HAL_OK stops the conversion.
 */
typedef hal_status_t (*jh_repr_piece_fn)(void *user, jb_json_span_t piece,
                                         size_t consumed);

/** Write repr() of UTF-8 text to piece: single quotes unless the text holds a
 * single quote and no double quote; backslash escapes for the backslash, the
 * chosen quote, tab, LF and CR; \\xNN, \\uNNNN or \\UNNNNNNNN for scalars that
 * str.isprintable() rejects, judged with the Unicode data pinned by jhbase.
 * A byte that is not valid UTF-8 is written as \\xNN. Returns HAL_OK, the
 * first status of piece other than HAL_OK, or HAL_EINVAL for a missing piece
 * or text bytes.
 */
hal_status_t jh_python_repr(jb_json_span_t text, jh_repr_piece_fn piece,
                            void *user);

/** Append repr() of text to out, using the allocator of out; text may point
 * into out. On failure out keeps its previous bytes and the status of the
 * string (HAL_ENOMEM, HAL_EOVERFLOW) is returned.
 */
hal_status_t jh_python_repr_append(jb_string_t *out, jb_json_span_t text);

#ifdef __cplusplus
}
#endif
#endif
