#include "jh_cli_internal.h"
#include "jh_device_defaults.h"
#include "jh_feature_lint.h"
#include "jh_feature_registry.h"
#include <jb/json_pointer.h>
#include <string.h>

int jh_cli_inspect(jh_cli_t *ctx) {
  const jh_arguments_t *args = &ctx->args;
  jh_json_document_t document = {0};
  bool changed = false;
  const char *destination = "";
  if (!jh_cli_value(ctx, opt_file, "json inspect requires --file"))
    return 0;
  if (args->present[opt_check] && !args->present[opt_output]) {
    (void)jh_cli_require(ctx, HAL_EINVAL, 2, "--check requires --output");
    return 0;
  }
  const char *path = jh_cli_path(ctx, args->values[opt_file], ctx->base, false);
  const size_t limit = jh_cli_limit(ctx);
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_ok(ctx, jh_json_read_document(&ctx->location, path, limit, false,
                                            &document, &ctx->failure)))
    goto done;
  jb_json_node_t selected;
  jb_json_span_t raw;
  if (!jh_cli_require(ctx,
                      jb_json_pointer_get(&document.doc,
                                          jh_span(args->values[opt_pointer]),
                                          &selected),
                      2, "cannot resolve JSON Pointer") ||
      !jh_cli_require(ctx, jb_json_raw(selected, &raw), 2,
                      "cannot read selected value"))
    goto done;
  if (args->present[opt_output]) {
    destination = jh_cli_path(ctx, args->values[opt_output], ctx->base, false);
    const jb_json_span_t content =
        jh_cli_join(ctx, JH_PARTS(raw, jh_span("\n")));
    if (ctx->failure.status != HAL_OK ||
        !jh_cli_require(
            ctx,
            jb_file_write_atomic(destination, content.data, content.length,
                                 args->present[opt_check], &changed),
            3, "cannot publish output file"))
      goto done;
  }
  const bool stale = args->present[opt_check] && changed;
  if (ctx->json) {
    size_t keys = 0;
    for (size_t i = 0; i < document.doc.token_count; ++i)
      keys += document.doc.tokens[i].has_key ? 1U : 0U;
    jh_result_begin(ctx, document.doc.input.length, keys);
    jh_result_text(ctx, "command", "json.inspect");
    jh_result_text(ctx, "status", stale ? "HAL_ESTATE" : "HAL_OK");
    jh_result_string(ctx, "source", jh_string_span(&ctx->location.source));
    jh_result_text(ctx, "pointer", args->values[opt_pointer]);
    jh_result_key(ctx, "value");
    if (ctx->failure.status == HAL_OK)
      (void)jh_cli_require(ctx, jb_json_writer_node(&ctx->writer, selected), 4,
                           "cannot write selected JSON");
    if (args->present[opt_output]) {
      jh_result_text(ctx, "output", destination);
      jh_result_boolean(ctx, "changed", changed);
      jh_result_boolean(ctx, "checkOnly", args->present[opt_check]);
    }
    (void)jh_result_finish(ctx);
  } else if (!args->present[opt_output]) {
    (void)jh_cli_stdout(ctx, jh_cli_join(ctx, JH_PARTS(raw, jh_span("\n"))));
  }
  if (stale && ctx->failure.status == HAL_OK)
    (void)fprintf(stderr, "%s: output differs\n", destination);
done:
  jh_json_document_destroy(&document);
  return args->present[opt_check] && changed ? 1 : 0;
}

