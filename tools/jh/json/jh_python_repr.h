#ifndef JH_PYTHON_REPR_H
#define JH_PYTHON_REPR_H

/** @file jh_python_repr.h
 * repr() of a Python str, which the diagnostics of the jh commands quote the
 * way the Python HAL tools did. C11.
 */
#include <jb/json.h>
#include <jb/json_tree.h>
#include <jb/string.h>
#include <stdbool.h>

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

/** Append repr() of the Python value json.loads makes of value: objects as
 * dict and arrays as list (", " and ": " separators), strings as
 * jh_python_repr, true/false/null as True/False/None, an integer token as int
 * and any other number as float (nan, inf, shortest round-trip digits in the
 * fixed or exponent notation repr() chooses). A number of any length rounds
 * as float() rounds it, from its first 768 significant digits and whether a
 * nonzero digit follows; inf stands only for a value past the binary64 range.
 * On failure out keeps its previous bytes; a tree error, HAL_ENOMEM, or
 * HAL_EOVERFLOW for a full string or an exhausted conversion budget is
 * returned.
 */
hal_status_t jh_python_repr_json_append(jb_string_t *out,
                                        jb_json_tree_view_t value);

/** float() of a JSON number token as json.loads reads it: NaN, Infinity and
 * -Infinity, a magnitude past the binary64 range as an infinity, one below
 * it as a signed zero, and otherwise the nearest binary64 value from the
 * first 768 significant digits and whether a nonzero digit follows. Returns
 * HAL_OK, HAL_EINVAL for missing arguments or the status of the conversion.
 */
hal_status_t jh_python_float(jb_json_span_t token, double *out);
/** Append repr() of a Python float: the shortest round-trip digits in the
 * fixed or exponent notation repr() chooses, nan, inf, -inf, 0.0 or -0.0.
 * On failure out keeps its previous bytes.
 */
hal_status_t jh_python_float_append(jb_string_t *out, double value);
/** Python's str.strip(): out receives text without the whitespace
 * str.isspace() accepts on both ends (empty for whitespace only). False for
 * text that is not valid UTF-8, with out unchanged.
 */
bool jh_python_strip(jb_json_span_t text, jb_json_span_t *out);

#ifdef __cplusplus
}
#endif
#endif
