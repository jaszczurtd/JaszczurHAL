#include "jh_cli_internal.h"
#include <jb/utf8.h>
#include <signal.h>
#include <string.h>
#ifdef _WIN32
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <wchar.h>
#include <windows.h>
#endif

typedef struct {
  const char *name, *value, *description;
} option_spec_t;
static const option_spec_t options[] = {
    {"file", "<path>",
     "Read this JSON file (feature resolve: the requests with their sources; "
     "board config: the values an assembly owns)"},
    {"schema", "<path>", "Validate with this local schema (json validate)"},
    {"pointer", "<path>", "Select a plain RFC 6901 pointer (default: root)"},
    {"output", "<path>", "Atomically write the selected JSON value"},
    {"root", "<path>", "JaszczurHAL checkout the generator reads"},
    {"bindings", "<path>",
     "Directory of HAL types (default: <root>/config/hardware/bindings)"},
    {"output-root", "<path>",
     "Root the generated artifact paths are relative to (default: <root>)"},
    {"config-root", "<path>",
     "Configuration the feature registry reads (default: <root>/config)"},
    {"write", NULL, "Refresh the generated artifact"},
    {"check", NULL,
     "Compare the output without changing files; exit 1 on drift"},
    {"max-bytes", "<count>",
     "Maximum input bytes (default 16777216, maximum 67108864)"},
    {"git", "<path>", "Absolute Git executable path (repository files)"},
    {"timeout-ms", "<count>", "Child process timeout; zero waits indefinitely"},
    {"header", "<path>",
     "Project hal_project_config.h to evaluate (config inspect)"},
    {"define", "<NAME[=VALUE]>",
     "Predefine a macro before the header; repeatable (config inspect)"},
    {"preinclude", "<path>",
     "Header read before the project header, like jh_hardware.h"},
    {"context", "<text>",
     "Configuration the resolution findings name (default: command line)"},
    {"target", "<id>",
     "Target whose required features join the resolution (board default: "
     "the target whose default board to print)"},
    {"request", "<SYMBOL[=VALUE]>",
     "Request a feature; repeatable (feature resolve)"},
    {"source", "<text>",
     "Source of the request at the same position; repeatable"},
    {"required-feature", "<SYMBOL[=1]>",
     "Feature the target requires; repeatable, needs --target"},
    {"input-root", "<path>",
     "File or directory the feature lint reads; repeatable (default: <root>)"},
    {"report-only", NULL, "Report lint findings as warnings, exit 0"},
    {"include-hardware-fixtures", NULL,
     "Also lint tests/hardware (hardware-fixture checks only)"},
    {"label", "<text>",
     "Name of the manifest in findings (config manifest; default: path)"},
    {"boards-root", "<path>",
     "Board registry directory the board commands read (default: "
     "<root>/boards)"},
    {"clocks", "<path>",
     "Clock registry directory the clock commands read (default: "
     "<root>/config/hardware/clocks)"},
    {"esp-sdkconfig", "<TARGET=PATH>",
     "Also check the sdkconfig an ESP-IDF build of TARGET wrote; repeatable "
     "(clock check)"},
    {"board", "<id>",
     "Board to configure (board config; default: the target's default board)"},
    {"feature", "<SYMBOL[=1]>",
     "Request a HAL feature for the board configuration; repeatable"},
    {"output-dir", "<path>",
     "Directory the board configuration writes, below a .build directory or "
     "--output-root"},
    {"config-dir", "<path>", "Directory containing hal_project_config.h"},
    {"variant", "<id>", "Declared project variant to build"},
    {"hardware-header", "<path>",
     "Generated jh_hardware.h read before the project"},
    {"selector", "<macro>", "Explicit target selector (config build adapter)"},
    {"json", NULL, "Write a versioned JSON result on stdout"},
    {"json-version", "<major>",
     "Require a supported JSON result major version (1)"},
    {"help", NULL, "Show help"},
    {"version", NULL, "Show the tool version"},
};
JB_STATIC_ASSERT(COUNTOF(options) == opt_count, "option table size");

