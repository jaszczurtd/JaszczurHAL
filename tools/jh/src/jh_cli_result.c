#include "jh_cli_internal.h"
#include <string.h>

bool jh_cli_ok(jh_cli_t *ctx, hal_status_t status) {
  if (status != HAL_OK && ctx->failure.status == HAL_OK)
    (void)jh_fail_storage(&ctx->failure, status);
  return ctx->failure.status == HAL_OK;
}

bool jh_cli_require(jh_cli_t *ctx, hal_status_t status, int exit_code,
                    const char *message) {
  if (ctx->failure.status == HAL_OK)
    (void)jh_require(&ctx->failure, status, exit_code, message);
  return ctx->failure.status == HAL_OK;
}

bool jh_cli_error(jh_cli_t *ctx, hal_status_t status, int exit_code,
                  const jb_json_span_t *parts, size_t count) {
  if (ctx->failure.status == HAL_OK)
    (void)jh_fail(&ctx->failure, status, exit_code, parts, count);
  return false;
}

bool jh_cli_value(jh_cli_t *ctx, jh_option_id_t option, const char *message) {
  return jh_cli_require(ctx,
                        ctx->args.present[option] &&
                                ctx->args.values[option][0] != '\0'
                            ? HAL_OK
                            : HAL_EINVAL,
                        2, message);
}

jb_json_span_t jh_cli_join(jh_cli_t *ctx, const jb_json_span_t *parts,
                           size_t count) {
  if (ctx->failure.status == HAL_OK &&
      jh_cli_ok(ctx, jh_list_push_parts(&ctx->strings, parts, count)))
    return jh_string_span(
        jh_list_at(&ctx->strings, jb_vector_size(&ctx->strings) - 1));
  return jh_span("");
}

const char *jh_cli_text(jh_cli_t *ctx, const jb_json_span_t *parts,
                        size_t count) {
  return jh_cli_join(ctx, parts, count).data;
}

const char *jh_cli_size(jh_cli_t *ctx, size_t value) {
  char text[3 * sizeof(size_t) + 1];
  (void)snprintf(text, sizeof text, "%zu", value);
  return jh_cli_text(ctx, JH_PARTS(jh_span(text)));
}

const char *jh_cli_path(jh_cli_t *ctx, const char *path, const char *base,
                        bool resolve) {
  jb_string_t text = {0};
  (void)jh_cli_ok(ctx, jb_string_init(&text, jb_heap_allocator()));
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_ok(
        ctx, resolve ? jh_python_resolve(path, base, &text, &ctx->failure)
                     : jh_absolute_path(path, base, &text, &ctx->failure));
  const char *result = jh_cli_text(ctx, JH_PARTS(jh_string_span(&text)));
  jb_string_destroy(&text);
  return result;
}

const char *jh_cli_checkout(jh_cli_t *ctx) {
  const char *path =
      jh_cli_path(ctx, ctx->args.values[opt_root], ctx->base, false);
  size_t length = strlen(path);
  if (length > 1 && (path[length - 1] == '/' || path[length - 1] == '\\') &&
      !(length == 3 && path[1] == ':'))
    --length;
  return jh_cli_text(ctx, JH_PARTS(((jb_json_span_t){path, length})));
}

const char *jh_cli_generator(jh_cli_t *ctx, const char *command) {
  const char *message =
      jh_cli_text(ctx, JH_PARTS(jh_span(command), jh_span(" requires --root")));
  if (!jh_cli_value(ctx, opt_root, message))
    return "";
  if (ctx->args.present[opt_write_mode] == ctx->args.present[opt_check]) {
    (void)jh_cli_error(
        ctx, HAL_EINVAL, 2,
        JH_PARTS(jh_span(command),
                 jh_span(" requires exactly one of --write and --check")));
    return "";
  }
  return jh_cli_checkout(ctx);
}

const char *jh_cli_feature_config(jh_cli_t *ctx, const char *command) {
  if (ctx->args.present[opt_config_root]) {
    if (!jh_cli_value(ctx, opt_config_root, "--config-root needs a path"))
      return "";
    return jh_cli_path(ctx, ctx->args.values[opt_config_root], ctx->base,
                       false);
  }
  const char *message =
      jh_cli_text(ctx, JH_PARTS(jh_span(command),
                                jh_span(" requires --root or --config-root")));
  if (!jh_cli_value(ctx, opt_root, message))
    return "";
  return jh_cli_path(ctx, "config", jh_cli_checkout(ctx), false);
}

