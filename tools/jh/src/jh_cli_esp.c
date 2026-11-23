/* jh esp build and jh esp artifacts: the ESP-IDF backend (jh_esp_build.h)
 * with the options of scripts/build_esp_idf.py. C11. */
#include "jh_cli_internal.h"
#include "jh_esp_build.h"
#include <string.h>

int jh_cli_esp(jh_cli_t *ctx, jh_command_id_t command) {
  const bool build = command == cmd_esp_build;
  if (!jh_cli_value(ctx, opt_root,
                    build ? "esp build requires --root"
                          : "esp artifacts requires --root") ||
      !jh_cli_value(ctx, opt_project,
                    build ? "esp build requires --project"
                          : "esp artifacts requires --project") ||
      !jh_cli_install_cancellation(ctx))
    return 0;
  const jh_arguments_t *a = &ctx->args;
  jh_esp_options_t options = {0};
  options.root = jh_cli_checkout(ctx);
  options.base = ctx->base;
  options.action = build ? JH_ESP_ACTION_BUILD : JH_ESP_ACTION_ARTIFACTS;
  options.project = a->values[opt_project];
  options.target = a->values[opt_target];
  options.board = a->values[opt_board_option];
  options.assembly = a->values[opt_assembly];
  options.name = a->values[opt_name];
  options.output = a->values[opt_output];
  options.variant = a->values[opt_variant_option];
  options.idf_dir = a->values[opt_idf_dir];
  options.port = a->values[opt_port];
  options.project_config = a->values[opt_project_config];
  options.sources = &a->lists[opt_source];
  options.include_dirs = &a->lists[opt_include_dir];
  options.link_libraries = &a->lists[opt_link_library];
  options.defines = &a->lists[opt_define];
  options.clean = a->present[opt_clean];
  options.all_features = a->present[opt_all_features];
  options.log_to_stderr = ctx->json;
  options.canceled = jh_cli_canceled;
  jh_esp_result_t result;
  jh_esp_result_init(&result);
  if (ctx->failure.status == HAL_OK &&
      jh_cli_ok(ctx, jh_esp_run(&options, &result, &ctx->failure)) &&
      ctx->json) {
    jh_result_begin(ctx, jb_string_size(&result.build_dir) * 2 + 2048, 32);
    jh_result_text(ctx, "command", ctx->command->name);
    jh_result_text(ctx, "status", "HAL_OK");
    jh_result_string(ctx, "target", jh_string_span(&result.target));
    jh_result_string(ctx, "board", jh_string_span(&result.board));
    if (result.device_tree)
      jh_result_string(ctx, "assembly", jh_string_span(&result.assembly));
    jh_result_string(ctx, "buildDir", jh_string_span(&result.build_dir));
    jh_result_number(ctx, "flashImages", result.flash_images);
    (void)jh_result_finish(ctx);
  }
  jh_esp_result_destroy(&result);
  return 0;
}