static const jh_command_spec_t commands[] = {
    {"json",
     "inspect",
     "json.inspect",
     {opt_file, opt_pointer, opt_output, opt_check, opt_max_bytes, opt_count}},
    {"json",
     "validate",
     "json.validate",
     {opt_file, opt_schema, opt_max_bytes, opt_count}},
    {"generate",
     "device-defaults",
     "generate.device-defaults",
     {opt_root, opt_bindings, opt_output_root, opt_write_mode, opt_check,
      opt_count}},
    {"generate",
     "features",
     "generate.features",
     {opt_root, opt_config_root, opt_output_root, opt_write_mode, opt_check,
      opt_count}},
    {"repository",
     "files",
     "repository.files",
     {opt_root, opt_git, opt_timeout_ms, opt_max_bytes, opt_count}},
    {"config",
     "inspect",
     "config.inspect",
     {opt_header, opt_define, opt_preinclude, opt_count}},
    {"config", "directives", "config.directives", {opt_file, opt_count}},
    {"feature", "list", "feature.list", {opt_root, opt_config_root, opt_count}},
    {"feature",
     "resolve",
     "feature.resolve",
     {opt_root, opt_config_root, opt_context_label, opt_target, opt_request,
      opt_source, opt_required_feature, opt_file, opt_count}},
    {"feature",
     "lint",
     "feature.lint",
     {opt_root, opt_config_root, opt_input_root, opt_report_only,
      opt_include_hardware, opt_count}},
    {"config", "manifest", "config.manifest", {opt_file, opt_label, opt_count}},
    {"board", "list", "board.list", {opt_root, opt_boards_root, opt_count}},
    {"board",
     "validate",
     "board.validate",
     {opt_root, opt_boards_root, opt_count}},
    {"board",
     "default",
     "board.default",
     {opt_root, opt_boards_root, opt_target, opt_count}},
    {"clock",
     "check",
     "clock.check",
     {opt_root, opt_boards_root, opt_clocks_dir, opt_esp_sdkconfig, opt_count}},
    {"clock",
     "list",
     "clock.list",
     {opt_root, opt_boards_root, opt_clocks_dir, opt_count}},
    {"generate",
     "boards",
     "generate.boards",
     {opt_root, opt_boards_root, opt_output_root, opt_write_mode, opt_check,
      opt_count}},
    {"board",
     "config",
     "board.config",
     {opt_root, opt_boards_root, opt_target, opt_board_option, opt_define,
      opt_feature_option, opt_output_dir, opt_output_root, opt_file,
      opt_count}},
    {"config",
     "build",
     "config.build",
     {opt_root, opt_target, opt_selector_option, opt_required_feature,
      opt_project_directory, opt_header, opt_variant_option, opt_define,
      opt_hardware_header, opt_count}},
    {"config",
     "cmake",
     "config.cmake",
     {opt_root, opt_target, opt_project_directory, opt_variant_option,
      opt_define, opt_hardware_header, opt_output, opt_count}},
};

static const char usage[] =
    "Usage: jh [--help | --version] [--json]\n"
    "       jh json inspect --file <path> [options]\n"
    "       jh json validate --file <path> --schema <path> [--json]\n"
    "       jh generate device-defaults --root <path> (--check | --write)"
    " [options]\n"
    "       jh generate features --root <path> (--check | --write)"
    " [options]\n"
    "       jh generate boards --root <path> (--check | --write)"
    " [--boards-root <path>] [--output-root <path>]\n"
    "       jh repository files --root <path> --git <executable> "
    "[options]\n"
    "       jh config inspect [--header <path>] [--define <NAME[=VALUE]>]..."
    " [--preinclude <path>]\n"
    "       jh config directives --file <path>\n"
    "       jh config build --root <path> --target <id> [options] --json\n"
    "       jh config cmake --root <path> --target <id> --output <path> "
    "[options]\n"
    "       jh feature list (--root <path> | --config-root <path>)\n"
    "       jh feature resolve (--root <path> | --config-root <path>)"
    " [--request <SYMBOL[=VALUE]> [--source <text>]]... [options]\n"
    "       jh feature resolve (--root <path> | --config-root <path>)"
    " --file <path> [options]\n"
    "       jh feature lint (--root <path> | --config-root <path>)"
    " [--input-root <path>]... [options]\n"
    "       jh config manifest --file <path> [--label <text>]\n"
    "       jh board (list | validate) --root <path> [--boards-root <path>]\n"
    "       jh board default --root <path> [--boards-root <path>]"
    " --target <id>\n"
    "       jh board config --root <path> --target <id> [--board <id>]"
    " (--output-dir <path> | --json) [options]\n"
    "       jh clock check --root <path> [--boards-root <path>]"
    " [--clocks <path>] [--esp-sdkconfig <TARGET=PATH>]...\n"
    "       jh clock list --root <path> [--boards-root <path>]"
    " [--clocks <path>]\n\n"
    "Read JSON, select a value and optionally compare or write it,\n"
    "generate the C defaults of the HAL component types or the feature\n"
    "registry artifacts, evaluate a project's hal_project_config.h as\n"
    "the build reads it, or list the feature registry, resolve the\n"
    "features one build requests and lint the feature requests of project\n"
    "headers and manifests, validate and list the board registry,\n"
    "generate its artifacts and the configuration of one build, or\n"
    "check and list the clock tree registry.\n";

