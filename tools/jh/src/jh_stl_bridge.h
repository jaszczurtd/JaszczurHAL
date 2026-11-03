#ifndef JH_STL_BRIDGE_H
#define JH_STL_BRIDGE_H

/** @file jh_stl_bridge.h
 * Transitional calls for the jh modules that still keep paths and messages in
 * standard strings (main.cpp, the type catalogue, the configuration reader
 * and their tests). The JSON file and repr() layer takes borrowed views and
 * returns jhbase-backed owners; these functions convert at the call. The
 * header goes away with the last module that needs it.
 */
#include "jh_json_file.h"
#include "jh_python_repr.h"
#include <string>

namespace jh::stl {

/** jh::absolute_path for standard strings. */
inline std::string absolute_path(const std::string &path,
                                 const std::string &base) {
  return std::string(jh::absolute_path(path.c_str(), base.c_str()));
}

/** jh::python_repr as a standard string. */
inline std::string python_repr(std::string_view text) {
  return std::string(jh::python_repr(text));
}

/** The bytes of a jhbase span as a standard string. */
inline std::string copied(jb_json_span_t text) {
  return std::string(text.data, text.length);
}

} // namespace jh::stl
#endif
