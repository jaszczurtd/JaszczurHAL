#include "jh_cli_internal.h"
#include "jh_feature_lint.h"
#include "jh_feature_registry.h"
#include "jh_project_config.h"
#include <string.h>

static const jh_feature_t *feature_at(const jh_feature_registry_t *registry,
                                      size_t index) {
  return jb_vector_const_at(&registry->features, index);
}

int jh_cli_feature_list(jh_cli_t *ctx) {
  const char *config = jh_cli_feature_config(ctx, "feature list");
  jh_feature_registry_t registry = {0};
  jh_feature_registry_init(&registry);
  jb_vector_t implied = {0};
  jb_string_t text = {0};
  (void)jh_cli_ok(
      ctx, jb_vector_init(&implied, sizeof(size_t), jb_heap_allocator()));
  (void)jh_cli_ok(ctx, jb_string_init(&text, jb_heap_allocator()));
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_ok(ctx, jh_feature_registry_load(
                          &registry, config, 16 * 1024 * 1024, &ctx->failure)))
    goto done;
  const size_t count = jb_vector_size(&registry.features);
  if (!ctx->json) {
    hal_status_t status = HAL_OK;
    for (size_t i = 0; i < count && status == HAL_OK; ++i) {
      const jh_feature_t *item = feature_at(&registry, i);
      jh_put_span(&text, &status, jh_string_span(&item->name));
      jh_put(&text, &status, item->derived ? " derived " : " feature ");
      jh_put_span(&text, &status, jh_string_span(&item->domain));
      jh_put(&text, &status, "\n");
    }
    if (jh_cli_ok(ctx, status))
      (void)jh_cli_stdout(ctx, jh_string_span(&text));
    goto done;
  }
  size_t bytes = strlen(config) + 1024;
  for (size_t i = 0; i < count; ++i) {
    const jh_feature_t *item = feature_at(&registry, i);
    bytes += jb_string_size(&item->name) * (count + 1) +
             jb_string_size(&item->domain) + jb_string_size(&item->note) +
             jh_cli_list_bytes(&item->implies) +
             jh_cli_list_bytes(&item->requirements) +
             jh_cli_list_bytes(&item->conflicts) +
             jh_cli_list_bytes(&item->feature_sources) +
             jh_cli_list_bytes(&item->portable_sources) +
             jh_cli_list_bytes(&item->dependencies) + 256;
  }
  jh_result_begin(ctx, bytes, 32);
  jh_result_text(ctx, "command", "feature.list");
  jh_result_text(ctx, "status", "HAL_OK");
  jh_result_text(ctx, "config", config);
  jh_result_text(ctx, "registryDigest", registry.digest);
  jh_result_text(ctx, "registrySchemaDigest", registry.schema_digest);
  jh_result_array(ctx, "features");
  for (size_t i = 0; i < count && ctx->failure.status == HAL_OK; ++i) {
    const jh_feature_t *item = feature_at(&registry, i);
    jh_result_object(ctx, NULL);
    jh_result_string(ctx, "name", jh_string_span(&item->name));
    jh_result_string(ctx, "domain", jh_string_span(&item->domain));
    jh_result_text(ctx, "kind", item->derived ? "derived" : "feature");
    jh_result_list(ctx, "implies", &item->implies);
    jh_result_list(ctx, "requires", &item->requirements);
    jh_result_list(ctx, "conflicts", &item->conflicts);
    jh_result_list(ctx, "featureSources", &item->feature_sources);
    jh_result_list(ctx, "portableSources", &item->portable_sources);
    jh_result_list(ctx, "dependencies", &item->dependencies);
    if (item->has_note)
      jh_result_string(ctx, "note", jh_string_span(&item->note));
    else
      jh_result_null(ctx, "note");
    if (ctx->failure.status != HAL_OK ||
        !jh_cli_require(ctx,
                        jh_feature_registry_closure(&registry, i, &implied), 4,
                        "cannot list the implied features"))
      break;
    jh_result_array(ctx, "closure");
    for (size_t j = 0;
         j < jb_vector_size(&implied) && ctx->failure.status == HAL_OK; ++j) {
      const size_t index = *(const size_t *)jb_vector_const_at(&implied, j);
      jh_result_item(ctx, jh_string_span(&feature_at(&registry, index)->name));
    }
    jh_result_array_end(ctx);
    jh_result_object_end(ctx);
  }
  jh_result_array_end(ctx);
  (void)jh_result_finish(ctx);