jb_json_span_t jh_cli_repr(jh_cli_t *ctx, jb_json_span_t value) {
  jb_string_t text = {0};
  (void)jh_cli_ok(ctx, jb_string_init(&text, jb_heap_allocator()));
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_ok(ctx, jh_python_repr_append(&text, value));
  const jb_json_span_t result =
      jh_cli_join(ctx, JH_PARTS(jh_string_span(&text)));
  jb_string_destroy(&text);
  return result;
}

bool jh_cli_stdout(jh_cli_t *ctx, jb_json_span_t text) {
  if (ctx->failure.status != HAL_OK)
    return false;
  const bool written =
      fwrite(text.data, 1, text.length, stdout) == text.length &&
      fflush(stdout) == 0;
  return jh_cli_require(ctx, written ? HAL_OK : HAL_EIO, 3,
                        "cannot write stdout");
}

bool jh_cli_source(jh_cli_t *ctx, const char *text) {
  return ctx->failure.status == HAL_OK &&
         jh_cli_ok(ctx,
                   jb_string_assign(&ctx->location.source, text, strlen(text)));
}

size_t jh_cli_count(jh_cli_t *ctx, jh_option_id_t option, size_t fallback,
                    size_t maximum, const char *message) {
  if (!ctx->args.present[option] || ctx->failure.status != HAL_OK)
    return fallback;
  const char *text = ctx->args.values[option];
  size_t value = 0;
  bool valid = text[0] != '\0';
  for (size_t i = 0; valid && text[i] != '\0'; ++i) {
    const unsigned digit = (unsigned char)text[i] - (unsigned)'0';
    if (digit > 9 || value > maximum / 10 ||
        (value == maximum / 10 && digit > maximum % 10))
      valid = false;
    else
      value = value * 10 + digit;
  }
  (void)jh_cli_require(ctx, valid ? HAL_OK : HAL_EINVAL, 2, message);
  return value;
}

size_t jh_cli_limit(jh_cli_t *ctx) {
  return jh_cli_count(ctx, opt_max_bytes, 16 * 1024 * 1024, 64 * 1024 * 1024,
                      "--max-bytes requires an integer between 0 and 67108864");
}

size_t jh_cli_list_bytes(const jb_vector_t *list) {
  size_t bytes = 0;
  for (size_t i = 0; i < jb_vector_size(list); ++i)
    bytes += jb_string_size(jh_list_at(list, i)) + 8;
  return bytes;
}

static hal_status_t discard_bytes(void *user, const char *data, size_t length) {
  (void)user;
  (void)data;
  (void)length;
  return HAL_OK;
}

size_t jh_cli_tree_bytes(jh_cli_t *ctx, jb_json_tree_view_t tree) {
  const jb_json_tree_format_t compact = {64 * 1024 * 1024, 0, false, false};
  size_t written = 0;
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_require(
        ctx, jb_json_tree_format(tree, &compact, discard_bytes, NULL, &written),
        4, "cannot measure the board registry");
  return written;
}

jb_json_span_t jh_cli_lines(jh_cli_t *ctx, const jb_vector_t *list,
                            const char *prefix) {
  jb_string_t text = {0};
  hal_status_t status = jb_string_init(&text, jb_heap_allocator());
  for (size_t i = 0; i < jb_vector_size(list) && status == HAL_OK; ++i) {
    jh_put(&text, &status, prefix);
    jh_put_span(&text, &status, jh_string_span(jh_list_at(list, i)));
    jh_put(&text, &status, "\n");
  }
  (void)jh_cli_ok(ctx, status);
  const jb_json_span_t result =
      jh_cli_join(ctx, JH_PARTS(jh_string_span(&text)));
  jb_string_destroy(&text);
  return result;
}

void jh_result_begin(jh_cli_t *ctx, size_t bytes, size_t keys) {
  if (ctx->failure.status != HAL_OK)
    return;
  if (bytes > (SIZE_MAX - 1024 * 1024) / 32 || keys > SIZE_MAX - 32) {
    (void)jh_cli_require(ctx, HAL_EOVERFLOW, 4, "result workspace overflow");
    return;
  }
  const jb_json_writer_limits_t limits = {bytes * 6 + 1024 * 1024, 257,
                                          keys + 32, bytes + 65536,
                                          bytes * 32 + 1024 * 1024};
  size_t needed = 0;
  if (!jh_cli_require(ctx, jb_json_writer_workspace_size(&limits, &needed), 4,
                      "result workspace overflow") ||
      !jh_cli_ok(ctx, jb_vector_resize(&ctx->workspace, needed)))
    return;
  jb_string_clear(&ctx->result);
  if (!jh_cli_require(
          ctx,
          jb_json_writer_init(
              &ctx->writer, &limits,
              (jb_json_workspace_t){jb_vector_data(&ctx->workspace), needed},
              jh_string_sink, &ctx->result),
          4, "cannot initialize result writer"))
    return;
  if (!jh_cli_require(ctx, jb_json_writer_object_begin(&ctx->writer), 4,
                      "cannot start result"))
    return;
  jh_result_number(ctx, "formatVersion", 1);
}

