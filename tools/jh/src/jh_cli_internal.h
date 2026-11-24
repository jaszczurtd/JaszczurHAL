#ifndef JH_CLI_INTERNAL_H
#define JH_CLI_INTERNAL_H

/* Command implementation shared by the C11 entry point and command groups.
 * An invocation owns its arguments, temporary strings and buffered result.
 * Helpers retain the first failure; commands stop before further side effects.
 */
#include "jh_board_registry.h"
#include "jh_generated_output.h"
#include "jh_json_check.h"
#include "jh_json_file.h"
#include "jh_python_repr.h"
#include <jb/json_writer.h>
#include <jb/process.h>
#include <stdio.h>

typedef enum {
  opt_file,
  opt_schema,
  opt_pointer,
  opt_output,
  opt_root,
  opt_bindings,
  opt_output_root,
  opt_config_root,
  opt_write_mode,
  opt_check,
  opt_max_bytes,
  opt_git,
  opt_timeout_ms,
  opt_header,
  opt_define,
  opt_preinclude,
  opt_context_label,
  opt_target,
  opt_request,
  opt_source,
  opt_required_feature,
  opt_input_root,
  opt_report_only,
  opt_include_hardware,
  opt_label,
  opt_boards_root,
  opt_clocks_dir,
  opt_esp_sdkconfig,
  opt_board_option,
  opt_feature_option,
  opt_output_dir,
  opt_project_directory,
  opt_variant_option,
  opt_hardware_header,
  opt_selector_option,
  opt_project,
  opt_assembly,
  opt_build_dir,
  opt_serial,
  opt_port,
  opt_tool,
  opt_address,
  opt_size,
  opt_reset,
  opt_baud,
  opt_hardware,
  opt_compiler,
  opt_compiler_flag,
  opt_include_board,
  opt_effective,
  opt_resolution_output,
  opt_project_config,
  opt_all_features,
  opt_freertos,
  opt_library_only,
  opt_clean,
  opt_jobs,
  opt_sdk_dir,
  opt_toolchain,
  opt_picotool_dir,
  opt_picotool_build_dir,
  opt_example,
  opt_freertos_kernel,
  opt_prefix,
  opt_name,
  opt_include_dir,
  opt_link_library,
  opt_idf_dir,
  opt_dry_run,
  opt_configure_only,
  opt_debug,
  opt_python,
  opt_bootsel_volume,
  opt_verbose,
  opt_machine,
  opt_json_version,
  opt_help,
  opt_version,
  opt_count
} jh_option_id_t;

typedef enum {
  cmd_inspect,
  cmd_validate,
  cmd_defaults,
  cmd_features,
  cmd_repository,
  cmd_config,
  cmd_directives,
  cmd_feature_list,
  cmd_feature_resolve,
  cmd_feature_lint,
  cmd_manifest,
  cmd_board_list,
  cmd_board_validate,
  cmd_board_default,
  cmd_clock_check,
  cmd_clock_list,
  cmd_boards,
  cmd_board_config,
  cmd_project_build,
  cmd_project_cmake,
  cmd_firmware,
  cmd_hardware_list,
  cmd_hardware_select,
  cmd_hardware_resolve,
  cmd_hardware_finalize,
  cmd_hardware_identity,
  cmd_target_list,
  cmd_target_facts,
  cmd_library_key,
  cmd_library_build,
  cmd_library_install,
  cmd_library_inspect,
  cmd_esp_build,
  cmd_esp_artifacts,
  cmd_compile
} jh_command_id_t;

typedef struct {
  const char *first, *second, *name;
  jh_option_id_t accepted[opt_count]; /* opt_count terminates the list. */
} jh_command_spec_t;

typedef struct {
  bool present[opt_count];
  const char
      *values[opt_count]; /* Borrow argv, which outlives the invocation. */
  jb_vector_t lists[opt_count]; /* Owned string lists for repeated options. */
  jb_vector_t command;
} jh_arguments_t;

typedef struct {
  jh_arguments_t args;
  jh_failure_t failure;
  jh_json_location_t location;
  bool json, process_attempted, configuration_rejected;
  bool firmware_published;
  const char *device_state;
  const jh_command_spec_t *command;
  const char *base;
  jb_process_result_t process;
  jb_vector_t strings;
  jb_json_writer_t writer;
  jb_vector_t workspace;
  jb_string_t result;
} jh_cli_t;

bool jh_cli_ok(jh_cli_t *ctx, hal_status_t status);
bool jh_cli_require(jh_cli_t *ctx, hal_status_t status, int exit_code,
                    const char *message);
bool jh_cli_error(jh_cli_t *ctx, hal_status_t status, int exit_code,
                  const jb_json_span_t *parts, size_t count);
bool jh_cli_value(jh_cli_t *ctx, jh_option_id_t option, const char *message);
jb_json_span_t jh_cli_join(jh_cli_t *ctx, const jb_json_span_t *parts,
                           size_t count);
