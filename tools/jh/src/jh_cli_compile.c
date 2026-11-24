/* jh compile: the model and plan of a VS Code firmware build and its
 * configure step (jh_compile.h). C11. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "jh_cli_internal.h"
#include "jh_compile.h"
#include "jh_process_support.h"
#include <jb/filesystem.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* The path of this executable, or an empty string when the system cannot
 * name it. */
static const char *self_path(jh_cli_t *ctx) {
#ifdef _WIN32
  wchar_t wide[32768];
  const DWORD length = GetModuleFileNameW(NULL, wide, 32768);
  if (length == 0 || length >= 32768)
    return "";
  char text[4 * 32768];
  const int bytes =
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, (int)length,
                          text, (int)sizeof text, NULL, NULL);
  return bytes > 0 ? jh_cli_text(
                         ctx, JH_PARTS(((jb_json_span_t){text, (size_t)bytes})))
                   : "";
#elif defined(__linux__)
  char text[4096];
  const ssize_t length = readlink("/proc/self/exe", text, sizeof text);
  return length > 0 && (size_t)length < sizeof text
             ? jh_cli_text(ctx,
                           JH_PARTS(((jb_json_span_t){text, (size_t)length})))
             : "";
#else
  (void)ctx;
  return "";
#endif
}

/* Path equality of two resolved paths: case-insensitive on Windows. */
static bool same_path(const char *a, const char *b) {
#ifdef _WIN32
  return _stricmp(a, b) == 0;
#else
  return strcmp(a, b) == 0;
#endif
}

static const char *environment(jh_cli_t *ctx, const char *name) {
  jb_string_t value = {0};
  bool found = false;
  const char *result = "";
  if (jh_cli_ok(ctx, jb_string_init(&value, jb_heap_allocator())) &&
      jh_cli_ok(ctx, jh_process_environment(name, &value, &found)) && found)
    result = jh_cli_text(ctx, JH_PARTS(jh_string_span(&value)));
  jb_string_destroy(&value);
  return result;
}

/* The jh the configure verifies: JH_EXECUTABLE when it names a file of this
 * checkout (JH_EXECUTABLE_ROOT empty or this checkout), else this program. */
static const char *jh_executable(jh_cli_t *ctx, const char *root) {
  const char *given = environment(ctx, "JH_EXECUTABLE");
  const char *given_root = environment(ctx, "JH_EXECUTABLE_ROOT");
  jb_fs_info_t info = {0};
  if (given[0] != '\0' && jb_fs_status(given, true, &info) == HAL_OK &&
      info.kind == JB_FS_REGULAR &&
      (given_root[0] == '\0' ||
       same_path(jh_cli_path(ctx, given_root, ctx->base, true),
                 jh_cli_path(ctx, root, ctx->base, true))))
    return given;
  return self_path(ctx);
}

int jh_cli_compile(jh_cli_t *ctx) {
  const jh_arguments_t *a = &ctx->args;
  const bool dry_run = a->present[opt_dry_run];
  const bool configure = a->present[opt_configure_only];
  if (!jh_cli_value(ctx, opt_root, "compile requires --root") ||
      !jh_cli_value(ctx, opt_project, "compile requires --project") ||
      !jh_cli_require(ctx, dry_run || configure ? HAL_OK : HAL_EUNSUPPORTED, 2,
                      "compile runs with --dry-run or --configure-only") ||
      !jh_cli_require(
          ctx, dry_run && configure ? HAL_EINVAL : HAL_OK, 2,
          "compile takes --dry-run or --configure-only, not both") ||
      (configure && !jh_cli_install_cancellation(ctx)))
    return 0;
  const char *root = jh_cli_checkout(ctx);
  if (ctx->failure.status != HAL_OK)
    return 0;
  jh_compile_options_t options = {0};
  options.root = root;
  options.base = ctx->base;
  options.project = a->values[opt_project];
  options.target = a->values[opt_target];
  options.board = a->values[opt_board_option];
  options.assembly = a->values[opt_assembly];
  options.variant = a->values[opt_variant_option];
  options.port = a->values[opt_port];
  options.bootsel_volume = a->values[opt_bootsel_volume];
  options.python = a->values[opt_python];
  options.jh = jh_executable(ctx, root);
  options.verbose = a->present[opt_verbose];
  options.debug = a->present[opt_debug];
  /* With --json stdout carries the result, so CMake writes to stderr. */
  options.log = ctx->json ? stderr : stdout;
  options.canceled = configure ? jh_cli_canceled : NULL;
  if (ctx->failure.status != HAL_OK)
    return 0;
  jh_compile_t compile;
  jh_compile_init(&compile);
  if (!jh_cli_ok(ctx, jh_compile_plan(&options, &compile, &ctx->failure)) ||
      (configure && !jh_cli_ok(ctx, jh_compile_configure(&options, &compile,
                                                         &ctx->failure))))
    goto done;
  jb_json_tree_view_t config = {0}, plan = {0};
  (void)jb_json_tree_root(compile.config, &config);
  (void)jb_json_tree_root(compile.plan, &plan);
  if (ctx->json) {
    jh_result_begin(ctx,
                    jh_cli_tree_bytes(ctx, config) +
                        jh_cli_tree_bytes(ctx, plan) + 1024,
                    32);
    jh_result_text(ctx, "command", "compile");
    jh_result_text(ctx, "status", "HAL_OK");
    jh_result_tree(ctx, "config", config);
    jh_result_tree(ctx, "plan", plan);
    (void)jh_result_finish(ctx);
  } else if (dry_run) {
    jb_string_t text = {0};
    hal_status_t status = jb_string_init(&text, jb_heap_allocator());
    static const char *const steps[] = {"configure", "build", "artifacts"};
    for (size_t i = 0; i < 3 && status == HAL_OK; ++i) {
      const jb_json_tree_view_t argv = jh_json_field(plan, steps[i]);
      if (argv.tree != NULL && jh_json_type(argv) == JB_JSON_ARRAY &&
          jh_json_count(argv) != 0 &&
          jh_json_type(jh_json_item(argv, 0)) == JB_JSON_STRING &&
          !(i == 2 && jh_json_field(plan, "configure").tree != NULL))
        status = jh_compile_command_line(argv, &text);
    }
    if (jh_cli_ok(ctx, status))
      (void)jh_cli_stdout(ctx, jh_string_span(&text));
    jb_string_destroy(&text);
  }
done:
  jh_compile_destroy(&compile);
  return 0;
}