void jh_result_key(jh_cli_t *ctx, const char *name) {
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_require(ctx, jb_json_writer_key(&ctx->writer, jh_span(name)),
                         4, "cannot write result key");
}

void jh_result_string(jh_cli_t *ctx, const char *name, jb_json_span_t value) {
  jh_result_key(ctx, name);
  jh_result_item(ctx, value);
}

void jh_result_text(jh_cli_t *ctx, const char *name, const char *value) {
  jh_result_string(ctx, name, jh_span(value));
}

void jh_result_number(jh_cli_t *ctx, const char *name, size_t value) {
  jh_result_key(ctx, name);
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_require(ctx, jb_json_writer_u64(&ctx->writer, value), 4,
                         "cannot write result number");
}

void jh_result_boolean(jh_cli_t *ctx, const char *name, bool value) {
  jh_result_key(ctx, name);
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_require(ctx, jb_json_writer_bool(&ctx->writer, value), 4,
                         "cannot write result flag");
}

void jh_result_null(jh_cli_t *ctx, const char *name) {
  jh_result_key(ctx, name);
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_require(ctx, jb_json_writer_null(&ctx->writer), 4,
                         "cannot write result null");
}

void jh_result_array(jh_cli_t *ctx, const char *name) {
  if (name != NULL)
    jh_result_key(ctx, name);
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_require(ctx, jb_json_writer_array_begin(&ctx->writer), 4,
                         "cannot start result array");
}

void jh_result_array_end(jh_cli_t *ctx) {
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_require(ctx, jb_json_writer_array_end(&ctx->writer), 4,
                         "cannot close result array");
}

void jh_result_object(jh_cli_t *ctx, const char *name) {
  if (name != NULL)
    jh_result_key(ctx, name);
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_require(ctx, jb_json_writer_object_begin(&ctx->writer), 4,
                         "cannot start result object");
}

void jh_result_object_end(jh_cli_t *ctx) {
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_require(ctx, jb_json_writer_object_end(&ctx->writer), 4,
                         "cannot close result object");
}

void jh_result_item(jh_cli_t *ctx, jb_json_span_t value) {
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_require(ctx, jb_json_writer_string(&ctx->writer, value), 4,
                         "cannot write result string");
}

void jh_result_list(jh_cli_t *ctx, const char *name, const jb_vector_t *list) {
  jh_result_array(ctx, name);
  for (size_t i = 0; i < jb_vector_size(list) && ctx->failure.status == HAL_OK;
       ++i)
    jh_result_item(ctx, jh_string_span(jh_list_at(list, i)));
  jh_result_array_end(ctx);
}

void jh_result_tree(jh_cli_t *ctx, const char *name,
                    jb_json_tree_view_t value) {
  if (name != NULL)
    jh_result_key(ctx, name);
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_require(ctx, jh_json_write_tree(&ctx->writer, value), 4,
                         "cannot write result tree");
}

void jh_result_process(jh_cli_t *ctx) {
  jh_result_boolean(ctx, "started", ctx->process.started);
  jh_result_boolean(ctx, "exited", ctx->process.exited);
  jh_result_number(ctx, "exitCode", ctx->process.exit_code);
  jh_result_number(ctx, "termSignal", (size_t)ctx->process.term_signal);
  jh_result_number(ctx, "nativeError", ctx->process.native_error);
}

bool jh_result_finish(jh_cli_t *ctx) {
  if (ctx->failure.status != HAL_OK)
    return false;
  size_t count = 0;
  return jh_cli_require(ctx, jb_json_writer_object_end(&ctx->writer), 4,
                        "cannot close result") &&
         jh_cli_require(ctx, jb_json_writer_finish(&ctx->writer, &count), 4,
                        "cannot finish result") &&
         jh_cli_ok(ctx, jh_append(&ctx->result, "\n")) &&
         jh_cli_stdout(ctx, jh_string_span(&ctx->result));
}
