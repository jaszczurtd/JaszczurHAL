#ifndef JH_PYTHON_REPR_H
#define JH_PYTHON_REPR_H

/** @file jh_python_repr.h
 * repr() of a Python str, which the diagnostics of the jh commands quote the
 * way the Python HAL tools did. C++17.
 */
#include "jh_containers.h"
#include <cstddef>
#include <string_view>

namespace jh {

/** Receives the quoted text piece by piece: piece is the escaped form of
 * consumed input bytes (zero for the opening and closing quote).
 */
using repr_piece_fn = void (*)(void *user, std::string_view piece,
                               size_t consumed);

/** Write repr() of UTF-8 text to piece: single quotes unless the text holds a
 * single quote and no double quote; backslash escapes for the backslash, the
 * chosen quote, tab, LF and CR; \\xNN, \\uNNNN or \\UNNNNNNNN for scalars that
 * str.isprintable() rejects, judged with the Unicode data pinned by jhbase.
 * A byte that is not valid UTF-8 is written as \\xNN.
 */
void python_repr(std::string_view text, repr_piece_fn piece, void *user);

/** repr() of UTF-8 text as one string. */
owned_string python_repr(std::string_view text);

} // namespace jh
#endif