int jh_cli_validate(jh_cli_t *ctx) {
  const jh_arguments_t *args = &ctx->args;
  if (!args->present[opt_file] || args->values[opt_file][0] == '\0' ||
      !args->present[opt_schema] || args->values[opt_schema][0] == '\0') {
    (void)jh_cli_require(ctx, HAL_EINVAL, 2,
                         "json validate requires --file and --schema");
    return 0;
  }
  jh_json_document_t input = {0};
  jh_schema_files_t state = {0};
  hal_status_t checked = HAL_OK;
  const size_t limit = jh_cli_limit(ctx);
  const char *path = jh_cli_path(ctx, args->values[opt_file], ctx->base, false);
  jh_schema_files_init(&state, &ctx->location, limit);
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_ok(ctx, jh_json_read_document(&ctx->location, path, limit, false,
                                            &input, &ctx->failure)))
    goto done;
  const char *schema =
      jh_cli_path(ctx, args->values[opt_schema], ctx->base, false);
  const jh_json_document_t *root_schema = NULL;
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_ok(ctx, jh_schema_files_root(&state, schema, &root_schema,
                                           &ctx->failure)))
    goto done;
  checked = jh_schema_files_validate(&state, &input, &ctx->failure);
  if (checked != HAL_ECONFIG && !jh_cli_ok(ctx, checked))
    goto done;
  if (!jh_cli_source(ctx, path))
    goto done;
  const size_t count = jh_schema_files_report_count(&state);
  for (size_t i = 0; i < count; ++i) {
    const jh_schema_report_t *error = jh_schema_files_report(&state, i);
    (void)fprintf(stderr, "%s:", path);
    if (jb_string_size(&error->pointer) == 0)
      (void)fputc('/', stderr);
    else
      (void)fwrite(jb_string_data(&error->pointer), 1,
                   jb_string_size(&error->pointer), stderr);
    (void)fputs(": ", stderr);
    (void)fwrite(jb_string_data(&error->message), 1,
                 jb_string_size(&error->message), stderr);
    (void)fputc('\n', stderr);
  }
  if (ctx->json) {
    jh_result_begin(ctx, 1024 * 1024, count * 8);
    jh_result_text(ctx, "command", "json.validate");
    jh_result_text(ctx, "status", hal_status_to_string(checked));
    jh_result_text(ctx, "source", path);
    jh_result_string(ctx, "schema", jh_string_span(&root_schema->uri));
    jh_result_array(ctx, "errors");
    for (size_t i = 0; i < count && ctx->failure.status == HAL_OK; ++i) {
      const jh_schema_report_t *error = jh_schema_files_report(&state, i);
      jh_result_object(ctx, NULL);
      jh_result_string(ctx, "instancePointer", jh_string_span(&error->pointer));
      jh_result_string(ctx, "schemaUri", jh_string_span(&error->uri));
      jh_result_string(ctx, "schemaPointer", jh_string_span(&error->rule));
      jh_result_string(ctx, "reason", jh_string_span(&error->reason));
      jh_result_string(ctx, "message", jh_string_span(&error->message));
      jh_result_object_end(ctx);
    }
    jh_result_array_end(ctx);
    (void)jh_result_finish(ctx);
  }
done:
  jh_schema_files_destroy(&state);
  jh_json_document_destroy(&input);
  return checked == HAL_OK ? 0 : 2;
}

