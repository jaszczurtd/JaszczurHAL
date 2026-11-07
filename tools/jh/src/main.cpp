#include "../config/jh_board_outputs.h"
#include "../config/jh_feature_lint.h"
#include "../config/jh_project_config.h"
#include "../hardware/jh_device_defaults.h"
#include "../hardware/jh_type_catalogue.h"
#include "jh_adapters.h"
#include "jh_stl_bridge.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <jb/array.h>
#include <jb/json_pointer.h>
#include <jb/json_writer.h>
#include <jb/process.h>
#include <jb/utf8.h>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <cerrno>
#include <signal.h>
#endif

namespace {
using jh::append_text;
using jh::failure;
using jh::require;
using jh::span;

enum option_id {
  file,
  schema,
  pointer,
  output,
  root,
  bindings,
  output_root,
  config_root,
  write_mode,
  check,
  max_bytes,
  git,
  timeout_ms,
  header,
  define,
  preinclude,
  context_label,
  target,
  request,
  source,
  required_feature,
  input_root,
  report_only,
  include_hardware,
  label,
  boards_root,
  clocks_dir,
  esp_sdkconfig,
  board_option,
  feature_option,
  output_dir,
  machine,
  json_version,
  help,
  version,
  option_count
};
struct option_spec {
  const char *name, *value, *description;
};
constexpr option_spec options[] = {
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
    {"write", nullptr, "Refresh the generated artifact"},
    {"check", nullptr,
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
    {"report-only", nullptr, "Report lint findings as warnings, exit 0"},
    {"include-hardware-fixtures", nullptr,
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
    {"json", nullptr, "Write a versioned JSON result on stdout"},
    {"json-version", "<major>",
     "Require a supported JSON result major version (1)"},
    {"help", nullptr, "Show help"},
    {"version", nullptr, "Show the tool version"},
};
static_assert(COUNTOF(options) == option_count);

enum command_id {
  inspect_command,
  validate_command,
  defaults_command,
  features_command,
  repository_command,
  config_command,
  directives_command,
  feature_list_command,
  feature_resolve_command,
  feature_lint_command,
  manifest_command,
  board_list_command,
  board_validate_command,
  board_default_command,
  clock_check_command,
  clock_list_command,
  boards_command,
  board_config_command
};
struct command_spec {
  const char *first, *second, *name;
  std::vector<option_id> accepted;
};
const command_spec commands[] = {
    {"json",
     "inspect",
     "json.inspect",
     {file, pointer, output, check, max_bytes}},
    {"json", "validate", "json.validate", {file, schema, max_bytes}},
    {"generate",
     "device-defaults",
     "generate.device-defaults",
     {root, bindings, output_root, write_mode, check}},
    {"generate",
     "features",
     "generate.features",
     {root, config_root, output_root, write_mode, check}},
    {"repository",
     "files",
     "repository.files",
     {root, git, timeout_ms, max_bytes}},
    {"config", "inspect", "config.inspect", {header, define, preinclude}},
    {"config", "directives", "config.directives", {file}},
    {"feature", "list", "feature.list", {root, config_root}},
    {"feature",
     "resolve",
     "feature.resolve",
     {root, config_root, context_label, target, request, source,
      required_feature, file}},
    {"feature",
     "lint",
     "feature.lint",
     {root, config_root, input_root, report_only, include_hardware}},
    {"config", "manifest", "config.manifest", {file, label}},
    {"board", "list", "board.list", {root, boards_root}},
    {"board", "validate", "board.validate", {root, boards_root}},
    {"board", "default", "board.default", {root, boards_root, target}},
    {"clock",
     "check",
     "clock.check",
     {root, boards_root, clocks_dir, esp_sdkconfig}},
    {"clock", "list", "clock.list", {root, boards_root, clocks_dir}},
    {"generate",
     "boards",
     "generate.boards",
     {root, boards_root, output_root, write_mode, check}},
    {"board",
     "config",
     "board.config",
     {root, boards_root, target, board_option, define, feature_option,
      output_dir, output_root, file}},
};

#ifdef _WIN32
volatile LONG interrupted = 0;
BOOL WINAPI cancel_console(DWORD event) {
  if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT)
    return FALSE;
  InterlockedExchange(&interrupted, 1);
  return TRUE;
}
bool canceled(void *) {
  return InterlockedCompareExchange(&interrupted, 0, 0) != 0;
}
#else
volatile std::sig_atomic_t interrupted = 0;
void cancel_signal(int) { interrupted = 1; }
bool canceled(void *) { return interrupted != 0; }
#endif

void install_cancellation() {
#ifdef _WIN32
  require(SetConsoleCtrlHandler(cancel_console, TRUE) != FALSE ? HAL_OK
                                                               : HAL_EIO,
          3, "cannot install console cancellation");
#else
  struct sigaction action {};
  action.sa_handler = cancel_signal;
  ::sigemptyset(&action.sa_mask);
  if (::sigaction(SIGINT, &action, nullptr) != 0)
    throw failure{HAL_EIO, 3, "cannot install SIGINT cancellation", {}, {}, {}};
#endif
}

struct arguments {
  std::array<bool, option_count> present{};
  std::array<std::string, option_count> values;
  /** Every value of a repeatable option, in order. */
  std::array<std::vector<std::string>, option_count> lists;
  std::vector<std::string> command;
};
struct context : jh::parse_location {
  arguments args;
  bool json = false;
  const command_spec *command = nullptr;
  bool process_attempted = false;
  jb_process_result_t process{};
};

arguments parse_arguments(const std::vector<std::string> &argv) {
  arguments result;
  bool positional = false;
  for (size_t i = 1; i < argv.size(); ++i) {
    const std::string &text = argv[i];
    size_t bad;
    require(jb_utf8_validate(text.data(), text.size(), &bad), 2,
            "argument is not valid UTF-8");
    if (!positional && text == "--") {
      positional = true;
      continue;
    }
    if (positional || text.empty() || text[0] != '-') {
      result.command.push_back(text);
      continue;
    }
    const std::string expanded = text == "-h"   ? "--help"
                                 : text == "-V" ? "--version"
                                                : text;
    const size_t equal = expanded.find('=');
    const std::string name = expanded.substr(0, equal);
    size_t id = 0;
    while (id < option_count && name != std::string("--") + options[id].name)
      ++id;
    if (id == option_count)
      throw failure{HAL_EINVAL, 2, "unknown option: " + text, {}, {}, {}};
    const bool repeatable = id == define || id == request || id == source ||
                            id == required_feature || id == input_root ||
                            id == esp_sdkconfig || id == feature_option;
    if (result.present[id] && !repeatable)
      throw failure{HAL_EINVAL, 2,  "option specified more than once: " + name,
                    {},         {}, {}};
    result.present[id] = true;
    if (options[id].value != nullptr) {
      if (equal != std::string::npos) {
        result.values[id] = expanded.substr(equal + 1);
      } else {
        if (i + 1 == argv.size() || argv[i + 1].rfind("--", 0) == 0)
          throw failure{HAL_EINVAL, 2,  "option requires a value: " + name,
                        {},         {}, {}};
        result.values[id] = argv[++i];
        require(jb_utf8_validate(result.values[id].data(),
                                 result.values[id].size(), &bad),
                2, "argument is not valid UTF-8");
      }
      if (repeatable)
        result.lists[id].push_back(result.values[id]);
    } else if (equal != std::string::npos) {
      throw failure{HAL_EINVAL, 2,  "flag does not take a value: " + name,
                    {},         {}, {}};
    }
  }
  return result;
}

std::string help_text() {
  std::string text =
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
  for (const auto &option : options) {
    text += "  --";
    text += option.name;
    if (option.value != nullptr) {
      text += ' ';
      text += option.value;
    }
    text += "\n      ";
    text += option.description;
    text += '\n';
  }
  return text;
}

class result_writer {
  jb_json_writer_t writer{};
  jh::owned_array<unsigned char> scratch;
  // append_text appends to an owned_string.
  jh::owned_string text;

public:
  explicit result_writer(size_t input_bytes, size_t keys = 32) {
    // The envelope adds one container around a document of depth at most 256.
    const jb_json_writer_limits_t limits{input_bytes * 6 + 1024 * 1024, 257,
                                         keys + 32, input_bytes + 65536,
                                         input_bytes * 32 + 1024 * 1024};
    size_t needed;
    require(jb_json_writer_workspace_size(&limits, &needed), 4,
            "result workspace overflow");
    scratch.resize(needed);
    require(jb_json_writer_init(&writer, &limits,
                                {scratch.data(), scratch.size()}, append_text,
                                &text),
            4, "cannot initialize result writer");
    require(jb_json_writer_object_begin(&writer), 4, "cannot start result");
    number("formatVersion", 1);
  }
  void key(const char *name) {
    require(jb_json_writer_key(&writer, span(name)), 4,
            "cannot write result key");
  }
  void string(const char *name, std::string_view value) {
    key(name);
    require(jb_json_writer_string(&writer, span(value)), 4,
            "cannot write result string");
  }
  void number(const char *name, size_t value) {
    key(name);
    require(jb_json_writer_u64(&writer, value), 4,
            "cannot write result number");
  }
  void boolean(const char *name, bool value) {
    key(name);
    require(jb_json_writer_bool(&writer, value), 4, "cannot write result flag");
  }
  void signed_number(const char *name, int64_t value) {
    key(name);
    require(jb_json_writer_i64(&writer, value), 4,
            "cannot write result number");
  }
  void null(const char *name) {
    key(name);
    require(jb_json_writer_null(&writer), 4, "cannot write result null");
  }
  /** Open an array under name, or as an array item when name is null. */
  void array_begin(const char *name) {
    if (name != nullptr)
      key(name);
    require(jb_json_writer_array_begin(&writer), 4,
            "cannot start result array");
  }
  void array_end() {
    require(jb_json_writer_array_end(&writer), 4, "cannot close result array");
  }
  /** Open an object under name, or as an array item when name is null. */
  void object_begin(const char *name) {
    if (name != nullptr)
      key(name);
    require(jb_json_writer_object_begin(&writer), 4,
            "cannot start result object");
  }
  void object_end() {
    require(jb_json_writer_object_end(&writer), 4,
            "cannot close result object");
  }
  /** One string in an open array. */
  void string_item(std::string_view value) {
    require(jb_json_writer_string(&writer, span(value)), 4,
            "cannot write result string");
  }
  /** An array of the jb_string_t elements of list. */
  void string_list(const char *name, const jb_vector_t &list) {
    array_begin(name);
    for (size_t i = 0; i < jb_vector_size(&list); ++i)
      require(jb_json_writer_string(
                  &writer, span(jh::view(*static_cast<const jb_string_t *>(
                               jb_vector_const_at(&list, i))))),
              4, "cannot write result string");
    array_end();
  }
  template <class Strings>
  void strings(const char *name, const Strings &values) {
    array_begin(name);
    for (const std::string &value : values)
      require(jb_json_writer_string(&writer, span(value)), 4,
              "cannot write result string");
    array_end();
  }
  /** A tree value under name, or as an array item when name is null. */
  void tree(const char *name, jb_json_tree_view_t value) {
    if (name != nullptr)
      key(name);
    require(jh_json_write_tree(&writer, value), 4, "cannot write result tree");
  }
  /** The fixed tables of the board registry as members of the result. */
  void board_tables() {
    require(jh_board_write_tables(&writer), 4,
            "cannot write the board registry tables");
  }
  void node(jb_json_node_t value) {
    key("value");
    require(jb_json_writer_node(&writer, value), 4,
            "cannot write selected JSON");
  }
  void schema_errors(const jh::schema_files &files) {
    key("errors");
    require(jb_json_writer_array_begin(&writer), 4,
            "cannot start schema errors");
    for (size_t index = 0; index < files.report_count(); ++index) {
      const jh_schema_report_t &error = files.report(index);
      require(jb_json_writer_object_begin(&writer), 4,
              "cannot start schema error");
      string("instancePointer", jh::view(error.pointer));
      string("schemaUri", jh::view(error.uri));
      string("schemaPointer", jh::view(error.rule));
      string("reason", jh::view(error.reason));
      string("message", jh::view(error.message));
      require(jb_json_writer_object_end(&writer), 4,
              "cannot close schema error");
    }
    require(jb_json_writer_array_end(&writer), 4, "cannot close schema errors");
  }
  void paths(std::string_view bytes) {
    key("paths");
    require(jb_json_writer_array_begin(&writer), 4, "cannot start paths");
    size_t begin = 0;
    while (begin < bytes.size()) {
      const size_t nul = bytes.find('\0', begin);
      const size_t end = nul == std::string_view::npos ? bytes.size() : nul;
      if (end != begin) {
        size_t bad = 0;
        const jb_json_span_t path{bytes.data() + begin, end - begin};
        require(jb_utf8_validate(path.data, path.length, &bad), 2,
                "repository path is not UTF-8; use raw output");
        require(jb_json_writer_string(&writer, path), 4, "cannot write path");
      }
      if (nul == std::string_view::npos)
        break;
      begin = end + 1;
    }
    require(jb_json_writer_array_end(&writer), 4, "cannot close paths");
  }
  void process(const jb_process_result_t &value) {
    boolean("started", value.started);
    boolean("exited", value.exited);
    number("exitCode", value.exit_code);
    number("termSignal", static_cast<size_t>(value.term_signal));
    number("nativeError", value.native_error);
  }
  jh::owned_string finish() {
    require(jb_json_writer_object_end(&writer), 4, "cannot close result");
    size_t count;
    require(jb_json_writer_finish(&writer, &count), 4, "cannot finish result");
    text += '\n';
    return std::move(text);
  }
};

void stdout_bytes(std::string_view text) {
  if (std::fwrite(text.data(), 1, text.size(), stdout) != text.size() ||
      std::fflush(stdout) != 0)
    throw failure{HAL_EIO, 3, "cannot write stdout", {}, {}, {}};
}

size_t count_option(const arguments &args, option_id id, size_t default_value,
                    size_t maximum, const char *message) {
  size_t limit = default_value;
  if (args.present[id]) {
    const std::string &value = args.values[id];
    const auto converted =
        std::from_chars(value.data(), value.data() + value.size(), limit);
    if (converted.ec != std::errc{} ||
        converted.ptr != value.data() + value.size() || limit > maximum)
      throw failure{HAL_EINVAL, 2, message, {}, {}, {}};
  }
  return limit;
}

size_t input_limit(const arguments &args) {
  return count_option(args, max_bytes, 16 * 1024 * 1024, 64 * 1024 * 1024,
                      "--max-bytes requires an integer between 0 and 67108864");
}

void require_value(const arguments &args, option_id id, const char *message) {
  if (!args.present[id] || args.values[id].empty())
    throw failure{HAL_EINVAL, 2, message, {}, {}, {}};
}

int inspect(context &ctx, const std::string &base) {
  const arguments &args = ctx.args;
  require_value(args, file, "json inspect requires --file");
  if (args.present[check] && !args.present[output])
    throw failure{HAL_EINVAL, 2, "--check requires --output", {}, {}, {}};
  auto owned = jh::read_document(
      ctx, jh::stl::absolute_path(args.values[file], base).c_str(),
      input_limit(args));
  const auto &document = owned->doc;
  const auto bytes = document.input;
  jb_json_node_t selected;
  require(jb_json_pointer_get(&document, span(args.values[pointer]), &selected),
          2, "cannot resolve JSON Pointer");
  jb_json_span_t raw;
  require(jb_json_raw(selected, &raw), 2, "cannot read selected value");
  bool changed = false;
  std::string destination;
  if (args.present[output]) {
    destination = jh::stl::absolute_path(args.values[output], base);
    std::string content(raw.data, raw.length);
    content += '\n';
    require(jb_file_write_atomic(destination.c_str(), content.data(),
                                 content.size(), args.present[check], &changed),
            3, "cannot publish output file");
  }
  const bool stale = args.present[check] && changed;
  if (ctx.json) {
    const size_t keys = static_cast<size_t>(
        std::count_if(document.tokens, document.tokens + document.token_count,
                      [](const auto &token) { return token.has_key; }));
    result_writer result(bytes.length, keys);
    result.string("command", "json.inspect");
    result.string("status", stale ? "HAL_ESTATE" : "HAL_OK");
    result.string("source", jh::view(ctx.source));
    result.string("pointer", args.values[pointer]);
    result.node(selected);
    if (args.present[output]) {
      result.string("output", destination);
      result.boolean("changed", changed);
      result.boolean("checkOnly", args.present[check]);
    }
    stdout_bytes(result.finish());
  } else if (!args.present[output]) {
    stdout_bytes(std::string(raw.data, raw.length) + '\n');
  }
  if (stale)
    std::fprintf(stderr, "%s: output differs\n", destination.c_str());
  return stale ? 1 : 0;
}

int validate(context &ctx, const std::string &base) {
  const auto &args = ctx.args;
  if (!args.present[file] || args.values[file].empty() ||
      !args.present[schema] || args.values[schema].empty())
    throw failure{HAL_EINVAL, 2,  "json validate requires --file and --schema",
                  {},         {}, {}};
  const size_t max = input_limit(args);
  const auto input_path = jh::stl::absolute_path(args.values[file], base);
  auto input = jh::read_document(ctx, input_path.c_str(), max);
  jh::schema_files state(ctx, max);
  const auto &root_schema =
      state.root(jh::stl::absolute_path(args.values[schema], base).c_str());
  const auto status = jh::validate_with(state, *input);
  jh::assign(ctx.source, input_path);
  for (size_t index = 0; index < state.report_count(); ++index) {
    const auto error_pointer = jh::view(state.report(index).pointer);
    const auto error_message = jh::view(state.report(index).message);
    std::fprintf(stderr, "%s:", input_path.c_str());
    if (error_pointer.empty())
      (void)std::fputc('/', stderr);
    else
      (void)std::fwrite(error_pointer.data(), 1, error_pointer.size(), stderr);
    (void)std::fputs(": ", stderr);
    (void)std::fwrite(error_message.data(), 1, error_message.size(), stderr);
    (void)std::fputc('\n', stderr);
  }
  if (ctx.json) {
    result_writer result(1024 * 1024, state.report_count() * 8);
    result.string("command", "json.validate");
    result.string("status", hal_status_to_string(status));
    result.string("source", input_path);
    result.string("schema", jh::view(root_schema.uri));
    result.schema_errors(state);
    stdout_bytes(result.finish());
  }
  return status == HAL_OK ? 0 : 2;
}

// --root resolved against base, without the trailing separator a root given
// as "." resolves with.
std::string checkout_root(const arguments &args, const std::string &base) {
  auto hal_root = jh::stl::absolute_path(args.values[root], base);
  if (hal_root.size() > 1 &&
      (hal_root.back() == '/' || hal_root.back() == '\\') &&
      !(hal_root.size() == 3 && hal_root[1] == ':'))
    hal_root.pop_back();
  return hal_root;
}

// The HAL checkout of a generator.
std::string generator_root(const arguments &args, const std::string &base,
                           const char *command) {
  require_value(args, root,
                (std::string(command) + " requires --root").c_str());
  if (args.present[write_mode] == args.present[check])
    throw failure{HAL_EINVAL,
                  2,
                  std::string(command) +
                      " requires exactly one of --write and --check",
                  {},
                  {},
                  {}};
  return checkout_root(args, base);
}

// The relative path of generatedArtifacts.<key> in
// config/tooling/artifacts.json.
std::string artifact_path(const std::string &hal_root, const char *key) {
  jh::owned_string relative;
  jh::failure_record failed;
  failed.check(jh_generated_artifact_paths(hal_root.c_str(), &key, 1,
                                           relative.raw(), &failed.value));
  return std::string(jh::view(*relative.raw()));
}

int generate_defaults(context &ctx, const std::string &base) {
  const auto &args = ctx.args;
  constexpr size_t limit = 16 * 1024 * 1024;
  const auto hal_root = generator_root(args, base, "generate device-defaults");
  const auto bindings_dir =
      args.present[bindings]
          ? jh::stl::absolute_path(args.values[bindings], base)
          : jh::stl::absolute_path("config/hardware/bindings", hal_root);
  const auto out_root =
      args.present[output_root]
          ? jh::stl::absolute_path(args.values[output_root], base)
          : hal_root;
  const std::string header_relative =
      artifact_path(hal_root, "deviceDefaultsHeader");
  const auto output_path = jh::stl::absolute_path(header_relative, out_root);
  const auto features = jh::hardware::hal_features(ctx, hal_root, limit);
  const auto capabilities =
      jh::hardware::board_capabilities(ctx, hal_root, limit);
  const auto types = jh::hardware::load_hal_catalogue(
      ctx, jh::stl::absolute_path("config/hardware", hal_root), bindings_dir,
      limit);
  jh::hardware::check_catalogue(types, &features, &capabilities);
  const std::string content = jh::hardware::render_device_defaults(types);
  jh::assign(ctx.source, output_path);
  bool changed = false;
  bool current = true;
  if (args.present[write_mode]) {
    changed = jh::write_generated(output_path.c_str(), content);
    if (!ctx.json)
      stdout_bytes("generated 1 device defaults artifacts (" +
                   std::to_string(changed ? 1 : 0) + " changed)\n");
  } else {
    current = jh::check_generated(output_path.c_str(), content, 40,
                                  "generated:" + header_relative);
    changed = !current;
    if (current && !ctx.json)
      stdout_bytes("verified 1 generated device defaults artifacts\n");
  }
  if (ctx.json) {
    result_writer result(output_path.size() + bindings_dir.size() + 1024);
    result.string("command", "generate.device-defaults");
    result.string("status", current ? "HAL_OK" : "HAL_ESTATE");
    result.string("root", hal_root);
    result.string("bindings", bindings_dir);
    result.string("output", output_path);
    result.boolean("changed", changed);
    result.boolean("checkOnly", args.present[check]);
    stdout_bytes(result.finish());
  }
  return current ? 0 : 1;
}

int generate_features(context &ctx, const std::string &base) {
  const auto &args = ctx.args;
  constexpr size_t limit = 16 * 1024 * 1024;
  const auto hal_root = generator_root(args, base, "generate features");
  const auto config_dir =
      args.present[config_root]
          ? jh::stl::absolute_path(args.values[config_root], base)
          : jh::stl::absolute_path("config", hal_root);
  const auto out_root =
      args.present[output_root]
          ? jh::stl::absolute_path(args.values[output_root], base)
          : hal_root;
  jh::feature_registry registry;
  jh::feature_outputs outputs;
  jh::failure_record failed;
  failed.check(jh_feature_registry_load(&registry, config_dir.c_str(), limit,
                                        &failed.value));
  failed.check(
      jh_feature_outputs_read(hal_root.c_str(), &outputs, &failed.value));
  size_t changed = 0;
  bool current = true;
  failed.check(jh_feature_registry_publish(
      &registry, &outputs, out_root.c_str(), args.present[check], &changed,
      &current, &failed.value));
  if (!ctx.json) {
    if (args.present[write_mode])
      stdout_bytes("generated 2 feature artifacts (" + std::to_string(changed) +
                   " changed)\n");
    else if (current)
      stdout_bytes("verified 2 generated feature artifacts\n");
  } else {
    result_writer result(hal_root.size() + config_dir.size() + out_root.size() +
                         1024);
    result.string("command", "generate.features");
    result.string("status", current ? "HAL_OK" : "HAL_ESTATE");
    result.string("root", hal_root);
    result.string("config", config_dir);
    result.string("output", out_root);
    result.number("changed", changed);
    result.boolean("checkOnly", args.present[check]);
    stdout_bytes(result.finish());
  }
  return current ? 0 : 1;
}

// The configuration a feature command reads: --config-root, else
// <root>/config.
std::string feature_config(const arguments &args, const std::string &base,
                           const char *command) {
  if (args.present[config_root]) {
    require_value(args, config_root, "--config-root needs a path");
    return jh::stl::absolute_path(args.values[config_root], base);
  }
  require_value(
      args, root,
      (std::string(command) + " requires --root or --config-root").c_str());
  return jh::stl::absolute_path("config", checkout_root(args, base));
}

size_t list_bytes(const jb_vector_t &list) {
  size_t bytes = 0;
  for (size_t i = 0; i < jb_vector_size(&list); ++i)
    bytes += jb_string_size(static_cast<const jb_string_t *>(
                 jb_vector_const_at(&list, i))) +
             8;
  return bytes;
}

/* Every registered symbol with its relations, build inputs and closure. */
int feature_list(context &ctx, const std::string &base) {
  constexpr size_t limit = 16 * 1024 * 1024;
  const auto config_dir = feature_config(ctx.args, base, "feature list");
  jh::feature_registry registry;
  jh::failure_record failed;
  failed.check(jh_feature_registry_load(&registry, config_dir.c_str(), limit,
                                        &failed.value));
  const auto feature = [&](size_t index) {
    return static_cast<const jh_feature_t *>(
        jb_vector_const_at(&registry.features, index));
  };
  const size_t count = jb_vector_size(&registry.features);
  if (!ctx.json) {
    std::string text;
    for (size_t i = 0; i < count; ++i)
      text += std::string(jh::view(feature(i)->name)) +
              (feature(i)->derived ? " derived " : " feature ") +
              std::string(jh::view(feature(i)->domain)) + '\n';
    stdout_bytes(text);
    return 0;
  }
  struct indices {
    jb_vector_t value{};
    indices() {
      (void)jb_vector_init(&value, sizeof(size_t), jb_heap_allocator());
    }
    indices(const indices &) = delete;
    indices &operator=(const indices &) = delete;
    ~indices() { jb_vector_destroy(&value); }
  } implied;
  size_t bytes = config_dir.size() + 1024;
  for (size_t i = 0; i < count; ++i) {
    const jh_feature_t &item = *feature(i);
    bytes += jb_string_size(&item.name) * (count + 1) +
             jb_string_size(&item.domain) + jb_string_size(&item.note) +
             list_bytes(item.implies) + list_bytes(item.requirements) +
             list_bytes(item.conflicts) + list_bytes(item.feature_sources) +
             list_bytes(item.portable_sources) + list_bytes(item.dependencies) +
             256;
  }
  result_writer result(bytes);
  result.string("command", "feature.list");
  result.string("status", "HAL_OK");
  result.string("config", config_dir);
  result.string("registryDigest", registry.digest);
  result.string("registrySchemaDigest", registry.schema_digest);
  result.array_begin("features");
  for (size_t i = 0; i < count; ++i) {
    const jh_feature_t &item = *feature(i);
    result.object_begin(nullptr);
    result.string("name", jh::view(item.name));
    result.string("domain", jh::view(item.domain));
    result.string("kind", item.derived ? "derived" : "feature");
    result.string_list("implies", item.implies);
    result.string_list("requires", item.requirements);
    result.string_list("conflicts", item.conflicts);
    result.string_list("featureSources", item.feature_sources);
    result.string_list("portableSources", item.portable_sources);
    result.string_list("dependencies", item.dependencies);
    if (item.has_note)
      result.string("note", jh::view(item.note));
    else
      result.null("note");
    require(jh_feature_registry_closure(&registry, i, &implied.value), 4,
            "cannot list the implied features");
    result.array_begin("closure");
    for (size_t j = 0; j < jb_vector_size(&implied.value); ++j)
      result.string_item(
          jh::view(feature(*static_cast<const size_t *>(
                               jb_vector_const_at(&implied.value, j)))
                       ->name));
    result.array_end();
    result.object_end();
  }
  result.array_end();
  stdout_bytes(result.finish());
  return 0;
}

/* The requests of feature resolve --file, read like json.loads: an object
 * with only "requests", an array of objects with only a string "request"
 * (SYMBOL[=VALUE]) and a string "source". */
void request_file(context &ctx, const std::string &path, size_t limit,
                  std::vector<std::string> &texts,
                  std::vector<std::string> &sources) {
  jb_json_tree_t *raw = nullptr;
  jh::failure_record failed;
  failed.check(
      jh_json_read_value(&ctx, path.c_str(), limit, &raw, &failed.value));
  const jh::tree_owner tree(raw);
  // key of an object with exactly members members.
  const auto member = [](jb_json_tree_view_t object, const char *key,
                         size_t members, jb_json_tree_view_t *out) {
    size_t count = 0;
    return jh_json_member(object, key, out) &&
           jb_json_tree_count(object, &count) == HAL_OK && count == members;
  };
  const auto text = [](jb_json_tree_view_t value, std::string &out) {
    jb_json_span_t bytes;
    if (jh_json_type(value) != JB_JSON_STRING ||
        jb_json_tree_text(value, &bytes) != HAL_OK)
      return false;
    out.assign(bytes.data, bytes.length);
    return true;
  };
  jb_json_tree_view_t root_value, list;
  size_t count = 0;
  if (jb_json_tree_root(tree.get(), &root_value) != HAL_OK ||
      !member(root_value, "requests", 1, &list) ||
      jh_json_type(list) != JB_JSON_ARRAY ||
      jb_json_tree_count(list, &count) != HAL_OK)
    throw failure{HAL_ETYPE, 2,  "request file needs only a requests array",
                  {},        {}, {}};
  for (size_t i = 0; i < count; ++i) {
    jb_json_tree_view_t item, value;
    texts.emplace_back();
    sources.emplace_back();
    if (jb_json_tree_at(list, i, &item) != HAL_OK ||
        !member(item, "request", 2, &value) || !text(value, texts.back()) ||
        !member(item, "source", 2, &value) || !text(value, sources.back()))
      throw failure{HAL_ETYPE,
                    2,
                    "request file /requests/" + std::to_string(i) +
                        " needs only a string request and source",
                    {},
                    {},
                    {}};
  }
}

/* The features one build requests, resolved like the build resolves them;
 * findings are reported on stderr and give exit code 1. */
int feature_resolve(context &ctx, const std::string &base) {
  const auto &args = ctx.args;
  constexpr size_t limit = 16 * 1024 * 1024;
  const auto config_dir = feature_config(args, base, "feature resolve");
  std::vector<std::string> texts = args.lists[request];
  std::vector<std::string> sources = args.lists[source];
  if (!sources.empty() && sources.size() != texts.size())
    throw failure{
        HAL_EINVAL, 2,  "feature resolve takes one --source per --request",
        {},         {}, {}};
  if (args.present[file]) {
    require_value(args, file, "--file needs a path");
    if (!texts.empty())
      throw failure{HAL_EINVAL, 2,  "--file replaces --request and --source",
                    {},         {}, {}};
  }
  if (args.present[required_feature] && !args.present[target])
    throw failure{HAL_EINVAL, 2,  "--required-feature requires --target",
                  {},         {}, {}};
  for (size_t i = sources.size(); i < texts.size(); ++i)
    sources.push_back("command-line:--request[" + std::to_string(i) + "]");
  jh::feature_registry registry;
  jh::failure_record failed;
  failed.check(jh_feature_registry_load(&registry, config_dir.c_str(), limit,
                                        &failed.value));
  // Read after the registry, whose failures must not name this file.
  if (args.present[file])
    request_file(ctx, jh::stl::absolute_path(args.values[file], base), limit,
                 texts, sources);
  std::vector<jh_feature_request_t> requests;
  for (size_t i = 0; i < texts.size(); ++i) {
    const std::string_view text = texts[i];
    const size_t equal = text.find('=');
    jh_feature_request_t item{};
    item.symbol = span(text.substr(0, equal));
    item.has_value = equal != std::string_view::npos;
    item.value = item.has_value ? span(text.substr(equal + 1)) : span("");
    item.source = span(sources[i]);
    requests.push_back(item);
  }
  std::vector<jb_json_span_t> required;
  for (const auto &item : args.lists[required_feature])
    required.push_back(span(item));
  const jh_feature_target_t target_value{span(args.values[target]),
                                         required.data(), required.size()};
  const std::string context_text =
      args.present[context_label] ? args.values[context_label] : "command line";
  jh::feature_resolution resolution;
  failed.check(jh_feature_resolve(
      &registry, requests.data(), requests.size(), span(context_text),
      args.present[target] ? &target_value : nullptr, &resolution,
      &failed.value));
  const size_t findings = jb_vector_size(&resolution.findings);
  std::string report;
  for (size_t i = 0; i < findings; ++i)
    report += std::string(jh::view(*static_cast<const jb_string_t *>(
                  jb_vector_const_at(&resolution.findings, i)))) +
              '\n';
  (void)std::fwrite(report.data(), 1, report.size(), stderr);
  if (!ctx.json) {
    std::string text;
    for (size_t i = 0; i < jb_vector_size(&resolution.resolved); ++i)
      text += std::string(jh::view(*static_cast<const jb_string_t *>(
                  jb_vector_const_at(&resolution.resolved, i)))) +
              '\n';
    stdout_bytes(text);
    return findings == 0 ? 0 : 1;
  }
  const size_t entries = jb_vector_size(&resolution.provenance);
  size_t bytes = config_dir.size() + context_text.size() +
                 args.values[target].size() + report.size() +
                 2 * list_bytes(resolution.resolved) + 1024;
  for (size_t i = 0; i < entries; ++i) {
    const auto &entry = *static_cast<const jh_feature_provenance_t *>(
        jb_vector_const_at(&resolution.provenance, i));
    bytes += jb_string_size(&entry.symbol) + list_bytes(entry.sources) + 16;
  }
  result_writer result(bytes, entries);
  result.string("command", "feature.resolve");
  result.string("status", findings == 0 ? "HAL_OK" : "HAL_ECONFIG");
  result.string("config", config_dir);
  result.string("registryDigest", registry.digest);
  result.string("context", context_text);
  if (args.present[target])
    result.string("target", args.values[target]);
  else
    result.null("target");
  result.string_list("requestedFeatures", resolution.requested);
  result.string_list("resolvedFeatures", resolution.resolved);
  result.string("resolvedFeaturesDigest", resolution.digest);
  result.object_begin("provenance");
  for (size_t i = 0; i < entries; ++i) {
    const auto &entry = *static_cast<const jh_feature_provenance_t *>(
        jb_vector_const_at(&resolution.provenance, i));
    result.string_list(std::string(jh::view(entry.symbol)).c_str(),
                       entry.sources);
  }
  result.object_end();
  result.string_list("findings", resolution.findings);
  stdout_bytes(result.finish());
  return findings == 0 ? 0 : 1;
}

/* lint_header: what every #define of a HAL feature in the header, in any
 * branch, and every variant definition breaks; a header the reader rejects
 * is one finding. */
void lint_header(const jh_feature_registry_t &registry, const std::string &path,
                 jb_vector_t *findings) {
  jh::config::project_config config;
  try {
    config = jh::config::read_project_config(path, {}, {});
  } catch (const failure &error) {
    require(jh_list_push(findings, span(error.message)), 4,
            "cannot store a lint finding");
    return;
  }
  const std::string &header = config.files.empty() ? path : config.files[0];
  const auto check = [&](std::string_view symbol, bool has_value,
                         std::string_view value, const std::string &source) {
    jh_feature_request_t item{};
    item.symbol = span(symbol);
    item.has_value = has_value;
    item.value = span(value);
    item.source = span(source);
    jh::failure_record failed;
    failed.check(jh_feature_check_request(&registry, &item, findings, nullptr,
                                          &failed.value));
  };
  for (const auto &item : config.directives)
    if (!item.undef && item.file == header &&
        jh_feature_symbol(span(item.name), true))
      check(item.name, !item.text.empty(), item.text,
            path + ':' + std::to_string(item.line));
  for (const auto &item : config.variants)
    for (const auto &definition : item.definitions) {
      const std::string_view text = definition;
      const size_t equal = text.find('=');
      const std::string_view symbol = text.substr(0, equal);
      if (jh_feature_symbol(span(symbol), false))
        check(symbol, equal != std::string_view::npos,
              equal == std::string_view::npos ? std::string_view()
                                              : text.substr(equal + 1),
              path + ":JH_PROJECT_VARIANTS(" + item.id + ")");
    }
}

/* generate_hal_features.py --lint: the feature requests of every header and
 * manifest below the input roots; findings fail unless --report-only. */
int feature_lint(context &ctx, const std::string &base) {
  const auto &args = ctx.args;
  constexpr size_t limit = 16 * 1024 * 1024;
  const auto config_dir = feature_config(args, base, "feature lint");
  std::vector<std::string> roots;
  for (const auto &item : args.lists[input_root])
    roots.push_back(jh::stl::absolute_path(item, base));
  if (roots.empty()) {
    require_value(args, root, "feature lint requires --input-root or --root");
    roots.push_back(checkout_root(args, base));
  }
  jh::feature_registry registry;
  jh::failure_record failed;
  failed.check(jh_feature_registry_load(&registry, config_dir.c_str(), limit,
                                        &failed.value));
  jh::string_list found;
  for (const auto &item : roots)
    failed.check(jh_feature_lint_inputs(item.c_str(),
                                        args.present[include_hardware],
                                        &found.value, &failed.value));
  jh_list_sort(&found.value);
  std::vector<std::string> inputs;
  for (size_t i = 0; i < found.size(); ++i)
    if (inputs.empty() || inputs.back() != found[i])
      inputs.emplace_back(found[i]);
  jh::string_list findings;
  for (const auto &path : inputs) {
    const size_t slash = path.find_last_of("/\\");
    if (path.compare(slash + 1, std::string::npos, "hal_project_config.h") ==
        0) {
      lint_header(registry, path, &findings.value);
      continue;
    }
    jb_json_tree_t *tree = nullptr;
    jb_json_tree_view_t manifest{};
    const hal_status_t loaded = jh_check_load_object(path.c_str(), limit, &tree,
                                                     &manifest, &failed.value);
    const jh::tree_owner owner(tree);
    failed.check(loaded);
    failed.check(jh_manifest_findings(manifest, span(path), &findings.value,
                                      &failed.value));
  }
  jh_list_sort(&findings.value);
  const bool report_only_mode = args.present[report_only];
  const std::string label = report_only_mode ? "warning: " : "error: ";
  std::string report;
  for (size_t i = 0; i < findings.size(); ++i)
    report += label + std::string(findings[i]) + '\n';
  const std::string summary =
      "linted " + std::to_string(inputs.size()) + " feature inputs: " +
      (findings.size() == 0 ? std::string("no findings")
                            : std::to_string(findings.size()) + " finding(s)") +
      '\n';
  if (findings.size() != 0 && !ctx.json)
    report += summary;
  (void)std::fwrite(report.data(), 1, report.size(), stderr);
  const bool passed = report_only_mode || findings.size() == 0;
  if (!ctx.json) {
    if (findings.size() == 0)
      stdout_bytes(summary);
    return passed ? 0 : 1;
  }
  size_t bytes = config_dir.size() + report.size() + 1024;
  for (const auto &path : inputs)
    bytes += path.size() + 8;
  result_writer result(bytes);
  result.string("command", "feature.lint");
  result.string("status", passed ? "HAL_OK" : "HAL_ECONFIG");
  result.string("config", config_dir);
  result.boolean("reportOnly", report_only_mode);
  result.strings("inputs", inputs);
  result.string_list("findings", findings.value);
  stdout_bytes(result.finish());
  return passed ? 0 : 1;
}

/* The project configuration a manifest carries although it belongs in
 * hal_project_config.h, read as jh-vscode reads a manifest. */
// The board registry of --boards-root, else <root>/boards, with the
// component, feature and hardware registries of --root.
void load_boards(context &ctx, const std::string &base, const char *command,
                 jh::board_registry &registry, std::string &boards) {
  constexpr size_t limit = 16 * 1024 * 1024;
  require_value(ctx.args, root,
                (std::string(command) + " requires --root").c_str());
  const auto hal_root = checkout_root(ctx.args, base);
  if (ctx.args.present[boards_root]) {
    require_value(ctx.args, boards_root, "--boards-root needs a path");
    // Path.resolve(), as generate_board_config.py did: links before "..".
    boards = jh::stl::python_resolve(ctx.args.values[boards_root], base);
  } else {
    boards = jh::stl::absolute_path("boards", hal_root);
  }
  jh::failure_record failed;
  failed.check(jh_board_registry_load(&registry, hal_root.c_str(),
                                      boards.c_str(), limit, &failed.value));
}

size_t tree_bytes(jb_json_tree_view_t value) {
  const jb_json_tree_format_t compact{64 * 1024 * 1024, 0, false, false};
  size_t written = 0;
  require(jb_json_tree_format(
              value, &compact,
              [](void *, const char *, size_t) { return HAL_OK; }, nullptr,
              &written),
          4, "cannot measure the board registry");
  return written;
}

/* The targets and boards with their descriptors, the capabilities, the
 * component registry and the fixed tables. */
int board_list(context &ctx, const std::string &base) {
  jh::board_registry registry;
  std::string boards;
  load_boards(ctx, base, "board list", registry, boards);
  const auto descriptor = [](const jb_vector_t &list, size_t index) {
    return static_cast<const jh_board_descriptor_t *>(
        jb_vector_const_at(&list, index));
  };
  if (!ctx.json) {
    std::string text;
    for (const auto *list : {&registry.targets, &registry.boards})
      for (size_t i = 0; i < jb_vector_size(list); ++i)
        text += std::string(list == &registry.targets ? "target " : "board ") +
                std::string(jh::view(descriptor(*list, i)->id)) + '\n';
    stdout_bytes(text);
    return 0;
  }
  size_t bytes = boards.size() + tree_bytes(registry.capabilities) + 65536;
  for (const auto *list : {&registry.targets, &registry.boards})
    for (size_t i = 0; i < jb_vector_size(list); ++i)
      bytes += tree_bytes(descriptor(*list, i)->root) +
               jb_string_size(&descriptor(*list, i)->path) + 64;
  result_writer result(bytes, bytes / 4);
  result.string("command", "board.list");
  result.string("status", "HAL_OK");
  result.string("boardsRoot", jh::view(registry.boards_root));
  for (const auto *list : {&registry.targets, &registry.boards}) {
    result.array_begin(list == &registry.targets ? "targets" : "boards");
    for (size_t i = 0; i < jb_vector_size(list); ++i) {
      const jh_board_descriptor_t &item = *descriptor(*list, i);
      result.object_begin(nullptr);
      result.string("id", jh::view(item.id));
      result.string("path", jh::view(item.path));
      result.tree("descriptor", item.root);
      result.object_end();
    }
    result.array_end();
  }
  result.tree("capabilities", registry.capabilities);
  result.array_begin("components");
  for (size_t i = 0; i < jb_vector_size(&registry.components); ++i) {
    const auto &component = *static_cast<const jh_board_component_t *>(
        jb_vector_const_at(&registry.components, i));
    result.object_begin(nullptr);
    result.string("id", jh::view(component.id));
    result.string_list("providers", component.providers);
    result.string("slot", jh::view(component.slot));
    result.object_end();
  }
  result.array_end();
  result.board_tables();
  stdout_bytes(result.finish());
  return 0;
}

int board_validate(context &ctx, const std::string &base) {
  jh::board_registry registry;
  std::string boards;
  load_boards(ctx, base, "board validate", registry, boards);
  const size_t targets = jb_vector_size(&registry.targets);
  const size_t count = jb_vector_size(&registry.boards);
  if (!ctx.json) {
    stdout_bytes("validated " + std::to_string(targets) + " targets and " +
                 std::to_string(count) + " boards\n");
    return 0;
  }
  result_writer result(boards.size() + 1024);
  result.string("command", "board.validate");
  result.string("status", "HAL_OK");
  result.string("boardsRoot", jh::view(registry.boards_root));
  result.number("targets", targets);
  result.number("boards", count);
  stdout_bytes(result.finish());
  return 0;
}

/* The default board of a known target. */
int board_default(context &ctx, const std::string &base) {
  jh::board_registry registry;
  std::string boards;
  load_boards(ctx, base, "board default", registry, boards);
  const jh_board_descriptor_t *selected =
      ctx.args.present[target] && !ctx.args.values[target].empty()
          ? jh_board_registry_find(&registry.targets,
                                   span(ctx.args.values[target]))
          : nullptr;
  if (selected == nullptr)
    throw failure{HAL_ECONFIG, 2,  "board default requires a known --target",
                  {},          {}, {}};
  jb_json_tree_view_t value{};
  jb_json_span_t board{nullptr, 0};
  require(jb_json_tree_find(selected->root, span("defaultBoard"), &value) ==
                      HAL_OK &&
                  jb_json_tree_text(value, &board) == HAL_OK
              ? HAL_OK
              : HAL_EINTERNAL,
          4, "cannot read the default board");
  const std::string_view text(board.data, board.length);
  if (!ctx.json) {
    stdout_bytes(std::string(text) + '\n');
    return 0;
  }
  result_writer result(text.size() + 1024);
  result.string("command", "board.default");
  result.string("status", "HAL_OK");
  result.string("target", jh::view(selected->id));
  result.string("board", text);
  stdout_bytes(result.finish());
  return 0;
}

/* The checked-in artifacts of the board registry: write or check them. */
int generate_boards(context &ctx, const std::string &base) {
  const auto &args = ctx.args;
  const auto hal_root = generator_root(args, base, "generate boards");
  jh::board_registry registry;
  std::string boards;
  load_boards(ctx, base, "generate boards", registry, boards);
  const auto out_root =
      args.present[output_root]
          ? jh::stl::absolute_path(args.values[output_root], base)
          : hal_root;
  size_t changed = 0;
  bool current = true;
  jh::failure_record failed;
  failed.check(jh_board_static_publish(&registry, hal_root.c_str(),
                                       out_root.c_str(), args.present[check],
                                       &changed, &current, &failed.value));
  if (!ctx.json) {
    if (args.present[write_mode])
      stdout_bytes("generated 3 board artifacts (" + std::to_string(changed) +
                   " changed)\n");
    else if (current)
      stdout_bytes("verified 3 generated board artifacts\n");
  } else {
    result_writer result(hal_root.size() + boards.size() + out_root.size() +
                         1024);
    result.string("command", "generate.boards");
    result.string("status", current ? "HAL_OK" : "HAL_ESTATE");
    result.string("root", hal_root);
    result.string("boardsRoot", jh::view(registry.boards_root));
    result.string("output", out_root);
    result.number("changed", changed);
    result.boolean("checkOnly", args.present[check]);
    stdout_bytes(result.finish());
  }
  return current ? 0 : 1;
}

/* The build configuration of one target and board: write its files below a
 * managed output directory, return them as JSON, or both. */
int board_config(context &ctx, const std::string &base) {
  const auto &args = ctx.args;
  jh::board_registry registry;
  std::string boards;
  load_boards(ctx, base, "board config", registry, boards);
  const auto hal_root = checkout_root(args, base);
  jh::string_list definitions, features;
  for (const auto &item : args.lists[define])
    require(jh_list_push(&definitions.value, jh::span(item)), 4,
            "cannot store the definitions");
  for (const auto &item : args.lists[feature_option])
    require(jh_list_push(&features.value, jh::span(item)), 4,
            "cannot store the feature requests");
  // The output paths resolve as Path.resolve() did in
  // generate_board_config.py, a link before a ".." that follows it; the
  // assembly file, an input of jh alone, as every other jh input.
  const auto path = [&](option_id id, bool resolve) {
    if (!args.present[id] || args.values[id].empty())
      return std::string();
    return resolve ? jh::stl::python_resolve(args.values[id], base)
                   : jh::stl::absolute_path(args.values[id], base);
  };
  const auto directory = path(output_dir, true),
             managed = path(output_root, true), assembly = path(file, false);
  const auto optional = [](const std::string &text) {
    return text.empty() ? nullptr : text.c_str();
  };
  const jh_board_config_request_t request{&registry,
                                          hal_root.c_str(),
                                          jh::span(args.values[target]),
                                          jh::span(args.values[board_option]),
                                          &definitions.value,
                                          &features.value,
                                          optional(directory),
                                          optional(managed),
                                          optional(assembly),
                                          ctx.json,
                                          16 * 1024 * 1024};
  jh_board_config_result_t result{};
  require(jh_board_config_result_init(&result), 4,
          "cannot start the board configuration");
  struct result_owner {
    jh_board_config_result_t *value;
    ~result_owner() { jh_board_config_result_destroy(value); }
  } owner{&result};
  jh::failure_record failed;
  failed.check(jh_board_configure(&request, &result, &failed.value));
  if (!ctx.json)
    return 0;
  size_t bytes = jb_string_size(&result.output_dir) + 4096;
  for (size_t i = 0; i < JH_BOARD_OUTPUT_COUNT; ++i)
    bytes += jb_string_size(&result.outputs.content[i]) + 64;
  result_writer json(bytes);
  json.string("command", "board.config");
  json.string("status", "HAL_OK");
  json.string("target", jh::view(result.target));
  json.string("board", jh::view(result.board));
  if (jb_string_size(&result.output_dir) != 0) {
    json.string("outputDir", jh::view(result.output_dir));
    json.number("changed", result.changed);
  }
  json.object_begin("files");
  for (size_t i = 0; i < JH_BOARD_OUTPUT_COUNT; ++i)
    json.string(jh_board_output_names[i], jh::view(result.outputs.content[i]));
  json.object_end();
  stdout_bytes(json.finish());
  return 0;
}

// The clock registry of --clocks, else <root>/config/hardware/clocks, with
// the clock tree schema of --root.
void load_clocks(context &ctx, const std::string &base,
                 jh::clock_registry &registry, std::string &directory) {
  constexpr size_t limit = 16 * 1024 * 1024;
  const auto hal_root = checkout_root(ctx.args, base);
  if (ctx.args.present[clocks_dir]) {
    require_value(ctx.args, clocks_dir, "--clocks needs a path");
    directory = jh::stl::absolute_path(ctx.args.values[clocks_dir], base);
  } else {
    directory = jh::stl::absolute_path("config/hardware/clocks", hal_root);
  }
  jh::failure_record failed;
  failed.check(jh_clock_registry_load(&registry, hal_root.c_str(),
                                      directory.c_str(), limit, &failed.value));
}

/* clock_registry.py: the registry against the targets, boards, STM32G474
 * startup headers, pinned Pico SDK and HAL build files, and each
 * --esp-sdkconfig TARGET=PATH against its target. */
int clock_check(context &ctx, const std::string &base) {
  constexpr size_t limit = 16 * 1024 * 1024;
  require_value(ctx.args, root, "clock check requires --root");
  std::vector<std::pair<std::string, std::string>> sdkconfigs;
  for (const auto &item : ctx.args.lists[esp_sdkconfig]) {
    const size_t equal = item.find('=');
    if (equal == std::string::npos || equal == 0 || equal + 1 == item.size())
      throw failure{HAL_EINVAL, 2,  "--esp-sdkconfig needs TARGET=PATH",
                    {},         {}, {}};
    sdkconfigs.emplace_back(
        item.substr(0, equal),
        jh::stl::absolute_path(item.substr(equal + 1), base));
  }
  jh::clock_registry clocks;
  std::string directory;
  load_clocks(ctx, base, clocks, directory);
  jh::board_registry boards;
  std::string boards_dir;
  load_boards(ctx, base, "clock check", boards, boards_dir);
  const auto hal_root = checkout_root(ctx.args, base);
  const auto port =
      jh::stl::absolute_path("src/hal/impl/stm32g474/port", hal_root);
  const auto sdk = jh::stl::absolute_path("third_party/pico-sdk/src", hal_root);
  jh::failure_record failed;
  failed.check(jh_clock_check_registry(&clocks, &boards, &failed.value));
  failed.check(jh_clock_check_boards(&clocks, &boards, &failed.value));
  failed.check(
      jh_clock_check_stm32g474(&clocks, port.c_str(), limit, &failed.value));
  failed.check(
      jh_clock_check_pico_sdk(&clocks, sdk.c_str(), limit, &failed.value));
  failed.check(
      jh_clock_check_no_hal_override(hal_root.c_str(), limit, &failed.value));
  for (const auto &[target_id, path] : sdkconfigs)
    failed.check(jh_clock_check_esp_sdkconfig(
        &clocks, span(target_id), path.c_str(), limit, &failed.value));
  const size_t targets = jb_vector_size(&clocks.files);
  const size_t trees = jh_clock_registry_tree_count(&clocks);
  if (!ctx.json) {
    stdout_bytes("[OK] clock registry: " + std::to_string(targets) +
                 " targets, " + std::to_string(trees) + " trees\n");
    return 0;
  }
  result_writer result(directory.size() + 1024);
  result.string("command", "clock.check");
  result.string("status", "HAL_OK");
  result.string("clocks", directory);
  result.number("targets", targets);
  result.number("trees", trees);
  result.number("espSdkconfigs", sdkconfigs.size());
  stdout_bytes(result.finish());
  return 0;
}

/* The trees of each target as load_registry read them, the board component
 * behind each STM32G474 tree and, for every board, compatible target and
 * tree of that target, why the board cannot run the tree. */
int clock_list(context &ctx, const std::string &base) {
  jh::clock_registry clocks;
  std::string directory;
  require_value(ctx.args, root, "clock list requires --root");
  load_clocks(ctx, base, clocks, directory);
  const auto file = [&clocks](size_t index) {
    return static_cast<const jh_clock_file_t *>(
        jb_vector_const_at(&clocks.files, index));
  };
  if (!ctx.json) {
    std::string text;
    for (size_t i = 0; i < jb_vector_size(&clocks.files); ++i) {
      jb_json_tree_view_t trees{};
      (void)jh_json_member(file(i)->root, "trees", &trees);
      for (size_t t = 0; t < jh_json_count(trees); ++t) {
        const jb_json_span_t name = jh_json_key(jh_json_item(trees, t));
        text += std::string(jh::view(file(i)->target)) + ' ' +
                std::string(name.data, name.length) + '\n';
      }
    }
    stdout_bytes(text);
    return 0;
  }
  const auto hal_root = checkout_root(ctx.args, base);
  std::string boards_dir;
  if (ctx.args.present[boards_root]) {
    require_value(ctx.args, boards_root, "--boards-root needs a path");
    boards_dir = jh::stl::absolute_path(ctx.args.values[boards_root], base);
  } else {
    boards_dir = jh::stl::absolute_path("boards", hal_root);
  }
  size_t bytes = directory.size() + boards_dir.size() + 1024 * 1024;
  for (size_t i = 0; i < jb_vector_size(&clocks.files); ++i)
    bytes += tree_bytes(file(i)->root) + jb_string_size(&file(i)->path) + 64;
  result_writer result(bytes, bytes / 4);
  result.string("command", "clock.list");
  result.string("status", "HAL_OK");
  result.string("clocks", directory);
  result.array_begin("targets");
  for (size_t i = 0; i < jb_vector_size(&clocks.files); ++i) {
    result.object_begin(nullptr);
    result.string("id", jh::view(file(i)->target));
    result.string("path", jh::view(file(i)->path));
    result.tree("registry", file(i)->root);
    result.object_end();
  }
  result.array_end();
  result.object_begin("stm32g474TreeComponents");
  for (size_t i = 0; i < jh_clock_stm32g474_tree_count; ++i) {
    const auto &entry = jh_clock_stm32g474_trees[i];
    if (entry.component == nullptr)
      result.null(entry.tree);
    else
      result.string(entry.tree, entry.component);
  }
  result.object_end();
  result.array_begin("boardSources");
  const auto write = [](void *user, jb_json_span_t board, jb_json_span_t target,
                        jb_json_span_t tree,
                        const jb_vector_t *errors) noexcept -> hal_status_t {
    try {
      auto &out = *static_cast<result_writer *>(user);
      out.object_begin(nullptr);
      out.string("board", std::string_view(board.data, board.length));
      out.string("target", std::string_view(target.data, target.length));
      out.string("tree", std::string_view(tree.data, tree.length));
      out.array_begin("errors");
      for (size_t e = 0; e < jb_vector_size(errors); ++e) {
        const auto &error = *static_cast<const jh_clock_source_error_t *>(
            jb_vector_const_at(errors, e));
        out.object_begin(nullptr);
        out.string("pointer", jh::view(error.pointer));
        out.string("message", jh::view(error.message));
        out.object_end();
      }
      out.array_end();
      out.object_end();
      return HAL_OK;
    } catch (...) {
      return HAL_ENOMEM;
    }
  };
  jh::failure_record failed;
  failed.check(jh_clock_board_sources(&clocks, boards_dir.c_str(),
                                      16 * 1024 * 1024, write, &result,
                                      &failed.value));
  result.array_end();
  stdout_bytes(result.finish());
  return 0;
}

int config_manifest(context &ctx, const std::string &base) {
  const auto &args = ctx.args;
  require_value(args, file, "config manifest requires --file");
  const auto path = jh::stl::absolute_path(args.values[file], base);
  const std::string name = args.present[label] ? args.values[label] : path;
  const auto manifest = jh::read_settings(ctx, path.c_str(), 16 * 1024 * 1024);
  jb_json_tree_view_t document{};
  require(jb_json_tree_root(manifest.tree.get(), &document), 4,
          "cannot read the manifest");
  jh::string_list findings;
  jh::failure_record failed;
  failed.check(jh_manifest_findings(document, span(name), &findings.value,
                                    &failed.value));
  std::string report;
  for (size_t i = 0; i < findings.size(); ++i)
    report += std::string(findings[i]) + '\n';
  (void)std::fwrite(report.data(), 1, report.size(), stderr);
  if (ctx.json) {
    result_writer result(path.size() + name.size() + report.size() + 1024);
    result.string("command", "config.manifest");
    result.string("status", findings.size() == 0 ? "HAL_OK" : "HAL_ECONFIG");
    result.string("file", path);
    result.string("label", name);
    result.boolean("exists", manifest.exists);
    result.string_list("findings", findings.value);
    stdout_bytes(result.finish());
  }
  return findings.size() == 0 ? 0 : 1;
}

hal_status_t stream_bytes(FILE *stream, const char *data, size_t length) {
  while (length != 0) {
    if (canceled(nullptr))
      return HAL_ECANCELED;
#ifdef _WIN32
    const auto handle =
        reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stream)));
    const DWORD requested = static_cast<DWORD>(std::min(
        length, static_cast<size_t>(std::numeric_limits<DWORD>::max())));
    DWORD written = 0;
    if (WriteFile(handle, data, requested, &written, nullptr) == FALSE ||
        written == 0)
      return canceled(nullptr) ? HAL_ECANCELED : HAL_EIO;
    const size_t count = written;
#else
    const size_t count = std::fwrite(data, 1, length, stream);
    if (count == 0) {
      if (std::ferror(stream) != 0 && errno == EINTR) {
        std::clearerr(stream);
        continue;
      }
      return HAL_EIO;
    }
#endif
    data += count;
    length -= count;
  }
  return HAL_OK;
}