done:
  jb_string_destroy(&text);
  jb_vector_destroy(&implied);
  jh_feature_registry_destroy(&registry);
  return 0;
}

static bool exact_member(jb_json_tree_view_t object, const char *key,
                         size_t members, jb_json_tree_view_t *out) {
  size_t count = 0;
  return jh_json_member(object, key, out) &&
         jb_json_tree_count(object, &count) == HAL_OK && count == members;
}

static bool request_text(jb_json_tree_view_t value, jb_json_span_t *text) {
  return jh_json_type(value) == JB_JSON_STRING &&
         jb_json_tree_text(value, text) == HAL_OK;
}

static void request_file(jh_cli_t *ctx, const char *path, jb_vector_t *texts,
                         jb_vector_t *sources) {
  jb_json_tree_t *tree = NULL;
  if (!jh_cli_ok(ctx, jh_json_read_value(&ctx->location, path, 16 * 1024 * 1024,
                                         &tree, &ctx->failure)))
    return;
  jb_json_tree_view_t root, list;
  size_t count = 0;
  if (jb_json_tree_root(tree, &root) != HAL_OK ||
      !exact_member(root, "requests", 1, &list) ||
      jh_json_type(list) != JB_JSON_ARRAY ||
      jb_json_tree_count(list, &count) != HAL_OK) {
    (void)jh_cli_require(ctx, HAL_ETYPE, 2,
                         "request file needs only a requests array");
    goto done;
  }
  for (size_t i = 0; i < count && ctx->failure.status == HAL_OK; ++i) {
    jb_json_tree_view_t item, value;
    jb_json_span_t text, source;
    if (jb_json_tree_at(list, i, &item) != HAL_OK ||
        !exact_member(item, "request", 2, &value) ||
        !request_text(value, &text) ||
        !exact_member(item, "source", 2, &value) ||
        !request_text(value, &source)) {
      (void)jh_cli_error(
          ctx, HAL_ETYPE, 2,
          JH_PARTS(jh_span("request file /requests/"),
                   jh_span(jh_cli_size(ctx, i)),
                   jh_span(" needs only a string request and source")));
      break;
    }
    if (!jh_cli_ok(ctx, jh_list_push(texts, text)) ||
        !jh_cli_ok(ctx, jh_list_push(sources, source)))
      break;
  }
done:
  jb_json_tree_destroy(tree);
}

static jh_feature_request_t split_request(jb_json_span_t text,
                                          jb_json_span_t source) {
  const char *equal = memchr(text.data, '=', text.length);
  jh_feature_request_t item = {text, {"", 0}, source, equal != NULL};
  if (equal != NULL) {
    item.symbol.length = (size_t)(equal - text.data);
    item.value =
        (jb_json_span_t){equal + 1, text.length - item.symbol.length - 1};
  }
  return item;
}