static void arguments_init(jh_arguments_t *args) {
  memset(args, 0, sizeof *args);
  for (size_t i = 0; i < opt_count; ++i) {
    args->values[i] = "";
    (void)jh_list_init(&args->lists[i]);
  }
  (void)jh_list_init(&args->command);
}

static void arguments_destroy(jh_arguments_t *args) {
  for (size_t i = 0; i < opt_count; ++i)
    jh_list_destroy(&args->lists[i]);
  jh_list_destroy(&args->command);
}

static void parse_arguments(jh_cli_t *ctx, size_t argc,
                            const char *const *argv) {
  jh_arguments_t parsed;
  arguments_init(&parsed);
  bool positional = false;
  for (size_t i = 1; i < argc && ctx->failure.status == HAL_OK; ++i) {
    const char *text = argv[i];
    size_t bad;
    if (!jh_cli_require(ctx, jb_utf8_validate(text, strlen(text), &bad), 2,
                        "argument is not valid UTF-8"))
      break;
    if (!positional && strcmp(text, "--") == 0) {
      positional = true;
      continue;
    }
    if (positional || text[0] != '-') {
      (void)jh_cli_ok(ctx, jh_list_push(&parsed.command, jh_span(text)));
      continue;
    }
    const char *expanded = strcmp(text, "-h") == 0   ? "--help"
                           : strcmp(text, "-V") == 0 ? "--version"
                                                     : text;
    const char *equal = strchr(expanded, '=');
    const jb_json_span_t name = {expanded, equal == NULL
                                               ? strlen(expanded)
                                               : (size_t)(equal - expanded)};
    size_t id = 0;
    while (id < opt_count) {
      const size_t length = strlen(options[id].name);
      if (name.length == length + 2 && expanded[0] == '-' &&
          expanded[1] == '-' &&
          memcmp(expanded + 2, options[id].name, length) == 0)
        break;
      ++id;
    }
    if (id == opt_count) {
      (void)jh_cli_error(ctx, HAL_EINVAL, 2,
                         JH_PARTS(jh_span("unknown option: "), jh_span(text)));
      break;
    }
    const bool repeatable = id == opt_define || id == opt_request ||
                            id == opt_source || id == opt_required_feature ||
                            id == opt_input_root || id == opt_esp_sdkconfig ||
                            id == opt_feature_option;
    if (parsed.present[id] && !repeatable) {
      (void)jh_cli_error(
          ctx, HAL_EINVAL, 2,
          JH_PARTS(jh_span("option specified more than once: "), name));
      break;
    }
    parsed.present[id] = true;
    if (options[id].value != NULL) {
      if (equal != NULL)
        parsed.values[id] = equal + 1;
      else {
        if (i + 1 == argc || strncmp(argv[i + 1], "--", 2) == 0) {
          (void)jh_cli_error(
              ctx, HAL_EINVAL, 2,
              JH_PARTS(jh_span("option requires a value: "), name));
          break;
        }
        parsed.values[id] = argv[++i];
        if (!jh_cli_require(ctx,
                            jb_utf8_validate(parsed.values[id],
                                             strlen(parsed.values[id]), &bad),
                            2, "argument is not valid UTF-8"))
          break;
      }
      if (repeatable)
        (void)jh_cli_ok(
            ctx, jh_list_push(&parsed.lists[id], jh_span(parsed.values[id])));
    } else if (equal != NULL) {
      (void)jh_cli_error(
          ctx, HAL_EINVAL, 2,
          JH_PARTS(jh_span("flag does not take a value: "), name));
    }
  }
  if (ctx->failure.status == HAL_OK) {
    arguments_destroy(&ctx->args);
    ctx->args = parsed;
  } else
    arguments_destroy(&parsed);
}