struct repository_output {
  bool capture;
  size_t limit;
  // append_text appends to an owned_string.
  jh::owned_string bytes;
};

hal_status_t repository_bytes(void *user, jb_process_stream_t stream,
                              const unsigned char *data, size_t length) {
  auto &output = *static_cast<repository_output *>(user);
  const auto *bytes = reinterpret_cast<const char *>(data);
  if (stream == JB_PROCESS_STDERR)
    return stream_bytes(stderr, bytes, length);
  if (!output.capture)
    return stream_bytes(stdout, bytes, length);
  if (length > output.limit - output.bytes.size())
    return HAL_EOVERFLOW;
  return append_text(&output.bytes, bytes, length);
}

int repository_files(context &ctx, const std::string &base) {
  const auto &args = ctx.args;
  require_value(args, root, "repository files requires --root");
  require_value(args, git, "repository files requires --git");
  if (!std::filesystem::u8path(args.values[git]).is_absolute())
    throw failure{HAL_EINVAL, 2,  "--git requires an absolute executable path",
                  {},         {}, {}};
  if (args.present[max_bytes] && !ctx.json)
    throw failure{HAL_EINVAL, 2,  "repository --max-bytes requires --json",
                  {},         {}, {}};
  const auto directory = jh::stl::absolute_path(args.values[root], base);
  const auto executable = jh::stl::absolute_path(args.values[git], base);
  const auto timeout = static_cast<uint32_t>(count_option(
      args, timeout_ms, 0, UINT32_MAX,
      "--timeout-ms requires an integer between 0 and 4294967295"));
  repository_output output{ctx.json, ctx.json ? input_limit(args) : 0, {}};
  const char *child_args[] = {
      "-C",       directory.c_str(),    "ls-files", "--cached",
      "--others", "--exclude-standard", "-z",       nullptr};
  jb_process_options_t process_options{};
  process_options.executable = executable.c_str();
  process_options.arguments = child_args;
  process_options.working_directory = base.c_str();
  process_options.timeout_ms = timeout;
  process_options.output = repository_bytes;
  process_options.canceled = canceled;
  process_options.context = &output;
  jh::assign(ctx.source, directory);
  install_cancellation();
  if (std::fflush(stdout) != 0 || std::fflush(stderr) != 0)
    throw failure{HAL_EIO, 3,  "cannot flush output before process launch",
                  {},      {}, {}};
  ctx.process_attempted = true;
  const hal_status_t status = jb_process_run(&process_options, &ctx.process);
  if (status != HAL_OK)
    throw failure{status,
                  status == HAL_ECANCELED                           ? 130
                  : status == HAL_ETIMEOUT                          ? 124
                  : status == HAL_ENOMEM || status == HAL_EOVERFLOW ? 4
                                                                    : 3,
                  "cannot enumerate repository files",
                  {},
                  {},
                  {}};
  if (ctx.process.term_signal != 0)
    throw failure{HAL_EIO,
                  128 + static_cast<int>(ctx.process.term_signal),
                  "Git terminated by a signal",
                  {},
                  {},
                  {}};
  if (ctx.process.exit_code != 0)
    throw failure{HAL_EIO,
                  static_cast<int>(ctx.process.exit_code),
                  "Git exited with code " +
                      std::to_string(ctx.process.exit_code),
                  {},
                  {},
                  {}};
  if (ctx.json) {
    result_writer result(output.bytes.size() + directory.size());
    result.string("command", "repository.files");
    result.string("status", "HAL_OK");
    result.string("root", directory);
    result.paths(output.bytes);
    result.process(ctx.process);
    stdout_bytes(result.finish());
  } else if (std::fflush(stdout) != 0 || std::fflush(stderr) != 0) {
    throw failure{HAL_EIO, 3, "cannot flush repository output", {}, {}, {}};
  }
  return 0;
}

