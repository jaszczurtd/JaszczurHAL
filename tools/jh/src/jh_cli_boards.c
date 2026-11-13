#include "jh_board_outputs.h"
#include "jh_cli_internal.h"
#include "jh_clock_registry.h"
#include <string.h>

const char *jh_cli_load_boards(jh_cli_t *ctx, const char *command,
                               jh_board_registry_t *registry) {
  const char *message =
      jh_cli_text(ctx, JH_PARTS(jh_span(command), jh_span(" requires --root")));
  if (!jh_cli_value(ctx, opt_root, message))
    return "";
  const char *hal_root = jh_cli_checkout(ctx);
  const char *directory;
  if (ctx->args.present[opt_boards_root]) {
    if (!jh_cli_value(ctx, opt_boards_root, "--boards-root needs a path"))
      return "";
    directory =
        jh_cli_path(ctx, ctx->args.values[opt_boards_root], ctx->base, true);
  } else
    directory = jh_cli_path(ctx, "boards", hal_root, false);
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_ok(ctx,
                    jh_board_registry_load(registry, hal_root, directory,
                                           16 * 1024 * 1024, &ctx->failure));
  return directory;
}

static const jh_board_descriptor_t *descriptor(const jb_vector_t *list,
                                               size_t index) {
  return jb_vector_const_at(list, index);
}

int jh_cli_board_list(jh_cli_t *ctx) {
  jh_board_registry_t registry = {0};
  jh_board_registry_init(&registry);
  const char *boards = jh_cli_load_boards(ctx, "board list", &registry);
  jb_string_t text = {0};
  (void)jh_cli_ok(ctx, jb_string_init(&text, jb_heap_allocator()));
  if (ctx->failure.status != HAL_OK)
    goto done;
  const jb_vector_t *lists[] = {&registry.targets, &registry.boards};
  if (!ctx->json) {
    hal_status_t status = HAL_OK;
    for (size_t l = 0; l < COUNTOF(lists) && status == HAL_OK; ++l)
      for (size_t i = 0; i < jb_vector_size(lists[l]) && status == HAL_OK;
           ++i) {
        jh_put(&text, &status, l == 0 ? "target " : "board ");
        jh_put_span(&text, &status,
                    jh_string_span(&descriptor(lists[l], i)->id));
        jh_put(&text, &status, "\n");
      }
    if (jh_cli_ok(ctx, status))
      (void)jh_cli_stdout(ctx, jh_string_span(&text));
    goto done;
  }
  size_t bytes =
      strlen(boards) + jh_cli_tree_bytes(ctx, registry.capabilities) + 65536;
  for (size_t l = 0; l < COUNTOF(lists) && ctx->failure.status == HAL_OK; ++l)
    for (size_t i = 0;
         i < jb_vector_size(lists[l]) && ctx->failure.status == HAL_OK; ++i) {
      const jh_board_descriptor_t *item = descriptor(lists[l], i);
      bytes +=
          jh_cli_tree_bytes(ctx, item->root) + jb_string_size(&item->path) + 64;
    }
  jh_result_begin(ctx, bytes, bytes / 4);
  jh_result_text(ctx, "command", "board.list");
  jh_result_text(ctx, "status", "HAL_OK");
  jh_result_string(ctx, "boardsRoot", jh_string_span(&registry.boards_root));
  for (size_t l = 0; l < COUNTOF(lists) && ctx->failure.status == HAL_OK; ++l) {
    jh_result_array(ctx, l == 0 ? "targets" : "boards");
    for (size_t i = 0;
         i < jb_vector_size(lists[l]) && ctx->failure.status == HAL_OK; ++i) {
      const jh_board_descriptor_t *item = descriptor(lists[l], i);
      jh_result_object(ctx, NULL);
      jh_result_string(ctx, "id", jh_string_span(&item->id));
      jh_result_string(ctx, "path", jh_string_span(&item->path));
      jh_result_tree(ctx, "descriptor", item->root);
      jh_result_object_end(ctx);
    }
    jh_result_array_end(ctx);
  }
  jh_result_tree(ctx, "capabilities", registry.capabilities);
  jh_result_array(ctx, "components");
  for (size_t i = 0; i < jb_vector_size(&registry.components) &&
                     ctx->failure.status == HAL_OK;
       ++i) {
    const jh_board_component_t *component =
        jb_vector_const_at(&registry.components, i);
    jh_result_object(ctx, NULL);
    jh_result_string(ctx, "id", jh_string_span(&component->id));
    jh_result_list(ctx, "providers", &component->providers);
    jh_result_string(ctx, "slot", jh_string_span(&component->slot));
    jh_result_object_end(ctx);
  }
  jh_result_array_end(ctx);
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_require(ctx, jh_board_write_tables(&ctx->writer), 4,
                         "cannot write the board registry tables");
  (void)jh_result_finish(ctx);
done:
  jb_string_destroy(&text);
  jh_board_registry_destroy(&registry);
  return 0;
}