static void select_command(jh_cli_t *ctx) {
  const size_t count = jb_vector_size(&ctx->args.command);
  if (count == 0 || ctx->failure.status != HAL_OK)
    return;
  if (count == 2) {
    const char *first = jb_string_data(jh_list_at(&ctx->args.command, 0));
    const char *second = jb_string_data(jh_list_at(&ctx->args.command, 1));
    for (size_t i = 0; i < COUNTOF(commands); ++i)
      if (strcmp(first, commands[i].first) == 0 &&
          strcmp(second, commands[i].second) == 0) {
        ctx->command = &commands[i];
        return;
      }
  }
  (void)jh_cli_require(ctx, HAL_EINVAL, 2, "unknown command; use jh --help");
}

static void reject_foreign_options(jh_cli_t *ctx) {
  for (size_t id = 0; id < opt_machine && ctx->failure.status == HAL_OK; ++id) {
    if (!ctx->args.present[id])
      continue;
    const jh_option_id_t *accepted = ctx->command->accepted;
    bool found = false;
    for (size_t i = 0; accepted[i] != opt_count; ++i)
      found = found || (size_t)accepted[i] == id;
    if (!found)
      (void)jh_cli_error(ctx, HAL_EINVAL, 2,
                         JH_PARTS(jh_span("--"), jh_span(options[id].name),
                                  jh_span(" does not apply to "),
                                  jh_span(ctx->command->first), jh_span(" "),
                                  jh_span(ctx->command->second)));
  }
}

static void show_help(jh_cli_t *ctx) {
  jb_string_t text = {0};
  hal_status_t status = jb_string_init(&text, jb_heap_allocator());
  jh_put(&text, &status, usage);
  for (size_t i = 0; i < COUNTOF(options) && status == HAL_OK; ++i) {
    jh_put(&text, &status, "  --");
    jh_put(&text, &status, options[i].name);
    if (options[i].value != NULL) {
      jh_put(&text, &status, " ");
      jh_put(&text, &status, options[i].value);
    }
    jh_put(&text, &status, "\n      ");
    jh_put(&text, &status, options[i].description);
    jh_put(&text, &status, "\n");
  }
  (void)jh_cli_ok(ctx, status);
  if (ctx->json) {
    jh_result_begin(ctx, jb_string_size(&text), 32);
    jh_result_text(ctx, "command", "help");
    jh_result_text(ctx, "status", "HAL_OK");
    jh_result_string(ctx, "help", jh_string_span(&text));
    (void)jh_result_finish(ctx);
  } else
    (void)jh_cli_stdout(ctx, jh_string_span(&text));
  jb_string_destroy(&text);
}

static int dispatch(jh_cli_t *ctx, size_t argc, const char *const *argv) {
  parse_arguments(ctx, argc, argv);
  select_command(ctx);
  if (ctx->failure.status != HAL_OK)
    return 0;
  if (ctx->args.present[opt_json_version]) {
    if (!jh_cli_require(ctx, ctx->json ? HAL_OK : HAL_EINVAL, 2,
                        "--json-version requires --json"))
      return 0;
    const size_t requested =
        jh_cli_count(ctx, opt_json_version, 1, UINT32_MAX,
                     "--json-version requires an unsigned major version");
    if (!jh_cli_require(ctx, requested == 1 ? HAL_OK : HAL_EUNSUPPORTED, 2,
                        "unsupported JSON result version; supported: 1"))
      return 0;
  }
  if (ctx->args.present[opt_help] || argc == 1) {
    show_help(ctx);
    return 0;
  }
  if (ctx->args.present[opt_version]) {
    for (size_t id = 0; id < opt_machine; ++id)
      if (ctx->args.present[id]) {
        (void)jh_cli_require(ctx, HAL_EINVAL, 2,
                             "--version does not take command options");
        return 0;
      }
    if (!jh_cli_require(
            ctx, jb_vector_size(&ctx->args.command) == 0 ? HAL_OK : HAL_EINVAL,
            2, "--version is a root option"))
      return 0;
    if (ctx->json) {
      jh_result_begin(ctx, 128, 32);
      jh_result_text(ctx, "command", "version");
      jh_result_text(ctx, "status", "HAL_OK");
      jh_result_text(ctx, "version", JH_TOOL_VERSION);
      (void)jh_result_finish(ctx);
    } else
      (void)jh_cli_stdout(ctx, jh_span("jh " JH_TOOL_VERSION "\n"));
    return 0;
  }
  if (!jh_cli_require(ctx, ctx->command != NULL ? HAL_OK : HAL_EINVAL, 2,
                      "a command is required"))
    return 0;
  reject_foreign_options(ctx);
  jb_string_t base = {0};
  (void)jh_cli_ok(ctx, jb_string_init(&base, jb_heap_allocator()));
  if (ctx->failure.status == HAL_OK)
    (void)jh_cli_ok(ctx, jh_working_directory(&base, &ctx->failure));
  ctx->base = jh_cli_text(ctx, JH_PARTS(jh_string_span(&base)));
  jb_string_destroy(&base);
  if (ctx->failure.status != HAL_OK)
    return 0;
  switch ((jh_command_id_t)(ctx->command - commands)) {
  case cmd_inspect:
    return jh_cli_inspect(ctx);
  case cmd_validate:
    return jh_cli_validate(ctx);
  case cmd_defaults:
    return jh_cli_defaults(ctx);
  case cmd_features:
    return jh_cli_features(ctx);
  case cmd_repository:
    return jh_cli_repository(ctx);
  case cmd_config:
    return jh_cli_config(ctx);
  case cmd_directives:
    return jh_cli_directives(ctx);
  case cmd_feature_list:
    return jh_cli_feature_list(ctx);
  case cmd_feature_resolve:
    return jh_cli_feature_resolve(ctx);
  case cmd_feature_lint:
    return jh_cli_feature_lint(ctx);
  case cmd_manifest:
    return jh_cli_manifest(ctx);
  case cmd_board_list:
    return jh_cli_board_list(ctx);
  case cmd_board_validate:
    return jh_cli_board_validate(ctx);
  case cmd_board_default:
    return jh_cli_board_default(ctx);
  case cmd_clock_check:
    return jh_cli_clock_check(ctx);
  case cmd_clock_list:
    return jh_cli_clock_list(ctx);
  case cmd_boards:
    return jh_cli_boards(ctx);
  case cmd_board_config:
    return jh_cli_board_config(ctx);
  case cmd_project_build:
    return jh_cli_project_build(ctx, false);
  case cmd_project_cmake:
    return jh_cli_project_build(ctx, true);
  }
  (void)jh_cli_require(ctx, HAL_EINTERNAL, 3, "unexpected failure");
  return 0;
}