const char *jh_cli_text(jh_cli_t *ctx, const jb_json_span_t *parts,
                        size_t count);
const char *jh_cli_size(jh_cli_t *ctx, size_t value);
const char *jh_cli_path(jh_cli_t *ctx, const char *path, const char *base,
                        bool resolve);
const char *jh_cli_checkout(jh_cli_t *ctx);
const char *jh_cli_generator(jh_cli_t *ctx, const char *command);
const char *jh_cli_feature_config(jh_cli_t *ctx, const char *command);
jb_json_span_t jh_cli_repr(jh_cli_t *ctx, jb_json_span_t text);
bool jh_cli_stdout(jh_cli_t *ctx, jb_json_span_t text);
bool jh_cli_source(jh_cli_t *ctx, const char *text);
size_t jh_cli_count(jh_cli_t *ctx, jh_option_id_t option, size_t fallback,
                    size_t maximum, const char *message);
size_t jh_cli_limit(jh_cli_t *ctx);
size_t jh_cli_list_bytes(const jb_vector_t *list);
size_t jh_cli_tree_bytes(jh_cli_t *ctx, jb_json_tree_view_t tree);
jb_json_span_t jh_cli_lines(jh_cli_t *ctx, const jb_vector_t *list,
                            const char *prefix);

void jh_result_begin(jh_cli_t *ctx, size_t bytes, size_t keys);
void jh_result_key(jh_cli_t *ctx, const char *name);
void jh_result_string(jh_cli_t *ctx, const char *name, jb_json_span_t value);
void jh_result_text(jh_cli_t *ctx, const char *name, const char *value);
void jh_result_number(jh_cli_t *ctx, const char *name, size_t value);
void jh_result_boolean(jh_cli_t *ctx, const char *name, bool value);
void jh_result_null(jh_cli_t *ctx, const char *name);
void jh_result_array(jh_cli_t *ctx, const char *name);
void jh_result_array_end(jh_cli_t *ctx);
void jh_result_object(jh_cli_t *ctx, const char *name);
void jh_result_object_end(jh_cli_t *ctx);
void jh_result_item(jh_cli_t *ctx, jb_json_span_t value);
void jh_result_list(jh_cli_t *ctx, const char *name, const jb_vector_t *list);
void jh_result_tree(jh_cli_t *ctx, const char *name, jb_json_tree_view_t value);
jb_json_tree_view_t jh_cli_parse_result(jh_cli_t *ctx, const jb_string_t *text,
                                        jb_json_tree_t **tree);
void jh_result_process(jh_cli_t *ctx);
bool jh_result_finish(jh_cli_t *ctx);

/* Return the ordinary command exit code; failure.exit_code takes precedence
 * whenever failure.status is not HAL_OK. */
int jh_cli_inspect(jh_cli_t *ctx);
int jh_cli_validate(jh_cli_t *ctx);
int jh_cli_defaults(jh_cli_t *ctx);
int jh_cli_features(jh_cli_t *ctx);
int jh_cli_repository(jh_cli_t *ctx);
int jh_cli_firmware(jh_cli_t *ctx);
int jh_cli_hardware(jh_cli_t *ctx, jh_command_id_t command);
bool jh_cli_install_cancellation(jh_cli_t *ctx);
bool jh_cli_canceled(void *user);
int jh_cli_config(jh_cli_t *ctx);
int jh_cli_directives(jh_cli_t *ctx);
int jh_cli_feature_list(jh_cli_t *ctx);
int jh_cli_feature_resolve(jh_cli_t *ctx);
int jh_cli_feature_lint(jh_cli_t *ctx);
int jh_cli_manifest(jh_cli_t *ctx);
int jh_cli_board_list(jh_cli_t *ctx);
int jh_cli_board_validate(jh_cli_t *ctx);
int jh_cli_board_default(jh_cli_t *ctx);
int jh_cli_clock_check(jh_cli_t *ctx);
int jh_cli_clock_list(jh_cli_t *ctx);
int jh_cli_boards(jh_cli_t *ctx);
int jh_cli_board_config(jh_cli_t *ctx);
int jh_cli_project_build(jh_cli_t *ctx, bool cmake);
int jh_cli_target_list(jh_cli_t *ctx);
int jh_cli_target_facts(jh_cli_t *ctx);
int jh_cli_library_key(jh_cli_t *ctx);
int jh_cli_library_build(jh_cli_t *ctx);
int jh_cli_library_install(jh_cli_t *ctx);
int jh_cli_library_inspect(jh_cli_t *ctx);
int jh_cli_esp(jh_cli_t *ctx, jh_command_id_t command);
int jh_cli_compile(jh_cli_t *ctx);

const char *jh_cli_load_boards(jh_cli_t *ctx, const char *command,
                               jh_board_registry_t *registry);

#endif