void write_directives(result_writer &result,
                      const std::vector<jh::config::directive> &directives) {
  result.array_begin("directives");
  for (const auto &item : directives) {
    result.object_begin(nullptr);
    result.string("file", item.file);
    result.number("line", item.line);
    result.string("directive", item.undef ? "undef" : "define");
    result.string("name", item.name);
    result.string("text", item.text);
    result.object_end();
  }
  result.array_end();
}

size_t directive_bytes(const std::vector<jh::config::directive> &directives) {
  size_t bytes = 0;
  for (const auto &item : directives)
    bytes += item.file.size() + item.name.size() + item.text.size() + 96;
  return bytes;
}

/* The #define/#undef of one file without evaluating it: the checks that look
 * at any branch, also of a header the reader rejects. */
int config_directives(context &ctx, const std::string &base) {
  require_value(ctx.args, file, "config directives requires --file");
  const auto path = jh::stl::absolute_path(ctx.args.values[file], base);
  jh::assign(ctx.source, path);
  const auto directives = jh::config::read_directives(path);
  if (!ctx.json) {
    std::string text;
    for (const auto &item : directives)
      text += std::to_string(item.line) +
              (item.undef ? " undef " : " define ") + item.name + '\n';
    stdout_bytes(text);
    return 0;
  }
  result_writer result(path.size() + directive_bytes(directives) + 1024);
  result.string("command", "config.directives");
  result.string("status", "HAL_OK");
  result.string("file", path);
  write_directives(result, directives);
  stdout_bytes(result.finish());
  return 0;
}