static void fallback_json(bool requested, hal_status_t status) {
  if (requested) {
    (void)fprintf(stdout,
                  "{\"formatVersion\":1,\"command\":\"jh\",\"status\":\"%s\","
                  "\"message\":\"operation failed\"}\n",
                  hal_status_to_string(status));
    (void)fflush(stdout);
  }
}

static const char *failure_message(const jh_failure_t *failure) {
  if (jb_string_size(&failure->message) != 0)
    return jb_string_data(&failure->message);
  return failure->status == HAL_ENOMEM || failure->status == HAL_EOVERFLOW
             ? "allocation failed"
             : "operation failed";
}

static int report_failure(jh_cli_t *ctx) {
  /* A fresh record permits rendering the original error; keep its owned
   * strings alive until the result has been published. */
  jh_failure_t error = ctx->failure;
  memset(&ctx->failure, 0, sizeof ctx->failure);
  jh_failure_init(&ctx->failure);
  const bool no_message = jb_string_size(&error.message) == 0;
  const char *message = failure_message(&error);
  int exit_code = error.status == HAL_ENOMEM ? 4 : error.exit_code;
  if (ctx->location.located && !no_message)
    (void)fprintf(stderr, "%s:%zu:%zu: %s (%s)\n",
                  jb_string_data(&ctx->location.source), ctx->location.line,
                  ctx->location.column, message,
                  hal_status_to_string(error.status));
  else
    (void)fprintf(stderr, "jh: %s (%s)\n", message,
                  hal_status_to_string(error.status));
  if (no_message)
    fallback_json(ctx->json, error.status);
  else if (ctx->json) {
    jh_result_begin(ctx,
                    jb_string_size(&ctx->location.source) +
                        jb_string_size(&error.message) + 1024,
                    32);
    jh_result_text(ctx, "command",
                   ctx->command == NULL ? "jh" : ctx->command->name);
    jh_result_text(ctx, "status", hal_status_to_string(error.status));
    jh_result_string(ctx, "message", jh_string_span(&error.message));
    if (ctx->command == &commands[cmd_project_build] ||
        ctx->command == &commands[cmd_project_cmake])
      jh_result_boolean(ctx, "configurationRejected",
                        ctx->configuration_rejected);
    if (ctx->process_attempted)
      jh_result_process(ctx);
    if (jb_string_size(&error.code) != 0) {
      jh_result_string(ctx, "code", jh_string_span(&error.code));
      jh_result_string(ctx, "source", jh_string_span(&error.source));
      jh_result_string(ctx, "pointer", jh_string_span(&error.pointer));
    } else {
      if (jb_string_size(&ctx->location.source) != 0)
        jh_result_string(ctx, "source", jh_string_span(&ctx->location.source));
      if (ctx->args.present[opt_pointer])
        jh_result_text(ctx, "pointer", ctx->args.values[opt_pointer]);
    }
    if (ctx->location.located) {
      jh_result_number(ctx, "jsonError", ctx->location.error.code);
      jh_result_number(ctx, "offset", ctx->location.error.offset);
      jh_result_number(ctx, "line", ctx->location.line);
      jh_result_number(ctx, "column", ctx->location.column);
    }
    if (!jh_result_finish(ctx)) {
      fallback_json(true, ctx->failure.status);
      (void)fprintf(stderr, "jh: %s (%s)\n", failure_message(&ctx->failure),
                    hal_status_to_string(ctx->failure.status));
      exit_code = ctx->failure.exit_code;
    }
  }
  jh_failure_destroy(&error);
  return exit_code;
}

