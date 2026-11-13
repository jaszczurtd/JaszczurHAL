#include "jh_cli_internal.h"
#include "jh_project_build.h"
#include "jh_project_config.h"
#include <jb/filesystem.h>
#include <string.h>

int jh_cli_directives(jh_cli_t *ctx) {
  if (!jh_cli_value(ctx, opt_file, "config directives requires --file"))
    return 0;
  const char *path =
      jh_cli_path(ctx, ctx->args.values[opt_file], ctx->base, false);
  jb_vector_t directives = {0};
  jb_string_t text = {0};
  (void)jh_cli_ok(ctx, jh_config_directives_init(&directives));
  (void)jh_cli_ok(ctx, jb_string_init(&text, jb_heap_allocator()));
  if (!jh_cli_source(ctx, path) ||
      !jh_cli_ok(
          ctx, jh_project_config_directives(path, &directives, &ctx->failure)))
    goto done;
  size_t bytes = 0;
  hal_status_t status = HAL_OK;
  for (size_t i = 0; i < jb_vector_size(&directives) &&
                     ctx->failure.status == HAL_OK && status == HAL_OK;
       ++i) {
    const jh_config_directive_t *item = jb_vector_const_at(&directives, i);
    bytes += jb_string_size(&item->file) + jb_string_size(&item->name) +
             jb_string_size(&item->text) + 96;
    if (!ctx->json) {
      jh_put(&text, &status, jh_cli_size(ctx, item->line));
      jh_put(&text, &status, item->undef ? " undef " : " define ");
      jh_put_span(&text, &status, jh_string_span(&item->name));
      jh_put(&text, &status, "\n");
    }
  }
  if (!jh_cli_ok(ctx, status))
    goto done;
  if (!ctx->json)
    (void)jh_cli_stdout(ctx, jh_string_span(&text));
  else {
    jh_result_begin(ctx, strlen(path) + bytes + 1024, 32);
    jh_result_text(ctx, "command", "config.directives");
    jh_result_text(ctx, "status", "HAL_OK");
    jh_result_text(ctx, "file", path);
    if (ctx->failure.status == HAL_OK)
      (void)jh_cli_require(
          ctx, jh_config_write_directives(&ctx->writer, &directives), 4,
          "cannot write the directives");
    (void)jh_result_finish(ctx);
  }
done:
  jb_string_destroy(&text);
  jh_config_directives_destroy(&directives);
  return 0;
}

int jh_cli_config(jh_cli_t *ctx) {
  const jh_arguments_t *args = &ctx->args;
  if ((args->present[opt_header] && args->values[opt_header][0] == '\0') ||
      (args->present[opt_preinclude] &&
       args->values[opt_preinclude][0] == '\0')) {
    (void)jh_cli_require(ctx, HAL_EINVAL, 2,
                         "--header and --preinclude need a path");
    return 0;
  }
  const char *header =
      args->present[opt_header]
          ? jh_cli_path(ctx, args->values[opt_header], ctx->base, false)
          : "";
  const char *preinclude =
      args->present[opt_preinclude]
          ? jh_cli_path(ctx, args->values[opt_preinclude], ctx->base, false)
          : "";
  jh_project_config_t config = {0};
  jb_string_t text = {0};
  jh_project_config_init(&config);
  (void)jh_cli_ok(ctx, jb_string_init(&text, jb_heap_allocator()));
  if (!jh_cli_source(ctx, header) ||
      !jh_cli_ok(ctx, jh_project_config_read(header, &args->lists[opt_define],
                                             preinclude, NULL, &config,
                                             &ctx->failure)))
    goto done;
  if (ctx->json) {
    if (!jh_cli_ok(ctx, jh_project_config_json(&config, header, preinclude,
                                               &text, &ctx->failure)))
      goto done;
  } else {
    if (!jh_cli_ok(ctx, jh_project_config_text(&config, &text)))
      goto done;
    if (config.stopped)
      (void)fprintf(stderr, "%s: #error %s\n",
                    jb_string_data(&config.error_source),
                    jb_string_data(&config.error_message));
  }
  (void)jh_cli_stdout(ctx, jh_string_span(&text));
done:
  jb_string_destroy(&text);
  jh_project_config_destroy(&config);
  return 0;
}

static void append_option(jh_cli_t *ctx, jh_option_id_t id, jb_vector_t *out,
                          bool strip_define) {
  const jb_vector_t *list = &ctx->args.lists[id];
  for (size_t i = 0; i < jb_vector_size(list) && ctx->failure.status == HAL_OK;
       ++i) {
    jb_json_span_t value = jh_string_span(jh_list_at(list, i));
    if (strip_define && value.length >= 2 && value.data[0] == '-' &&
        value.data[1] == 'D') {
      value.data += 2;
      value.length -= 2;
    }
    (void)jh_cli_ok(ctx, jh_list_push(out, value));
  }
}