int jh_cli_defaults(jh_cli_t *ctx) {
  const jh_arguments_t *args = &ctx->args;
  const size_t limit = 16 * 1024 * 1024;
  const char *hal_root = jh_cli_generator(ctx, "generate device-defaults");
  const char *bindings_dir =
      args->present[opt_bindings]
          ? jh_cli_path(ctx, args->values[opt_bindings], ctx->base, false)
          : jh_cli_path(ctx, "config/hardware/bindings", hal_root, false);
  const char *out_root =
      args->present[opt_output_root]
          ? jh_cli_path(ctx, args->values[opt_output_root], ctx->base, false)
          : hal_root;
  jb_string_t relative = {0}, content = {0};
  jb_vector_t features = {0}, capabilities = {0};
  jh_catalogue_t types = {0};
  bool changed = false, current = true;
  (void)jh_cli_ok(ctx, jb_string_init(&relative, jb_heap_allocator()));
  (void)jh_cli_ok(ctx, jb_string_init(&content, jb_heap_allocator()));
  (void)jh_cli_ok(ctx, jh_list_init(&features));
  (void)jh_cli_ok(ctx, jh_list_init(&capabilities));
  (void)jh_cli_ok(ctx, jh_catalogue_init(&types));
  const char *key = "deviceDefaultsHeader";
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_ok(ctx, jh_generated_artifact_paths(hal_root, &key, 1, &relative,
                                                  &ctx->failure)))
    goto done;
  const char *destination =
      jh_cli_path(ctx, jb_string_data(&relative), out_root, false);
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_ok(ctx, jh_catalogue_features(&ctx->location, hal_root, limit,
                                            &features, &ctx->failure)) ||
      !jh_cli_ok(ctx, jh_catalogue_capabilities(&ctx->location, hal_root, limit,
                                                &capabilities, &ctx->failure)))
    goto done;
  const char *schemas = jh_cli_path(ctx, "config/hardware", hal_root, false);
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_ok(ctx, jh_catalogue_load(&types, &ctx->location, schemas,
                                        bindings_dir, limit, &ctx->failure)) ||
      !jh_cli_ok(ctx, jh_catalogue_check(&types, &features, &capabilities,
                                         &ctx->failure)) ||
      !jh_cli_ok(ctx,
                 jh_render_device_defaults(&types, &content, &ctx->failure)) ||
      !jh_cli_source(ctx, destination))
    goto done;
  if (args->present[opt_write_mode]) {
    if (!jh_cli_ok(ctx,
                   jh_write_generated(destination, jh_string_span(&content),
                                      &changed, &ctx->failure)))
      goto done;
    if (!ctx->json)
      (void)jh_cli_stdout(
          ctx,
          jh_span(changed
                      ? "generated 1 device defaults artifacts (1 changed)\n"
                      : "generated 1 device defaults artifacts (0 changed)\n"));
  } else {
    const jb_json_span_t label = jh_cli_join(
        ctx, JH_PARTS(jh_span("generated:"), jh_string_span(&relative)));
    if (ctx->failure.status != HAL_OK ||
        !jh_cli_ok(ctx,
                   jh_check_generated(destination, jh_string_span(&content), 40,
                                      label, &current, &ctx->failure)))
      goto done;
    changed = !current;
    if (current && !ctx->json)
      (void)jh_cli_stdout(
          ctx, jh_span("verified 1 generated device defaults artifacts\n"));
  }
  if (ctx->json) {
    jh_result_begin(ctx, strlen(destination) + strlen(bindings_dir) + 1024, 32);
    jh_result_text(ctx, "command", "generate.device-defaults");
    jh_result_text(ctx, "status", current ? "HAL_OK" : "HAL_ESTATE");
    jh_result_text(ctx, "root", hal_root);
    jh_result_text(ctx, "bindings", bindings_dir);
    jh_result_text(ctx, "output", destination);
    jh_result_boolean(ctx, "changed", changed);
    jh_result_boolean(ctx, "checkOnly", args->present[opt_check]);
    (void)jh_result_finish(ctx);
  }
done:
  jh_catalogue_destroy(&types);
  jh_list_destroy(&capabilities);
  jh_list_destroy(&features);
  jb_string_destroy(&content);
  jb_string_destroy(&relative);
  return current ? 0 : 1;
}

