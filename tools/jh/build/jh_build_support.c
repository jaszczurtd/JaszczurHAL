#include "jh_build_support.h"
#include "jh_json_check.h"
#include "jh_json_file.h"
#include <stdint.h>
#include <string.h>

bool jh_build_separator(char c) {
#ifdef _WIN32
  return c == '/' || c == '\\';
#else
  return c == '/';
#endif
}

static bool same_byte(char a, char b) {
#ifdef _WIN32
  if (jh_build_separator(a) && jh_build_separator(b))
    return true;
  return jh_ascii_to_upper(a) == jh_ascii_to_upper(b);
#else
  return a == b;
#endif
}

size_t jh_build_prefix_length(const char *path, const char *parent) {
  size_t length = strlen(parent);
  while (length > 1 && jh_build_separator(parent[length - 1]) &&
         !(length == 3 && parent[1] == ':'))
    --length;
  for (size_t i = 0; i < length; ++i)
    if (path[i] == '\0' || !same_byte(path[i], parent[i]))
      return SIZE_MAX;
  if (path[length] == '\0' || jh_build_separator(path[length]) ||
      (length > 0 && jh_build_separator(parent[length - 1])))
    return length;
  return SIZE_MAX;
}

bool jh_build_inside(const char *path, const char *parent) {
  return jh_build_prefix_length(path, parent) != SIZE_MAX;
}

jb_json_span_t jh_build_descriptor_text(jb_json_tree_view_t root, const char *a,
                                        const char *b) {
  jb_json_tree_view_t value = a != NULL ? jh_json_field(root, a) : root;
  if (b != NULL)
    value = jh_json_field(value, b);
  jb_json_span_t text = {"", 0};
  if (value.tree == NULL || !jh_json_string(value, &text))
    return (jb_json_span_t){"", 0};
  return text;
}

int jh_build_exit_code(const jb_process_result_t *result) {
  if (result->term_signal != 0)
    return 128 + result->term_signal;
  return result->exit_code > 255 ? 1 : (int)result->exit_code;
}