int jh_cli_feature_resolve(jh_cli_t *ctx) {
  jh_arguments_t *args = &ctx->args;
  const char *config = jh_cli_feature_config(ctx, "feature resolve");
  jb_vector_t *texts = &args->lists[opt_request],
              *sources = &args->lists[opt_source];
  if (ctx->failure.status != HAL_OK)
    return 0;
  if (jb_vector_size(sources) != 0 &&
      jb_vector_size(sources) != jb_vector_size(texts)) {
    (void)jh_cli_require(ctx, HAL_EINVAL, 2,
                         "feature resolve takes one --source per --request");
    return 0;
  }
  if (args->present[opt_file]) {
    if (!jh_cli_value(ctx, opt_file, "--file needs a path"))
      return 0;
    if (!jh_cli_require(ctx, jb_vector_size(texts) == 0 ? HAL_OK : HAL_EINVAL,
                        2, "--file replaces --request and --source"))
      return 0;
  }
  if (args->present[opt_required_feature] && !args->present[opt_target]) {
    (void)jh_cli_require(ctx, HAL_EINVAL, 2,
                         "--required-feature requires --target");
    return 0;
  }
  for (size_t i = jb_vector_size(sources);
       i < jb_vector_size(texts) && ctx->failure.status == HAL_OK; ++i) {
    const jb_json_span_t source =
        jh_cli_join(ctx, JH_PARTS(jh_span("command-line:--request["),
                                  jh_span(jh_cli_size(ctx, i)), jh_span("]")));
    if (ctx->failure.status == HAL_OK)
      (void)jh_cli_ok(ctx, jh_list_push(sources, source));
  }
  jh_feature_registry_t registry = {0};
  jh_feature_resolution_t resolution = {0};
  jb_vector_t requests = {0}, required = {0};
  jh_feature_registry_init(&registry);
  jh_feature_resolution_init(&resolution);
  (void)jh_cli_ok(ctx, jb_vector_init(&requests, sizeof(jh_feature_request_t),
                                      jb_heap_allocator()));
  (void)jh_cli_ok(ctx, jb_vector_init(&required, sizeof(jb_json_span_t),
                                      jb_heap_allocator()));
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_ok(ctx, jh_feature_registry_load(
                          &registry, config, 16 * 1024 * 1024, &ctx->failure)))
    goto done;
  if (args->present[opt_file]) {
    const char *path =
        jh_cli_path(ctx, args->values[opt_file], ctx->base, false);
    if (ctx->failure.status == HAL_OK)
      request_file(ctx, path, texts, sources);
  }
  for (size_t i = 0; i < jb_vector_size(texts) && ctx->failure.status == HAL_OK;
       ++i) {
    const jh_feature_request_t item =
        split_request(jh_string_span(jh_list_at(texts, i)),
                      jh_string_span(jh_list_at(sources, i)));
    (void)jh_cli_ok(ctx, jb_vector_append(&requests, &item, 1));
  }
  for (size_t i = 0; i < jb_vector_size(&args->lists[opt_required_feature]) &&
                     ctx->failure.status == HAL_OK;
       ++i) {
    const jb_json_span_t item =
        jh_string_span(jh_list_at(&args->lists[opt_required_feature], i));
    (void)jh_cli_ok(ctx, jb_vector_append(&required, &item, 1));
  }
  const jh_feature_target_t target = {jh_span(args->values[opt_target]),
                                      jb_vector_const_data(&required),
                                      jb_vector_size(&required)};
  const char *context = args->present[opt_context_label]
                            ? args->values[opt_context_label]
                            : "command line";
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_ok(ctx,
                 jh_feature_resolve(&registry, jb_vector_const_data(&requests),
                                    jb_vector_size(&requests), jh_span(context),
                                    args->present[opt_target] ? &target : NULL,
                                    &resolution, &ctx->failure)))
    goto done;
  const size_t findings = jb_vector_size(&resolution.findings);
  const jb_json_span_t report = jh_cli_lines(ctx, &resolution.findings, "");
  if (ctx->failure.status != HAL_OK)
    goto done;
  (void)fwrite(report.data, 1, report.length, stderr);
  if (!ctx->json) {
    (void)jh_cli_stdout(ctx, jh_cli_lines(ctx, &resolution.resolved, ""));
    goto done;
  }
  const size_t entries = jb_vector_size(&resolution.provenance);
  size_t bytes = strlen(config) + strlen(context) +
                 strlen(args->values[opt_target]) + report.length +
                 2 * jh_cli_list_bytes(&resolution.resolved) + 1024;
  for (size_t i = 0; i < entries; ++i) {
    const jh_feature_provenance_t *entry =
        jb_vector_const_at(&resolution.provenance, i);
    bytes += jb_string_size(&entry->symbol) +
             jh_cli_list_bytes(&entry->sources) + 16;
  }
  jh_result_begin(ctx, bytes, entries);
  jh_result_text(ctx, "command", "feature.resolve");
  jh_result_text(ctx, "status", findings == 0 ? "HAL_OK" : "HAL_ECONFIG");
  jh_result_text(ctx, "config", config);
  jh_result_text(ctx, "registryDigest", registry.digest);
  jh_result_text(ctx, "context", context);
  if (args->present[opt_target])
    jh_result_text(ctx, "target", args->values[opt_target]);
  else
    jh_result_null(ctx, "target");
  jh_result_list(ctx, "requestedFeatures", &resolution.requested);
  jh_result_list(ctx, "resolvedFeatures", &resolution.resolved);
  jh_result_text(ctx, "resolvedFeaturesDigest", resolution.digest);
  jh_result_object(ctx, "provenance");
  for (size_t i = 0; i < entries && ctx->failure.status == HAL_OK; ++i) {
    const jh_feature_provenance_t *entry =
        jb_vector_const_at(&resolution.provenance, i);
    jh_result_list(ctx, jb_string_data(&entry->symbol), &entry->sources);
  }
  jh_result_object_end(ctx);
  jh_result_list(ctx, "findings", &resolution.findings);
  (void)jh_result_finish(ctx);