const char *integer_state(jh::config::integer_kind kind) {
  switch (kind) {
  case jh::config::integer_kind::value:
    return "value";
  case jh::config::integer_kind::out_of_range:
    return "outOfRange";
  default:
    return "none";
  }
}

int config_inspect(context &ctx, const std::string &base) {
  const auto &args = ctx.args;
  const auto path_of = [&](option_id id) {
    return args.present[id] ? jh::stl::absolute_path(args.values[id], base)
                            : std::string();
  };
  if ((args.present[header] && args.values[header].empty()) ||
      (args.present[preinclude] && args.values[preinclude].empty()))
    throw failure{HAL_EINVAL, 2,  "--header and --preinclude need a path",
                  {},         {}, {}};
  const std::string header_path = path_of(header);
  const std::string preinclude_path = path_of(preinclude);
  jh::assign(ctx.source, header_path);
  const auto config = jh::config::read_project_config(
      header_path, args.lists[define], preinclude_path);
  const auto macros = config.macros.ordered();
  std::vector<jh::config::integer_value> integers;
  std::vector<std::set<std::string>> unresolved;
  // Each value is a query with a budget of its own. A value the reader cannot
  // give (an invalid literal, a jh limit) fails only that macro, and the
  // caller decides: configure stops on an invalid literal, and on a value
  // over a limit only when the build reads it.
  std::vector<jh::failure> value_errors;
  size_t bytes = header_path.size() + preinclude_path.size() +
                 config.error_message.size() + config.error_source.size();
  for (const auto *item : macros) {
    jh::config::macro_value value;
    jh::failure error{HAL_OK, 0, {}, {}, {}, {}};
    try {
      value = config.value(item->name);
    } catch (const jh::failure &failed) {
      error = failed;
      bytes += error.message.size() + 64;
    }
    value_errors.push_back(std::move(error));
    integers.push_back(value.integer);
    unresolved.push_back(std::move(value.unresolved));
    bytes += item->name.size() + item->body.size() + item->source.size() + 64;
    for (const auto &name : unresolved.back())
      bytes += name.size() + 8;
    for (const auto &parameter : item->parameters)
      bytes += parameter.size() + 8;
  }
  for (const auto &file : config.files)
    bytes += file.size() + 8;
  bytes += directive_bytes(config.directives);
  for (const auto &item : config.variants) {
    bytes += item.id.size() + item.description.size() + item.source.size() + 64;
    for (const auto &definition : item.definitions)
      bytes += definition.size() + 8;
  }
  for (const auto &[name, unknown] : config.dependencies) {
    bytes += name.size() + 32;
    for (const auto &item : unknown)
      bytes += item.size() + 8;
  }
  if (!ctx.json) {
    std::string text;
    for (const auto *item : macros) {
      text += "#define " + item->name;
      if (item->function_like) {
        text += '(';
        for (size_t i = 0; i < item->parameters.size(); ++i)
          text += (i != 0 ? "," : "") + item->parameters[i];
        text += ')';
      }
      if (!item->body.empty())
        text += ' ' + item->body;
      text += '\n';
    }
    stdout_bytes(text);
    if (config.stopped)
      std::fprintf(stderr, "%s: #error %s\n", config.error_source.c_str(),
                   config.error_message.c_str());
    return 0;
  }
  result_writer result(bytes);
  result.string("command", "config.inspect");
  result.string("status", "HAL_OK");
  result.string("header", header_path);
  result.string("preinclude", preinclude_path);
  result.strings("files", config.files);
  result.strings("targets", config.targets);
  result.array_begin("variants");
  for (const auto &item : config.variants) {
    result.object_begin(nullptr);
    result.string("id", item.id);
    result.string("description", item.description);
    result.strings("definitions", item.definitions);
    result.string("source", item.source);
    result.object_end();
  }
  result.array_end();
  result.array_begin("macros");
  for (size_t i = 0; i < macros.size(); ++i) {
    const auto &item = *macros[i];
    result.object_begin(nullptr);
    result.string("name", item.name);
    result.string("body", item.body);
    result.string("source", item.source);
    result.boolean("functionLike", item.function_like);
    result.strings("parameters", item.parameters);
    const auto &error = value_errors[i];
    if (error.status != HAL_OK) {
      result.string("integerState", "error");
      result.object_begin("integerError");
      result.string("status", hal_status_to_string(error.status));
      result.string("message", error.message);
      result.object_end();
    } else {
      result.string("integerState", integer_state(integers[i].kind));
      if (integers[i].kind == jh::config::integer_kind::value)
        result.signed_number("integer", integers[i].value);
    }
    result.strings("unresolved", unresolved[i]);
    result.object_end();
  }
  result.array_end();
  std::vector<std::string> features;
  for (const auto *item : config.features())
    features.push_back(item->name);
  result.strings("features", features);
  result.array_begin("dependencies");
  for (const auto &[name, unknown] : config.dependencies) {
    result.object_begin(nullptr);
    result.string("name", name);
    result.strings("unknown", unknown);
    result.object_end();
  }
  result.array_end();
  write_directives(result, config.directives);
  if (config.stopped) {
    result.object_begin("error");
    result.string("message", config.error_message);
    result.string("source", config.error_source);
    result.object_end();
  } else {
    result.null("error");
  }
  stdout_bytes(result.finish());
  return 0;
}

