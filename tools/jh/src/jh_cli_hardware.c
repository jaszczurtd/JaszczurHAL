#include "jh_board_outputs.h"
#include "jh_cli_internal.h"
#include "jh_hardware_build.h"
#include "jh_process_support.h"
#include "jh_project_build.h"
#include <jb/filesystem.h>
#include <string.h>

static const char *const resolve_names[] = {"jh_hardware.h",
                                            "jh_hardware_resolved.json"};

static void generation_failure(jh_cli_t *ctx, const char *path) {
  if (ctx->failure.status != HAL_OK && ctx->failure.exit_code != 4)
    (void)jh_fail_pointer(&ctx->failure, "JH-HW-GENERATION", jh_span(path),
                          jh_span(""),
                          JH_PARTS(jh_string_span(&ctx->failure.message)));
}

static void output_result(jh_cli_t *ctx, jh_command_id_t command,
                          const jb_string_t *text, const jb_string_t *header,
                          const jb_string_t *board, const char *output,
                          size_t changed) {
  jb_json_tree_t *tree = NULL, *board_tree = NULL;
  const jb_json_tree_view_t value = jh_cli_parse_result(ctx, text, &tree);
  jb_json_tree_view_t assembly_board = {0};
  if (jb_string_size(board) != 0)
    assembly_board = jh_cli_parse_result(ctx, board, &board_tree);
  if (ctx->failure.status != HAL_OK)
    goto done;
  if (!ctx->json && command != cmd_hardware_list &&
      command != cmd_hardware_select && command != cmd_hardware_identity)
    goto done;
  jh_result_begin(ctx,
                  jb_string_size(text) + jb_string_size(header) +
                      jb_string_size(board) + strlen(output) + 2048,
                  (jb_string_size(text) + jb_string_size(board)) / 2 + 32);
  jh_result_text(ctx, "command", ctx->command->name);
  jh_result_text(ctx, "status", "HAL_OK");
  jh_result_number(ctx, "schemaVersion", 1);
  if (command == cmd_hardware_list) {
    jh_result_tree(ctx, "assemblies", value);
  } else if (command == cmd_hardware_select) {
    jh_result_tree(ctx, "selection", value);
  } else if (command == cmd_hardware_identity) {
    jh_result_tree(ctx, "selection", jh_json_field(value, "selection"));
    jh_result_tree(ctx, "variant",
                   jh_json_field(jh_json_field(value, "software"), "variant"));
    jh_result_tree(ctx, "hardwareSha256",
                   jh_json_field(value, "hardwareSha256"));
    jh_result_tree(ctx, "configSha256", jh_json_field(value, "configSha256"));
  } else {
    jh_result_text(ctx, "outputDir", output);
    jh_result_number(ctx, "changed", changed);
    jh_result_tree(ctx, "document", value);
    if (command == cmd_hardware_resolve)
      jh_result_string(ctx, "header", jh_string_span(header));
    if (assembly_board.tree != NULL)
      jh_result_tree(ctx, "assemblyBoard", assembly_board);
  }
  (void)jh_result_finish(ctx);
done:
  jb_json_tree_destroy(board_tree);
  jb_json_tree_destroy(tree);
}

static void write_files(jh_cli_t *ctx, const char *const *paths,
                        const jb_string_t *texts, size_t count,
                        size_t *changed) {
  for (size_t i = 0; ctx->failure.status == HAL_OK && i < count; ++i) {
    bool written = false;
    if (jh_cli_ok(ctx,
                  jh_project_build_write(paths[i], jh_string_span(&texts[i]),
                                         &written, &ctx->failure)))
      *changed += written ? 1u : 0u;
    else
      generation_failure(ctx, paths[i]);
  }
}

static void remove_files(const char *const *paths, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (paths[i] == NULL || *paths[i] == '\0')
      continue;
    const hal_status_t status = jb_fs_remove(paths[i], false);
    if (status != HAL_OK && status != HAL_ENOENT)
      (void)fprintf(stderr,
                    "error: cannot remove stale hardware output %s: %s\n",
                    paths[i], hal_status_to_string(status));
  }
}