int jh_cli_board_validate(jh_cli_t *ctx) {
  jh_board_registry_t registry = {0};
  jh_board_registry_init(&registry);
  const char *boards = jh_cli_load_boards(ctx, "board validate", &registry);
  const size_t targets = jb_vector_size(&registry.targets),
               count = jb_vector_size(&registry.boards);
  if (ctx->failure.status == HAL_OK) {
    if (!ctx->json)
      (void)jh_cli_stdout(
          ctx, jh_cli_join(ctx, JH_PARTS(jh_span("validated "),
                                         jh_span(jh_cli_size(ctx, targets)),
                                         jh_span(" targets and "),
                                         jh_span(jh_cli_size(ctx, count)),
                                         jh_span(" boards\n"))));
    else {
      jh_result_begin(ctx, strlen(boards) + 1024, 32);
      jh_result_text(ctx, "command", "board.validate");
      jh_result_text(ctx, "status", "HAL_OK");
      jh_result_string(ctx, "boardsRoot",
                       jh_string_span(&registry.boards_root));
      jh_result_number(ctx, "targets", targets);
      jh_result_number(ctx, "boards", count);
      (void)jh_result_finish(ctx);
    }
  }
  jh_board_registry_destroy(&registry);
  return 0;
}

int jh_cli_board_default(jh_cli_t *ctx) {
  jh_board_registry_t registry = {0};
  jh_board_registry_init(&registry);
  (void)jh_cli_load_boards(ctx, "board default", &registry);
  if (ctx->failure.status != HAL_OK)
    goto done;
  const jh_board_descriptor_t *selected =
      ctx->args.present[opt_target] && ctx->args.values[opt_target][0] != '\0'
          ? jh_board_registry_find(&registry.targets,
                                   jh_span(ctx->args.values[opt_target]))
          : NULL;
  if (!jh_cli_require(ctx, selected == NULL ? HAL_ECONFIG : HAL_OK, 2,
                      "board default requires a known --target"))
    goto done;
  jb_json_tree_view_t value = {0};
  jb_json_span_t board = {NULL, 0};
  if (!jh_cli_require(ctx,
                      jb_json_tree_find(selected->root, jh_span("defaultBoard"),
                                        &value) == HAL_OK &&
                              jb_json_tree_text(value, &board) == HAL_OK
                          ? HAL_OK
                          : HAL_EINTERNAL,
                      4, "cannot read the default board"))
    goto done;
  if (!ctx->json)
    (void)jh_cli_stdout(ctx, jh_cli_join(ctx, JH_PARTS(board, jh_span("\n"))));
  else {
    jh_result_begin(ctx, board.length + 1024, 32);
    jh_result_text(ctx, "command", "board.default");
    jh_result_text(ctx, "status", "HAL_OK");
    jh_result_string(ctx, "target", jh_string_span(&selected->id));
    jh_result_string(ctx, "board", board);
    (void)jh_result_finish(ctx);
  }
done:
  jh_board_registry_destroy(&registry);
  return 0;
}

int jh_cli_boards(jh_cli_t *ctx) {
  const char *hal_root = jh_cli_generator(ctx, "generate boards");
  jh_board_registry_t registry = {0};
  jh_board_registry_init(&registry);
  const char *boards = jh_cli_load_boards(ctx, "generate boards", &registry);
  const char *out_root =
      ctx->args.present[opt_output_root]
          ? jh_cli_path(ctx, ctx->args.values[opt_output_root], ctx->base,
                        false)
          : hal_root;
  size_t changed = 0;
  bool current = true;
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_ok(ctx,
                 jh_board_static_publish(&registry, hal_root, out_root,
                                         ctx->args.present[opt_check], &changed,
                                         &current, &ctx->failure)))
    goto done;
  if (!ctx->json) {
    if (ctx->args.present[opt_write_mode])
      (void)jh_cli_stdout(
          ctx,
          jh_cli_join(ctx, JH_PARTS(jh_span("generated 3 board artifacts ("),
                                    jh_span(jh_cli_size(ctx, changed)),
                                    jh_span(" changed)\n"))));
    else if (current)
      (void)jh_cli_stdout(ctx,
                          jh_span("verified 3 generated board artifacts\n"));
  } else {
    jh_result_begin(
        ctx, strlen(hal_root) + strlen(boards) + strlen(out_root) + 1024, 32);
    jh_result_text(ctx, "command", "generate.boards");
    jh_result_text(ctx, "status", current ? "HAL_OK" : "HAL_ESTATE");
    jh_result_text(ctx, "root", hal_root);
    jh_result_string(ctx, "boardsRoot", jh_string_span(&registry.boards_root));
    jh_result_text(ctx, "output", out_root);
    jh_result_number(ctx, "changed", changed);
    jh_result_boolean(ctx, "checkOnly", ctx->args.present[opt_check]);
    (void)jh_result_finish(ctx);
  }