static void build_json(jh_cli_t *ctx, const jh_project_build_t *build,
                       const char *header, const char *destination, bool cmake,
                       bool changed) {
  jh_result_begin(ctx,
                  build->model_bytes +
                      2 * jh_cli_list_bytes(&build->definitions) +
                      2 * jh_cli_list_bytes(&build->sources) + 4096,
                  32);
  jh_result_text(ctx, "command", cmake ? "config.cmake" : "config.build");
  jh_result_text(ctx, "status", "HAL_OK");
  jh_result_text(ctx, "header", header);
  jh_result_tree(ctx, "config", build->config_root);
  if (build->variant.tree != NULL)
    jh_result_tree(ctx, "variant", build->variant);
  else
    jh_result_null(ctx, "variant");
  jh_result_list(ctx, "definitions", &build->definitions);
  jh_result_list(ctx, "requestedFeatures", &build->requested);
  jh_result_array(ctx, "definitionSources");
  for (size_t i = 0; i < jb_vector_size(&build->effective_definitions) &&
                     ctx->failure.status == HAL_OK;
       ++i) {
    jh_result_object(ctx, NULL);
    jh_result_string(
        ctx, "definition",
        jh_string_span(jh_list_at(&build->effective_definitions, i)));
    jh_result_string(ctx, "source",
                     jh_string_span(jh_list_at(&build->sources, i)));
    jh_result_object_end(ctx);
  }
  jh_result_array_end(ctx);
  jh_result_array(ctx, "featureRequests");
  for (size_t i = 0;
       i < jb_vector_size(&build->requests) && ctx->failure.status == HAL_OK;
       ++i) {
    const jh_feature_request_t *request =
        jb_vector_const_at(&build->requests, i);
    jh_result_object(ctx, NULL);
    jh_result_string(ctx, "symbol", request->symbol);
    if (request->has_value)
      jh_result_string(ctx, "value", request->value);
    else
      jh_result_null(ctx, "value");
    jh_result_string(ctx, "source", request->source);
    jh_result_object_end(ctx);
  }
  jh_result_array_end(ctx);
  if (cmake) {
    jh_result_text(ctx, "output", destination);
    jh_result_boolean(ctx, "changed", changed);
  }
  (void)jh_result_finish(ctx);
}

