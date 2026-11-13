/* Reads one file with jh_json_read_settings and prints the compact tree, or
 * ERROR <status> <exit code>. settings_compat.py compares this result with
 * load_json_file of the Python tools. */
#include "jh_json_file.h"
#include <stdio.h>

int main(int argc, char **argv) {
  if (argc != 2)
    return 64;
  const size_t limit = 16 * 1024 * 1024;
  jb_string_t output = {0};
  jh_json_location_t where = {0};
  jh_failure_t failure = {0};
  jb_json_tree_t *tree = NULL;
  bool exists = false;
  jh_json_location_init(&where);
  jh_failure_init(&failure);
  hal_status_t status = jb_string_init(&output, jb_heap_allocator());
  if (status == HAL_OK)
    status =
        jh_json_read_settings(&where, argv[1], limit, &tree, &exists, &failure);
  jb_json_tree_view_t root = {0};
  if (status == HAL_OK)
    status = jh_require(&failure, jb_json_tree_root(tree, &root), 2, "no root");
  const jb_json_tree_format_t format = {limit * 6, 0, false, false};
  size_t written = 0;
  if (status == HAL_OK)
    status = jh_require(
        &failure,
        jb_json_tree_format(root, &format, jh_string_sink, &output, &written),
        4, "cannot format the tree");
  if (status == HAL_OK)
    status = jh_append(&output, "\n");
  int result;
  if (status == HAL_OK)
    result = fwrite(jb_string_data(&output), 1, jb_string_size(&output),
                    stdout) == jb_string_size(&output)
                 ? 0
                 : 3;
  else
    result = fprintf(stdout, "ERROR %s %d\n", hal_status_to_string(status),
                     failure.status == HAL_OK ? 4 : failure.exit_code) < 0
                 ? 3
                 : 0;
  jb_json_tree_destroy(tree);
  jh_failure_destroy(&failure);
  jh_json_location_destroy(&where);
  jb_string_destroy(&output);
  return result;
}
