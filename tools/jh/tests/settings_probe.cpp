// Reads one file with jh::read_settings and prints the compact tree, or
// "ERROR <status> <exit code>" after a failure. settings_compat.py compares
// the result with load_json_file of the Python tools.
#include "jh_json_file.h"
#include <cstdio>
#include <string>

int main(int argc, char **argv) {
  if (argc != 2)
    return 64;
  constexpr size_t limit = 16 * 1024 * 1024;
  jh::owned_string output; // append_text appends to an owned_string.
  try {
    jh::parse_location where;
    const auto file = jh::read_settings(where, argv[1], limit);
    jb_json_tree_view_t root;
    jh::require(jb_json_tree_root(file.tree.get(), &root), 2, "no root");
    const jb_json_tree_format_t format{limit * 6, 0, false, false};
    size_t written = 0;
    jh::require(
        jb_json_tree_format(root, &format, jh::append_text, &output, &written),
        4, "cannot format the tree");
    output += '\n';
  } catch (const jh::failure &error) {
    output = std::string("ERROR ") + hal_status_to_string(error.status) + ' ' +
             std::to_string(error.exit_code) + '\n';
  }
  return std::fwrite(output.data(), 1, output.size(), stdout) == output.size()
             ? 0
             : 3;
}