done:;
  const int result = jb_vector_size(&resolution.findings) == 0 ? 0 : 1;
  jb_vector_destroy(&required);
  jb_vector_destroy(&requests);
  jh_feature_resolution_destroy(&resolution);
  jh_feature_registry_destroy(&registry);
  return result;
}

static void lint_header(jh_cli_t *ctx, const jh_feature_registry_t *registry,
                        const char *path, jb_vector_t *findings) {
  jh_project_config_t config = {0};
  jh_failure_t read = {0};
  jh_project_config_init(&config);
  jh_failure_init(&read);
  const hal_status_t status =
      jh_project_config_read(path, NULL, "", NULL, &config, &read);
  if (status == HAL_ENOMEM || status == HAL_EOVERFLOW) {
    jh_failure_destroy(&ctx->failure);
    ctx->failure = read;
    read = (jh_failure_t){0};
    (void)jh_cli_ok(ctx, status);
    goto done;
  }
  if (status != HAL_OK && jb_string_size(&read.message) != 0) {
    (void)jh_cli_require(ctx,
                         jh_list_push(findings, jh_string_span(&read.message)),
                         4, "cannot store a lint finding");
    goto done;
  }
  if (!jh_cli_ok(ctx, status))
    goto done;
  const jb_json_span_t header =
      jb_vector_size(&config.files) == 0
          ? jh_span(path)
          : jh_string_span(jh_list_at(&config.files, 0));
  for (size_t i = 0;
       i < jb_vector_size(&config.directives) && ctx->failure.status == HAL_OK;
       ++i) {
    const jh_config_directive_t *item =
        jb_vector_const_at(&config.directives, i);
    if (!item->undef && jh_span_equal(jh_string_span(&item->file), header) &&
        jh_feature_symbol(jh_string_span(&item->name), true)) {
      const jb_json_span_t source =
          jh_cli_join(ctx, JH_PARTS(jh_span(path), jh_span(":"),
                                    jh_span(jh_cli_size(ctx, item->line))));
      const jh_feature_request_t request = {jh_string_span(&item->name),
                                            jh_string_span(&item->text), source,
                                            jb_string_size(&item->text) != 0};
      if (ctx->failure.status == HAL_OK)
        (void)jh_cli_ok(ctx,
                        jh_feature_check_request(registry, &request, findings,
                                                 NULL, &ctx->failure));
    }
  }
  for (size_t i = 0;
       i < jb_vector_size(&config.variants) && ctx->failure.status == HAL_OK;
       ++i) {
    const jh_config_variant_t *item = jb_vector_const_at(&config.variants, i);
    for (size_t j = 0; j < jb_vector_size(&item->definitions) &&
                       ctx->failure.status == HAL_OK;
         ++j) {
      jh_feature_request_t request = split_request(
          jh_string_span(jh_list_at(&item->definitions, j)), jh_span(""));
      if (jh_feature_symbol(request.symbol, false)) {
        request.source = jh_cli_join(
            ctx, JH_PARTS(jh_span(path), jh_span(":JH_PROJECT_VARIANTS("),
                          jh_string_span(&item->id), jh_span(")")));
        if (ctx->failure.status == HAL_OK)
          (void)jh_cli_ok(ctx,
                          jh_feature_check_request(registry, &request, findings,
                                                   NULL, &ctx->failure));
      }
    }
  }
done:
  jh_failure_destroy(&read);
  jh_project_config_destroy(&config);
}