int jh_cli_features(jh_cli_t *ctx) {
  const jh_arguments_t *args = &ctx->args;
  const char *hal_root = jh_cli_generator(ctx, "generate features");
  const char *config =
      args->present[opt_config_root]
          ? jh_cli_path(ctx, args->values[opt_config_root], ctx->base, false)
          : jh_cli_path(ctx, "config", hal_root, false);
  const char *out_root =
      args->present[opt_output_root]
          ? jh_cli_path(ctx, args->values[opt_output_root], ctx->base, false)
          : hal_root;
  jh_feature_registry_t registry = {0};
  jh_feature_outputs_t outputs = {0};
  jh_feature_registry_init(&registry);
  size_t changed = 0;
  bool current = true;
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_ok(ctx,
                 jh_feature_registry_load(&registry, config, 16 * 1024 * 1024,
                                          &ctx->failure)) ||
      !jh_cli_ok(ctx,
                 jh_feature_outputs_read(hal_root, &outputs, &ctx->failure)) ||
      !jh_cli_ok(ctx,
                 jh_feature_registry_publish(&registry, &outputs, out_root,
                                             args->present[opt_check], &changed,
                                             &current, &ctx->failure)))
    goto done;
  if (!ctx->json) {
    if (args->present[opt_write_mode])
      (void)jh_cli_stdout(
          ctx,
          jh_cli_join(ctx, JH_PARTS(jh_span("generated 2 feature artifacts ("),
                                    jh_span(jh_cli_size(ctx, changed)),
                                    jh_span(" changed)\n"))));
    else if (current)
      (void)jh_cli_stdout(ctx,
                          jh_span("verified 2 generated feature artifacts\n"));
  } else {
    jh_result_begin(
        ctx, strlen(hal_root) + strlen(config) + strlen(out_root) + 1024, 32);
    jh_result_text(ctx, "command", "generate.features");
    jh_result_text(ctx, "status", current ? "HAL_OK" : "HAL_ESTATE");
    jh_result_text(ctx, "root", hal_root);
    jh_result_text(ctx, "config", config);
    jh_result_text(ctx, "output", out_root);
    jh_result_number(ctx, "changed", changed);
    jh_result_boolean(ctx, "checkOnly", args->present[opt_check]);
    (void)jh_result_finish(ctx);
  }
done:
  jh_feature_outputs_destroy(&outputs);
  jh_feature_registry_destroy(&registry);
  return current ? 0 : 1;
}

int jh_cli_manifest(jh_cli_t *ctx) {
  if (!jh_cli_value(ctx, opt_file, "config manifest requires --file"))
    return 0;
  const char *path =
      jh_cli_path(ctx, ctx->args.values[opt_file], ctx->base, false);
  const char *name =
      ctx->args.present[opt_label] ? ctx->args.values[opt_label] : path;
  jb_json_tree_t *tree = NULL;
  bool exists = false;
  jb_vector_t findings = {0};
  (void)jh_cli_ok(ctx, jh_list_init(&findings));
  jb_json_tree_view_t document = {0};
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_ok(ctx,
                 jh_json_read_settings(&ctx->location, path, 16 * 1024 * 1024,
                                       &tree, &exists, &ctx->failure)) ||
      !jh_cli_require(ctx, jb_json_tree_root(tree, &document), 4,
                      "cannot read the manifest") ||
      !jh_cli_ok(ctx, jh_manifest_findings(document, jh_span(name), &findings,
                                           &ctx->failure)))
    goto done;
  const jb_json_span_t report = jh_cli_lines(ctx, &findings, "");
  if (ctx->failure.status != HAL_OK)
    goto done;
  (void)fwrite(report.data, 1, report.length, stderr);
  if (ctx->json) {
    jh_result_begin(ctx, strlen(path) + strlen(name) + report.length + 1024,
                    32);
    jh_result_text(ctx, "command", "config.manifest");
    jh_result_text(ctx, "status",
                   jb_vector_size(&findings) == 0 ? "HAL_OK" : "HAL_ECONFIG");
    jh_result_text(ctx, "file", path);
    jh_result_text(ctx, "label", name);
    jh_result_boolean(ctx, "exists", exists);
    jh_result_list(ctx, "findings", &findings);
    (void)jh_result_finish(ctx);
  }
done:;
  const int result = jb_vector_size(&findings) == 0 ? 0 : 1;
  jh_list_destroy(&findings);
  jb_json_tree_destroy(tree);
  return result;
}