const command_spec *select_command(const std::vector<std::string> &words) {
  if (words.empty())
    return nullptr;
  for (const auto &spec : commands)
    if (words.size() == 2 && words[0] == spec.first && words[1] == spec.second)
      return &spec;
  throw failure{HAL_EINVAL, 2, "unknown command; use jh --help", {}, {}, {}};
}

void reject_foreign_options(const context &ctx) {
  for (size_t id = 0; id < machine; ++id) {
    if (!ctx.args.present[id])
      continue;
    const auto &accepted = ctx.command->accepted;
    const auto wanted = static_cast<option_id>(id);
    if (std::find(accepted.begin(), accepted.end(), wanted) == accepted.end())
      throw failure{HAL_EINVAL,
                    2,
                    std::string("--") + options[id].name +
                        " does not apply to " + ctx.command->first + " " +
                        ctx.command->second,
                    {},
                    {},
                    {}};
  }
}

int run(const std::vector<std::string> &argv) {
  context ctx;
  ctx.json = std::find(argv.begin() + 1, argv.end(), "--json") != argv.end();
  try {
    ctx.args = parse_arguments(argv);
    const auto &args = ctx.args;
    ctx.command = select_command(args.command);
    if (args.present[json_version]) {
      if (!ctx.json)
        throw failure{HAL_EINVAL, 2,  "--json-version requires --json",
                      {},         {}, {}};
      const size_t requested =
          count_option(args, json_version, 1, UINT32_MAX,
                       "--json-version requires an unsigned major version");
      if (requested != 1)
        throw failure{HAL_EUNSUPPORTED,
                      2,
                      "unsupported JSON result version; supported: 1",
                      {},
                      {},
                      {}};
    }
    if (args.present[help] || (argv.size() == 1)) {
      const std::string text = help_text();
      if (ctx.json) {
        result_writer result(text.size());
        result.string("command", "help");
        result.string("status", "HAL_OK");
        result.string("help", text);
        stdout_bytes(result.finish());
      } else
        stdout_bytes(text);
      return 0;
    }
    if (args.present[version]) {
      for (size_t id = 0; id < machine; ++id)
        if (args.present[id])
          throw failure{
              HAL_EINVAL, 2,  "--version does not take command options",
              {},         {}, {}};
      if (!args.command.empty())
        throw failure{HAL_EINVAL, 2, "--version is a root option", {}, {}, {}};
      if (ctx.json) {
        result_writer result(128);
        result.string("command", "version");
        result.string("status", "HAL_OK");
        result.string("version", JH_TOOL_VERSION);
        stdout_bytes(result.finish());
      } else
        stdout_bytes("jh " JH_TOOL_VERSION "\n");
      return 0;
    }
    if (ctx.command == nullptr)
      throw failure{HAL_EINVAL, 2, "a command is required", {}, {}, {}};
    reject_foreign_options(ctx);
    const auto base = std::filesystem::current_path().u8string();
    if (ctx.command == &commands[repository_command])
      return repository_files(ctx, base);
    if (ctx.command == &commands[validate_command])
      return validate(ctx, base);
    if (ctx.command == &commands[defaults_command])
      return generate_defaults(ctx, base);
    if (ctx.command == &commands[features_command])
      return generate_features(ctx, base);
    if (ctx.command == &commands[config_command])
      return config_inspect(ctx, base);
    if (ctx.command == &commands[directives_command])
      return config_directives(ctx, base);
    if (ctx.command == &commands[feature_list_command])
      return feature_list(ctx, base);
    if (ctx.command == &commands[feature_resolve_command])
      return feature_resolve(ctx, base);
    if (ctx.command == &commands[feature_lint_command])
      return feature_lint(ctx, base);
    if (ctx.command == &commands[manifest_command])
      return config_manifest(ctx, base);
    if (ctx.command == &commands[board_list_command])
      return board_list(ctx, base);
    if (ctx.command == &commands[board_validate_command])
      return board_validate(ctx, base);
    if (ctx.command == &commands[board_default_command])
      return board_default(ctx, base);
    if (ctx.command == &commands[clock_check_command])
      return clock_check(ctx, base);
    if (ctx.command == &commands[boards_command])
      return generate_boards(ctx, base);
    if (ctx.command == &commands[board_config_command])
      return board_config(ctx, base);
    if (ctx.command == &commands[clock_list_command])
      return clock_list(ctx, base);
    return inspect(ctx, base);
  } catch (const failure &error) {
    if (ctx.located) {
      std::fprintf(stderr, "%s:%zu:%zu: %s (%s)\n", jb_string_data(&ctx.source),
                   ctx.line, ctx.column, error.message.c_str(),
                   hal_status_to_string(error.status));
    } else {
      std::fprintf(stderr, "jh: %s (%s)\n", error.message.c_str(),
                   hal_status_to_string(error.status));
    }
    if (ctx.json) {
      result_writer result(jb_string_size(&ctx.source) + error.message.size() +
                           1024);
      result.string("command",
                    ctx.command == nullptr ? "jh" : ctx.command->name);
      result.string("status", hal_status_to_string(error.status));
      result.string("message", error.message);
      if (ctx.process_attempted)
        result.process(ctx.process);
      if (!error.code.empty()) {
        result.string("code", error.code);
        result.string("source", error.source);
        result.string("pointer", error.pointer);
      } else {
        if (jb_string_size(&ctx.source) != 0)
          result.string("source", jh::view(ctx.source));
        if (ctx.args.present[pointer])
          result.string("pointer", ctx.args.values[pointer]);
      }
      if (ctx.located) {
        result.number("jsonError", ctx.error.code);
        result.number("offset", ctx.error.offset);
        result.number("line", ctx.line);
        result.number("column", ctx.column);
      }
      stdout_bytes(result.finish());
    }
    return error.exit_code;
  }
}