done:
  jh_board_registry_destroy(&registry);
  return current ? 0 : 1;
}

static const char *optional_path(jh_cli_t *ctx, jh_option_id_t id,
                                 bool resolve) {
  return !ctx->args.present[id] || ctx->args.values[id][0] == '\0'
             ? NULL
             : jh_cli_path(ctx, ctx->args.values[id], ctx->base, resolve);
}

int jh_cli_board_config(jh_cli_t *ctx) {
  jh_board_registry_t registry = {0};
  jh_board_config_result_t result = {0};
  jh_board_registry_init(&registry);
  (void)jh_cli_load_boards(ctx, "board config", &registry);
  const char *hal_root = jh_cli_checkout(ctx);
  const char *directory = optional_path(ctx, opt_output_dir, true);
  const char *managed = optional_path(ctx, opt_output_root, true);
  const char *assembly = optional_path(ctx, opt_file, false);
  const jh_board_config_request_t request = {
      &registry,
      hal_root,
      jh_span(ctx->args.values[opt_target]),
      jh_span(ctx->args.values[opt_board_option]),
      &ctx->args.lists[opt_define],
      &ctx->args.lists[opt_feature_option],
      directory,
      managed,
      assembly,
      ctx->json,
      16 * 1024 * 1024};
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_require(ctx, jh_board_config_result_init(&result), 4,
                      "cannot start the board configuration") ||
      !jh_cli_ok(ctx, jh_board_configure(&request, &result, &ctx->failure)) ||
      !ctx->json)
    goto done;
  size_t bytes = jb_string_size(&result.output_dir) + 4096;
  for (size_t i = 0; i < JH_BOARD_OUTPUT_COUNT; ++i)
    bytes += jb_string_size(&result.outputs.content[i]) + 64;
  jh_result_begin(ctx, bytes, 32);
  jh_result_text(ctx, "command", "board.config");
  jh_result_text(ctx, "status", "HAL_OK");
  jh_result_string(ctx, "target", jh_string_span(&result.target));
  jh_result_string(ctx, "board", jh_string_span(&result.board));
  if (jb_string_size(&result.output_dir) != 0) {
    jh_result_string(ctx, "outputDir", jh_string_span(&result.output_dir));
    jh_result_number(ctx, "changed", result.changed);
  }
  jh_result_object(ctx, "files");
  for (size_t i = 0; i < JH_BOARD_OUTPUT_COUNT && ctx->failure.status == HAL_OK;
       ++i)
    jh_result_string(ctx, jh_board_output_names[i],
                     jh_string_span(&result.outputs.content[i]));
  jh_result_object_end(ctx);
  (void)jh_result_finish(ctx);
done:
  jh_board_config_result_destroy(&result);
  jh_board_registry_destroy(&registry);
  return 0;
}

static const char *load_clocks(jh_cli_t *ctx, jh_clock_registry_t *registry) {
  const char *hal_root = jh_cli_checkout(ctx);
  const char *directory;
  if (ctx->args.present[opt_clocks_dir]) {
    if (!jh_cli_value(ctx, opt_clocks_dir, "--clocks needs a path"))
      return "";
    directory =
        jh_cli_path(ctx, ctx->args.values[opt_clocks_dir], ctx->base, false);
  } else
    directory = jh_cli_path(ctx, "config/hardware/clocks", hal_root, false);
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_ok(ctx,
                    jh_clock_registry_load(registry, hal_root, directory,
                                           16 * 1024 * 1024, &ctx->failure));
  return directory;
}