int jh_cli_feature_lint(jh_cli_t *ctx) {
  const jh_arguments_t *args = &ctx->args;
  const char *config = jh_cli_feature_config(ctx, "feature lint");
  jb_vector_t roots = {0}, found = {0}, inputs = {0}, findings = {0};
  jh_feature_registry_t registry = {0};
  jh_feature_registry_init(&registry);
  (void)jh_cli_ok(ctx, jh_list_init(&roots));
  (void)jh_cli_ok(ctx, jh_list_init(&found));
  (void)jh_cli_ok(ctx, jh_list_init(&inputs));
  (void)jh_cli_ok(ctx, jh_list_init(&findings));
  for (size_t i = 0; i < jb_vector_size(&args->lists[opt_input_root]) &&
                     ctx->failure.status == HAL_OK;
       ++i) {
    const char *path = jh_cli_path(
        ctx, jb_string_data(jh_list_at(&args->lists[opt_input_root], i)),
        ctx->base, false);
    if (ctx->failure.status == HAL_OK)
      (void)jh_cli_ok(ctx, jh_list_push(&roots, jh_span(path)));
  }
  if (jb_vector_size(&roots) == 0) {
    if (!jh_cli_value(ctx, opt_root,
                      "feature lint requires --input-root or --root"))
      goto done;
    const char *path = jh_cli_checkout(ctx);
    if (ctx->failure.status == HAL_OK)
      (void)jh_cli_ok(ctx, jh_list_push(&roots, jh_span(path)));
  }
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_ok(ctx, jh_feature_registry_load(
                          &registry, config, 16 * 1024 * 1024, &ctx->failure)))
    goto done;
  for (size_t i = 0;
       i < jb_vector_size(&roots) && ctx->failure.status == HAL_OK; ++i)
    (void)jh_cli_ok(
        ctx, jh_feature_lint_inputs(jb_string_data(jh_list_at(&roots, i)),
                                    args->present[opt_include_hardware], &found,
                                    &ctx->failure));
  jh_list_sort(&found);
  for (size_t i = 0;
       i < jb_vector_size(&found) && ctx->failure.status == HAL_OK; ++i) {
    const jb_json_span_t path = jh_string_span(jh_list_at(&found, i));
    if (i == 0 ||
        !jh_span_equal(path, jh_string_span(jh_list_at(&found, i - 1))))
      (void)jh_cli_ok(ctx, jh_list_push(&inputs, path));
  }
  for (size_t i = 0;
       i < jb_vector_size(&inputs) && ctx->failure.status == HAL_OK; ++i) {
    const char *path = jb_string_data(jh_list_at(&inputs, i));
    const char *name = path;
    for (const char *p = path; *p != '\0'; ++p)
      if (*p == '/' || *p == '\\')
        name = p + 1;
    if (strcmp(name, "hal_project_config.h") == 0) {
      lint_header(ctx, &registry, path, &findings);
      continue;
    }
    jb_json_tree_t *tree = NULL;
    jb_json_tree_view_t manifest = {0};
    if (jh_cli_ok(ctx, jh_check_load_object(path, 16 * 1024 * 1024, &tree,
                                            &manifest, &ctx->failure)))
      (void)jh_cli_ok(ctx, jh_manifest_findings(manifest, jh_span(path),
                                                &findings, &ctx->failure));
    jb_json_tree_destroy(tree);
  }
  if (ctx->failure.status != HAL_OK)
    goto done;
  jh_list_sort(&findings);
  const bool report_only = args->present[opt_report_only];
  jb_json_span_t report =
      jh_cli_lines(ctx, &findings, report_only ? "warning: " : "error: ");
  const jb_json_span_t finding_count =
      jb_vector_size(&findings) == 0
          ? jh_span("no findings")
          : jh_cli_join(
                ctx,
                JH_PARTS(jh_span(jh_cli_size(ctx, jb_vector_size(&findings))),
                         jh_span(" finding(s)")));
  const jb_json_span_t summary = jh_cli_join(
      ctx,
      JH_PARTS(jh_span("linted "),
               jh_span(jh_cli_size(ctx, jb_vector_size(&inputs))),
               jh_span(" feature inputs: "), finding_count, jh_span("\n")));
  if (jb_vector_size(&findings) != 0 && !ctx->json)
    report = jh_cli_join(ctx, JH_PARTS(report, summary));
  if (ctx->failure.status != HAL_OK)
    goto done;
  (void)fwrite(report.data, 1, report.length, stderr);
  if (!ctx->json) {
    if (jb_vector_size(&findings) == 0)
      (void)jh_cli_stdout(ctx, summary);
  } else {
    const size_t bytes =
        strlen(config) + report.length + jh_cli_list_bytes(&inputs) + 1024;
    jh_result_begin(ctx, bytes, 32);
    jh_result_text(ctx, "command", "feature.lint");
    jh_result_text(ctx, "status",
                   report_only || jb_vector_size(&findings) == 0
                       ? "HAL_OK"
                       : "HAL_ECONFIG");
    jh_result_text(ctx, "config", config);
    jh_result_boolean(ctx, "reportOnly", report_only);
    jh_result_list(ctx, "inputs", &inputs);
    jh_result_list(ctx, "findings", &findings);
    (void)jh_result_finish(ctx);
  }
done:;
  const int result =
      args->present[opt_report_only] || jb_vector_size(&findings) == 0 ? 0 : 1;
  jh_feature_registry_destroy(&registry);
  jh_list_destroy(&findings);
  jh_list_destroy(&inputs);
  jh_list_destroy(&found);
  jh_list_destroy(&roots);
  return result;
}