void fallback_json(bool requested, hal_status_t status) {
  if (requested) {
    std::fprintf(stdout,
                 "{\"formatVersion\":1,\"command\":\"jh\",\"status\":\"%s\","
                 "\"message\":\"operation failed\"}\n",
                 hal_status_to_string(status));
    (void)std::fflush(stdout);
  }
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
  bool requested_json = false;
  for (int i = 1; i < argc; ++i) {
#ifdef _WIN32
    requested_json = requested_json || std::wcscmp(argv[i], L"--json") == 0;
#else
    requested_json = requested_json || std::strcmp(argv[i], "--json") == 0;
#endif
  }
#ifndef _WIN32
  (void)std::signal(SIGPIPE, SIG_IGN);
#endif
  try {
#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1)
      return 3;
#endif
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
#ifdef _WIN32
      const int length =
          WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1,
                              nullptr, 0, nullptr, nullptr);
      if (length == 0)
        throw failure{HAL_EPROTO, 2,  "argument is not valid UTF-16",
                      {},         {}, {}};
      std::string text(static_cast<size_t>(length), '\0');
      if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1,
                              text.data(), length, nullptr, nullptr) != length)
        throw failure{HAL_EPROTO, 2,  "cannot convert argument to UTF-8",
                      {},         {}, {}};
      text.pop_back();
      args.push_back(std::move(text));
#else
      args.emplace_back(argv[i]);
#endif
    }
    return run(args);
  } catch (const std::bad_alloc &) {
    std::fputs("jh: allocation failed (HAL_ENOMEM)\n", stderr);
    fallback_json(requested_json, HAL_ENOMEM);
    return 4;
  } catch (const failure &error) {
    fallback_json(requested_json, error.status);
    std::fprintf(stderr, "jh: %s (%s)\n", error.message.c_str(),
                 hal_status_to_string(error.status));
    return error.exit_code;
  } catch (const std::exception &error) {
    fallback_json(requested_json, HAL_EIO);
    std::fprintf(stderr, "jh: %s (HAL_EIO)\n", error.what());
    return 3;
  } catch (...) {
    fallback_json(requested_json, HAL_EINTERNAL);
    std::fputs("jh: unexpected failure (HAL_EINTERNAL)\n", stderr);
    return 3;
  }
}