int jh_cli_clock_check(jh_cli_t *ctx) {
  if (!jh_cli_value(ctx, opt_root, "clock check requires --root"))
    return 0;
  const size_t limit = 16 * 1024 * 1024;
  jb_vector_t sdkconfigs = {0}; /* Pairs of owned target and path strings. */
  (void)jh_cli_ok(ctx, jh_list_init(&sdkconfigs));
  jh_clock_registry_t clocks = {0};
  jh_board_registry_t boards = {0};
  jh_clock_registry_init(&clocks);
  jh_board_registry_init(&boards);
  const jb_vector_t *items = &ctx->args.lists[opt_esp_sdkconfig];
  for (size_t i = 0; i < jb_vector_size(items) && ctx->failure.status == HAL_OK;
       ++i) {
    const char *text = jb_string_data(jh_list_at(items, i)),
               *equal = strchr(text, '=');
    if (equal == NULL || equal == text || equal[1] == '\0') {
      (void)jh_cli_require(ctx, HAL_EINVAL, 2,
                           "--esp-sdkconfig needs TARGET=PATH");
      goto done;
    }
    const char *path = jh_cli_path(ctx, equal + 1, ctx->base, false);
    if (ctx->failure.status != HAL_OK ||
        !jh_cli_ok(ctx, jh_list_push(
                            &sdkconfigs,
                            (jb_json_span_t){text, (size_t)(equal - text)})) ||
        !jh_cli_ok(ctx, jh_list_push(&sdkconfigs, jh_span(path))))
      goto done;
  }
  const char *directory = load_clocks(ctx, &clocks);
  (void)jh_cli_load_boards(ctx, "clock check", &boards);
  const char *hal_root = jh_cli_checkout(ctx);
  const char *port =
      jh_cli_path(ctx, "src/hal/impl/stm32g474/port", hal_root, false);
  const char *sdk =
      jh_cli_path(ctx, "third_party/pico-sdk/src", hal_root, false);
  if (ctx->failure.status != HAL_OK ||
      !jh_cli_ok(ctx,
                 jh_clock_check_registry(&clocks, &boards, &ctx->failure)) ||
      !jh_cli_ok(ctx, jh_clock_check_boards(&clocks, &boards, &ctx->failure)) ||
      !jh_cli_ok(
          ctx, jh_clock_check_stm32g474(&clocks, port, limit, &ctx->failure)) ||
      !jh_cli_ok(ctx,
                 jh_clock_check_pico_sdk(&clocks, sdk, limit, &ctx->failure)) ||
      !jh_cli_ok(
          ctx, jh_clock_check_no_hal_override(hal_root, limit, &ctx->failure)))
    goto done;
  for (size_t i = 0;
       i < jb_vector_size(&sdkconfigs) && ctx->failure.status == HAL_OK; i += 2)
    (void)jh_cli_ok(ctx,
                    jh_clock_check_esp_sdkconfig(
                        &clocks, jh_string_span(jh_list_at(&sdkconfigs, i)),
                        jb_string_data(jh_list_at(&sdkconfigs, i + 1)), limit,
                        &ctx->failure));
  const size_t targets = jb_vector_size(&clocks.files),
               trees = jh_clock_registry_tree_count(&clocks);
  if (!ctx->json)
    (void)jh_cli_stdout(
        ctx, jh_cli_join(ctx, JH_PARTS(jh_span("[OK] clock registry: "),
                                       jh_span(jh_cli_size(ctx, targets)),
                                       jh_span(" targets, "),
                                       jh_span(jh_cli_size(ctx, trees)),
                                       jh_span(" trees\n"))));
  else {
    jh_result_begin(ctx, strlen(directory) + 1024, 32);
    jh_result_text(ctx, "command", "clock.check");
    jh_result_text(ctx, "status", "HAL_OK");
    jh_result_text(ctx, "clocks", directory);
    jh_result_number(ctx, "targets", targets);
    jh_result_number(ctx, "trees", trees);
    jh_result_number(ctx, "espSdkconfigs", jb_vector_size(&sdkconfigs) / 2);
    (void)jh_result_finish(ctx);
  }
done:
  jh_board_registry_destroy(&boards);
  jh_clock_registry_destroy(&clocks);
  jh_list_destroy(&sdkconfigs);
  return 0;
}

static hal_status_t write_board_source(void *user, jb_json_span_t board,
                                       jb_json_span_t target,
                                       jb_json_span_t tree,
                                       const jb_vector_t *errors) {
  jh_cli_t *ctx = user;
  jh_result_object(ctx, NULL);
  jh_result_string(ctx, "board", board);
  jh_result_string(ctx, "target", target);
  jh_result_string(ctx, "tree", tree);
  jh_result_array(ctx, "errors");
  for (size_t i = 0;
       i < jb_vector_size(errors) && ctx->failure.status == HAL_OK; ++i) {
    const jh_clock_source_error_t *error = jb_vector_const_at(errors, i);
    jh_result_object(ctx, NULL);
    jh_result_string(ctx, "pointer", jh_string_span(&error->pointer));
    jh_result_string(ctx, "message", jh_string_span(&error->message));
    jh_result_object_end(ctx);
  }
  jh_result_array_end(ctx);
  jh_result_object_end(ctx);
  return ctx->failure.status == HAL_OK ? HAL_OK : HAL_ENOMEM;
}