int jh_cli_project_build(jh_cli_t *ctx, bool cmake) {
  const jh_arguments_t *args = &ctx->args;
  if (cmake && !jh_cli_value(ctx, opt_output, "config cmake requires --output"))
    return 0;
  if (!args->present[opt_selector_option] &&
      !jh_cli_value(ctx, opt_target,
                    cmake ? "config cmake requires --target"
                          : "config build requires --target"))
    return 0;
  jh_board_registry_t boards = {0};
  jh_feature_registry_t features = {0};
  jh_project_build_t build = {0};
  jb_vector_t required = {0}, extra = {0};
  jb_string_t rendered = {0}, parent = {0}, joined = {0};
  jh_board_registry_init(&boards);
  jh_feature_registry_init(&features);
  jh_project_build_init(&build);
  (void)jh_cli_ok(ctx, jh_list_init(&required));
  (void)jh_cli_ok(ctx, jh_list_init(&extra));
  (void)jh_cli_ok(ctx, jb_string_init(&rendered, jb_heap_allocator()));
  (void)jh_cli_ok(ctx, jb_string_init(&parent, jb_heap_allocator()));
  (void)jh_cli_ok(ctx, jb_string_init(&joined, jb_heap_allocator()));
  append_option(ctx, opt_define, &extra, cmake);
  const char *selector = "", *header = "", *hardware = "", *destination = "";
  if (args->present[opt_selector_option]) {
    selector = args->values[opt_selector_option];
    append_option(ctx, opt_required_feature, &required, false);
  } else {
    (void)jh_cli_load_boards(ctx, cmake ? "config cmake" : "config build",
                             &boards);
    if (ctx->failure.status != HAL_OK)
      goto done;
    const jh_board_descriptor_t *descriptor = jh_board_registry_find(
        &boards.targets, jh_span(args->values[opt_target]));
    if (descriptor == NULL) {
      (void)jh_cli_error(
          ctx, HAL_ECONFIG, 2,
          JH_PARTS(jh_span("[JH-CFG-TARGET] unknown target "),
                   jh_cli_repr(ctx, jh_span(args->values[opt_target]))));
      goto done;
    }
    jb_json_tree_view_t hal = {0}, selected = {0}, needed = {0};
    jb_json_span_t selected_text = {0};
    if (!jh_cli_require(
            ctx, jb_json_tree_find(descriptor->root, jh_span("hal"), &hal), 2,
            "target has no hal") ||
        !jh_cli_require(
            ctx, jb_json_tree_find(hal, jh_span("targetSelector"), &selected),
            2, "target has no selector") ||
        !jh_cli_require(ctx, jb_json_tree_text(selected, &selected_text), 2,
                        "target selector is not text"))
      goto done;
    selector = jh_cli_text(ctx, JH_PARTS(selected_text));
    if (jh_json_member(descriptor->root, "requiredFeatures", &needed))
      for (size_t i = 0;
           i < jh_json_count(needed) && ctx->failure.status == HAL_OK; ++i) {
        jb_json_span_t feature = {0};
        if (!jh_cli_require(
                ctx, jb_json_tree_text(jh_json_item(needed, i), &feature), 2,
                "target feature is not text"))
          break;
        (void)jh_cli_ok(ctx, jh_list_push(&required, feature));
      }
  }
  if (ctx->failure.status != HAL_OK)
    goto done;
  if (args->present[opt_header]) {
    if (!jh_cli_value(ctx, opt_header, "config build --header needs a path"))
      goto done;
    if (!jh_cli_require(
            ctx, args->present[opt_project_directory] ? HAL_EINVAL : HAL_OK, 2,
            "use either --header or --config-dir"))
      goto done;
    header = args->values[opt_header];
  } else if (args->present[opt_project_directory]) {
    if (!jh_cli_value(ctx, opt_project_directory,
                      cmake ? "config cmake --config-dir needs a path"
                            : "config build --config-dir needs a path"))
      goto done;
    header = cmake ? jh_cli_path(ctx, args->values[opt_project_directory],
                                 ctx->base, true)
                   : args->values[opt_project_directory];
    if (ctx->failure.status != HAL_OK ||
        !jh_cli_ok(ctx, jh_path_join(header, "hal_project_config.h", &joined)))
      goto done;
    header = jb_string_data(&joined);
  }
  if (args->present[opt_hardware_header]) {
    hardware =
        jh_cli_path(ctx, args->values[opt_hardware_header], ctx->base, true);
    if (ctx->failure.status != HAL_OK)
      goto done;
    if (cmake) {
      jb_fs_info_t info = {0};
      const hal_status_t checked = jb_fs_status(hardware, true, &info);
      if (checked == HAL_ENOMEM || checked == HAL_EOVERFLOW) {
        (void)jh_cli_ok(ctx, checked);
        goto done;
      }
      if (checked != HAL_OK || info.kind != JB_FS_REGULAR) {
        (void)jh_cli_error(
            ctx, HAL_ECONFIG, 2,
            JH_PARTS(jh_span("[JH-HW-GENERATION] "),
                     jh_span(args->values[opt_hardware_header]),
                     jh_span(" does not exist; a device tree build reads its "
                             "generated header")));
        goto done;
      }
    }
  }
  const jh_project_build_options_t options = {header,
                                              hardware,
                                              args->values[opt_target],
                                              selector,
                                              args->values[opt_variant_option],
                                              &required,
                                              &extra};
  const hal_status_t evaluated =
      jh_project_build_evaluate(&options, &build, &ctx->failure);
  ctx->configuration_rejected = build.rejected;
  if (!jh_cli_ok(ctx, evaluated))
    goto done;
  bool changed = false;
  if (cmake) {
    const char *hal_root = jh_cli_checkout(ctx);
    const char *config = jh_cli_path(ctx, "config", hal_root, false);
    if (ctx->failure.status != HAL_OK ||
        !jh_cli_ok(ctx,
                   jh_feature_registry_load(&features, config, 16 * 1024 * 1024,
                                            &ctx->failure)) ||
        !jh_cli_ok(ctx, jh_project_build_validate(&options, &build, &features,
                                                  &ctx->failure)) ||
        !jh_cli_ok(ctx,
                   jh_project_build_cmake(&options, &build, &boards.targets,
                                          &rendered, &ctx->failure)))
      goto done;
    destination = jh_cli_path(ctx, args->values[opt_output], ctx->base, true);
    if (ctx->failure.status != HAL_OK ||
        !jh_cli_ok(ctx, jh_path_parent(destination, &parent, &ctx->failure)) ||
        !jh_cli_require(ctx, jb_fs_mkdir(jb_string_data(&parent), true), 3,
                        "cannot create output directory") ||
        !jh_cli_ok(ctx, jh_project_build_write(destination,
                                               jh_string_span(&rendered),
                                               &changed, &ctx->failure)))
      goto done;
  }
  if (ctx->json)
    build_json(ctx, &build, header, destination, cmake, changed);
done:
  jb_string_destroy(&joined);
  jb_string_destroy(&parent);
  jb_string_destroy(&rendered);
  jh_list_destroy(&extra);
  jh_list_destroy(&required);
  jh_project_build_destroy(&build);
  jh_feature_registry_destroy(&features);
  jh_board_registry_destroy(&boards);
  return 0;
}