int jh_cli_hardware(jh_cli_t *ctx, jh_command_id_t command) {
  if (!jh_cli_value(ctx, opt_root, "hardware command requires --root") ||
      !jh_cli_value(ctx, opt_project_directory,
                    "hardware command requires --config-dir"))
    return 0;
  const jh_arguments_t *a = &ctx->args;
  const bool listing = command == cmd_hardware_list,
             identity = command == cmd_hardware_identity;
  const bool selecting = command == cmd_hardware_select;
  const bool finalizing = command == cmd_hardware_finalize;
  if (finalizing &&
      (!jh_cli_value(ctx, opt_hardware,
                     "hardware finalize requires --hardware") ||
       !jh_cli_value(ctx, opt_hardware_header,
                     "hardware finalize requires --hardware-header")))
    return 0;
  if (!listing && !selecting && !identity && !a->present[opt_output_dir] &&
      (finalizing || !ctx->json)) {
    (void)jh_cli_require(ctx, HAL_EINVAL, 2,
                         "hardware generation requires --output-dir (resolve "
                         "also permits --json)");
    return 0;
  }
  const char *root = jh_cli_path(ctx, a->values[opt_root], ctx->base, true);
  const char *config =
      jh_cli_path(ctx, a->values[opt_project_directory], ctx->base, true);
  const size_t limit = jh_cli_limit(ctx);
  jb_string_t output = {0}, board = {0}, resolved[2] = {{0}, {0}};
  (void)jb_string_init(&output, jb_heap_allocator());
  (void)jb_string_init(&board, jb_heap_allocator());
  for (size_t i = 0; i < COUNTOF(resolved); ++i)
    (void)jb_string_init(&resolved[i], jb_heap_allocator());
  jh_hardware_registry_t registry;
  jh_hardware_registry_init(&registry);
  jh_hardware_t hardware = {0};
  jh_hardware_outputs_t outputs;
  jh_hardware_outputs_init(&outputs);
  jb_temp_dir_t *scratch = NULL;
  const char *resolve_paths[COUNTOF(resolve_names)] = {0};
  const char *final_paths[JH_HARDWARE_OUTPUT_COUNT] = {0};
  bool managed = false;
  size_t changed = 0;
  if (a->present[opt_output_dir] && ctx->failure.status == HAL_OK) {
    (void)jh_cli_ok(ctx,
                    jh_board_managed_output(a->values[opt_output_dir],
                                            a->present[opt_output_root]
                                                ? a->values[opt_output_root]
                                                : NULL,
                                            root, &output, &ctx->failure));
    generation_failure(ctx, a->values[opt_output_dir]);
    managed = ctx->failure.status == HAL_OK;
  } else if (identity && ctx->failure.status == HAL_OK &&
             jh_cli_ok(ctx, jh_process_scratch(&scratch, &ctx->failure))) {
    (void)jh_cli_ok(ctx, jh_append(&output, jb_temp_dir_path(scratch)));
  }
  if (jb_string_size(&output) != 0) {
    for (size_t i = 0; i < COUNTOF(resolve_paths); ++i)
      resolve_paths[i] =
          jh_cli_path(ctx, resolve_names[i], jb_string_data(&output), false);
    for (size_t i = 0; i < COUNTOF(final_paths); ++i)
      final_paths[i] = jh_cli_path(ctx, jh_hardware_output_names[i],
                                   jb_string_data(&output), false);
  }
  if (listing || selecting) {
    registry.max_bytes = limit;
    (void)jh_cli_ok(ctx, jh_append(&registry.root, root));
    (void)jh_cli_ok(
        ctx, jh_path_join(root, "config/hardware", &registry.schema_dir));
  } else if (ctx->failure.status == HAL_OK) {
    (void)jh_cli_ok(
        ctx, jh_hardware_registry_load(&registry, root, limit, &ctx->failure));
  }
  if (!finalizing && ctx->failure.status == HAL_OK) {
    const char *path = jh_cli_path(ctx, "device_tree.json", config, false);
    if (jh_cli_ok(
            ctx, jh_hardware_load(&hardware, &registry, path, &ctx->failure))) {
      if (listing) {
        (void)jh_cli_ok(
            ctx, jh_hardware_list(&hardware, &resolved[1], &ctx->failure));
      } else if (selecting) {
        (void)jh_cli_ok(
            ctx, jh_hardware_select(
                     &hardware,
                     a->present[opt_target] ? a->values[opt_target] : NULL,
                     a->present[opt_assembly] ? a->values[opt_assembly] : NULL,
                     &resolved[1], &ctx->failure));
      } else if (jh_cli_ok(
                     ctx,
                     jh_hardware_resolve(
                         &hardware,
                         a->present[opt_target] ? a->values[opt_target] : NULL,
                         a->present[opt_assembly] ? a->values[opt_assembly]
                                                  : NULL,
                         &ctx->failure)) &&
                 jh_cli_ok(ctx, jh_hardware_header(&hardware, &resolved[0],
                                                   &ctx->failure)) &&
                 jh_cli_ok(ctx, jh_hardware_document(&hardware, &resolved[1],
                                                     &ctx->failure)) &&
                 jb_string_size(&output) != 0) {
        write_files(ctx, resolve_paths, resolved, COUNTOF(resolved), &changed);
      }
    }
  }
  if (a->present[opt_include_board] && ctx->failure.status == HAL_OK)
    (void)jh_cli_ok(ctx,
                    jh_hardware_board_json(&hardware, &board, &ctx->failure));
  if ((finalizing || identity) && ctx->failure.status == HAL_OK) {
    const jh_hardware_build_options_t options = {
        &registry,
        &registry.feature_registry,
        config,
        identity ? resolve_paths[1]
                 : jh_cli_path(ctx, a->values[opt_hardware], ctx->base, true),
        identity
            ? resolve_paths[0]
            : jh_cli_path(ctx, a->values[opt_hardware_header], ctx->base, true),
        jb_string_data(&output),
        a->present[opt_variant_option] ? a->values[opt_variant_option] : NULL,
        a->present[opt_compiler] ? a->values[opt_compiler] : NULL,
        &a->lists[opt_define],
        &a->lists[opt_compiler_flag]};
    if (ctx->failure.status == HAL_OK &&
        jh_cli_ok(ctx,
                  jh_hardware_finalize(&options, &outputs, &ctx->failure)) &&
        !identity)
      write_files(ctx, final_paths, outputs.content, COUNTOF(outputs.content),
                  &changed);
  }
  if (ctx->failure.status == HAL_OK)
    output_result(ctx, command,
                  finalizing || identity ? &outputs.content[JH_HARDWARE_CONFIG]
                                         : &resolved[1],
                  &resolved[0], &board, jb_string_data(&output), changed);
  if (ctx->failure.status != HAL_OK && managed) {
    if (!finalizing)
      remove_files(resolve_paths, COUNTOF(resolve_paths));
    remove_files(final_paths, COUNTOF(final_paths));
  }
  (void)jh_cli_ok(ctx, jb_temp_dir_remove(&scratch));
  jh_hardware_outputs_destroy(&outputs);
  jh_hardware_destroy(&hardware);
  jh_hardware_registry_destroy(&registry);
  for (size_t i = 0; i < COUNTOF(resolved); ++i)
    jb_string_destroy(&resolved[i]);
  jb_string_destroy(&output);
  jb_string_destroy(&board);
  return 0;
}