static int run(size_t argc, const char *const *argv, bool requested_json) {
  jh_cli_t ctx = {0};
  jh_failure_init(&ctx.failure);
  jh_json_location_init(&ctx.location);
  arguments_init(&ctx.args);
  ctx.json = requested_json;
  (void)jh_cli_ok(&ctx, jh_list_init(&ctx.strings));
  (void)jh_cli_ok(&ctx, jb_vector_init(&ctx.workspace, 1, jb_heap_allocator()));
  (void)jh_cli_ok(&ctx, jb_string_init(&ctx.result, jb_heap_allocator()));
  int result = ctx.failure.status == HAL_OK ? dispatch(&ctx, argc, argv) : 0;
  if (ctx.failure.status != HAL_OK)
    result = report_failure(&ctx);
  jb_string_destroy(&ctx.result);
  jb_vector_destroy(&ctx.workspace);
  jh_list_destroy(&ctx.strings);
  arguments_destroy(&ctx.args);
  jh_json_location_destroy(&ctx.location);
  jh_failure_destroy(&ctx.failure);
  return result;
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
  bool requested_json = false;
  for (int i = 1; i < argc; ++i)
    requested_json = requested_json || wcscmp(argv[i], L"--json") == 0;
  if (_setmode(_fileno(stdout), _O_BINARY) == -1)
    return 3;
  jb_vector_t texts = {0}, pointers = {0};
  hal_status_t status = jh_list_init(&texts);
  if (status == HAL_OK)
    status =
        jb_vector_init(&pointers, sizeof(const char *), jb_heap_allocator());
  const char *message = "allocation failed";
  int code = 4;
  for (int i = 0; i < argc && status == HAL_OK; ++i) {
    jb_string_t text = {0};
    status = jb_string_init(&text, jb_heap_allocator());
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                           argv[i], -1, NULL, 0, NULL, NULL);
    if (length == 0) {
      status = HAL_EPROTO;
      message = "argument is not valid UTF-16";
      code = 2;
    }
    if (status == HAL_OK)
      status = jb_string_resize(&text, (size_t)length, '\0');
    if (status == HAL_OK &&
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1,
                            jb_string_mutable_data(&text), length, NULL,
                            NULL) != length) {
      status = HAL_EPROTO;
      message = "cannot convert argument to UTF-8";
      code = 2;
    }
    if (status == HAL_OK)
      status = jb_string_resize(&text, (size_t)length - 1, '\0');
    if (status == HAL_OK)
      status = jb_vector_append(&texts, &text, 1);
    if (status != HAL_OK)
      jb_string_destroy(&text);
  }
  for (size_t i = 0; i < jb_vector_size(&texts) && status == HAL_OK; ++i) {
    const char *text = jb_string_data(jh_list_at(&texts, i));
    status = jb_vector_append(&pointers, &text, 1);
  }
  if (status == HAL_OK)
    code = run((size_t)argc, jb_vector_const_data(&pointers), requested_json);
  else {
    fallback_json(requested_json, status);
    (void)fprintf(stderr, "jh: %s (%s)\n", message,
                  hal_status_to_string(status));
  }
  jb_vector_destroy(&pointers);
  jh_list_destroy(&texts);
  return code;
}
#else
int main(int argc, char **argv) {
  bool requested_json = false;
  for (int i = 1; i < argc; ++i)
    requested_json = requested_json || strcmp(argv[i], "--json") == 0;
  (void)signal(SIGPIPE, SIG_IGN);
  return run((size_t)argc, (const char *const *)argv, requested_json);
}
#endif