int jh_cli_clock_list(jh_cli_t *ctx) {
  if (!jh_cli_value(ctx, opt_root, "clock list requires --root"))
    return 0;
  jh_clock_registry_t clocks = {0};
  jh_clock_registry_init(&clocks);
  jb_string_t text = {0};
  (void)jh_cli_ok(ctx, jb_string_init(&text, jb_heap_allocator()));
  const char *directory = load_clocks(ctx, &clocks);
  if (ctx->failure.status != HAL_OK)
    goto done;
  if (!ctx->json) {
    hal_status_t status = HAL_OK;
    for (size_t i = 0; i < jb_vector_size(&clocks.files) && status == HAL_OK;
         ++i) {
      const jh_clock_file_t *file = jb_vector_const_at(&clocks.files, i);
      jb_json_tree_view_t trees = {0};
      (void)jh_json_member(file->root, "trees", &trees);
      for (size_t t = 0; t < jh_json_count(trees) && status == HAL_OK; ++t) {
        jh_put_span(&text, &status, jh_string_span(&file->target));
        jh_put(&text, &status, " ");
        jh_put_span(&text, &status, jh_json_key(jh_json_item(trees, t)));
        jh_put(&text, &status, "\n");
      }
    }
    if (jh_cli_ok(ctx, status))
      (void)jh_cli_stdout(ctx, jh_string_span(&text));
    goto done;
  }
  const char *hal_root = jh_cli_checkout(ctx);
  const char *boards;
  if (ctx->args.present[opt_boards_root]) {
    if (!jh_cli_value(ctx, opt_boards_root, "--boards-root needs a path"))
      goto done;
    boards =
        jh_cli_path(ctx, ctx->args.values[opt_boards_root], ctx->base, false);
  } else
    boards = jh_cli_path(ctx, "boards", hal_root, false);
  size_t bytes = strlen(directory) + strlen(boards) + 1024 * 1024;
  for (size_t i = 0;
       i < jb_vector_size(&clocks.files) && ctx->failure.status == HAL_OK;
       ++i) {
    const jh_clock_file_t *file = jb_vector_const_at(&clocks.files, i);
    bytes +=
        jh_cli_tree_bytes(ctx, file->root) + jb_string_size(&file->path) + 64;
  }
  jh_result_begin(ctx, bytes, bytes / 4);
  jh_result_text(ctx, "command", "clock.list");
  jh_result_text(ctx, "status", "HAL_OK");
  jh_result_text(ctx, "clocks", directory);
  jh_result_array(ctx, "targets");
  for (size_t i = 0;
       i < jb_vector_size(&clocks.files) && ctx->failure.status == HAL_OK;
       ++i) {
    const jh_clock_file_t *file = jb_vector_const_at(&clocks.files, i);
    jh_result_object(ctx, NULL);
    jh_result_string(ctx, "id", jh_string_span(&file->target));
    jh_result_string(ctx, "path", jh_string_span(&file->path));
    jh_result_tree(ctx, "registry", file->root);
    jh_result_object_end(ctx);
  }
  jh_result_array_end(ctx);
  jh_result_object(ctx, "stm32g474TreeComponents");
  for (size_t i = 0;
       i < jh_clock_stm32g474_tree_count && ctx->failure.status == HAL_OK;
       ++i) {
    if (jh_clock_stm32g474_trees[i].component == NULL)
      jh_result_null(ctx, jh_clock_stm32g474_trees[i].tree);
    else
      jh_result_text(ctx, jh_clock_stm32g474_trees[i].tree,
                     jh_clock_stm32g474_trees[i].component);
  }
  jh_result_object_end(ctx);
  jh_result_array(ctx, "boardSources");
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_ok(
        ctx, jh_clock_board_sources(&clocks, boards, 16 * 1024 * 1024,
                                    write_board_source, ctx, &ctx->failure));
  jh_result_array_end(ctx);
  (void)jh_result_finish(ctx);
done:
  jb_string_destroy(&text);
  jh_clock_registry_destroy(&clocks);
  return 0;
}
