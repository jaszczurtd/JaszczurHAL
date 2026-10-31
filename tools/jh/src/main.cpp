#include "../hardware/jh_device_defaults.h"
#include "../hardware/jh_type_catalogue.h"
#include "../json/jh_json_file.h"
#include "jh_generated_output.h"
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
  write_mode,
  check,
  max_bytes,
  git,
  timeout_ms,
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
    {"file", "<path>", "Read this JSON file"},
    {"schema", "<path>", "Validate with this local schema (json validate)"},
    {"pointer", "<path>", "Select a plain RFC 6901 pointer (default: root)"},
    {"output", "<path>", "Atomically write the selected JSON value"},
    {"root", "<path>", "JaszczurHAL checkout the generator reads"},
    {"bindings", "<path>",
     "Directory of HAL types (default: <root>/config/hardware/bindings)"},
    {"output-root", "<path>",
     "Root the generated header path is relative to (default: <root>)"},
    {"write", nullptr, "Refresh the generated artifact"},
    {"check", nullptr,
     "Compare the output without changing files; exit 1 on drift"},
    {"max-bytes", "<count>",
     "Maximum input bytes (default 16777216, maximum 67108864)"},
    {"git", "<path>", "Absolute Git executable path (repository files)"},
    {"timeout-ms", "<count>", "Child process timeout; zero waits indefinitely"},
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
  repository_command
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
    {"repository",
     "files",
     "repository.files",
     {root, git, timeout_ms, max_bytes}},
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
    if (result.present[id])
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
      "       jh repository files --root <path> --git <executable> "
      "[options]\n\n"
      "Read JSON, select a value and optionally compare or write it, or\n"
      "generate the C defaults of the HAL component types.\n";
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

hal_status_t append(void *user, const char *data, size_t length) {
  try {
    static_cast<std::string *>(user)->append(data, length);
    return HAL_OK;
  } catch (const std::bad_alloc &) {
    return HAL_ENOMEM;
  } catch (const std::length_error &) {
    return HAL_EOVERFLOW;
  } catch (...) {
    return HAL_EINTERNAL;
  }
}

class result_writer {
  jb_json_writer_t writer{};
  std::vector<unsigned char> scratch;
  std::string text;

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
                                {scratch.data(), scratch.size()}, append,
                                &text),
            4, "cannot initialize result writer");
    require(jb_json_writer_object_begin(&writer), 4, "cannot start result");
    number("formatVersion", 1);
  }
  void key(const char *name) {
    require(jb_json_writer_key(&writer, span(name)), 4,
            "cannot write result key");
  }
  void string(const char *name, const std::string &value) {
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
  void node(jb_json_node_t value) {
    key("value");
    require(jb_json_writer_node(&writer, value), 4,
            "cannot write selected JSON");
  }
  void schema_errors(const std::vector<jh::schema_report> &errors) {
    key("errors");
    require(jb_json_writer_array_begin(&writer), 4,
            "cannot start schema errors");
    for (const auto &error : errors) {
      require(jb_json_writer_object_begin(&writer), 4,
              "cannot start schema error");
      string("instancePointer", error.pointer);
      string("schemaUri", error.uri);
      string("schemaPointer", error.rule);
      string("reason", error.reason);
      string("message", error.message);
      require(jb_json_writer_object_end(&writer), 4,
              "cannot close schema error");
    }
    require(jb_json_writer_array_end(&writer), 4, "cannot close schema errors");
  }
  void paths(const std::string &bytes) {
    key("paths");
    require(jb_json_writer_array_begin(&writer), 4, "cannot start paths");
    size_t begin = 0;
    while (begin < bytes.size()) {
      const size_t nul = bytes.find('\0', begin);
      const size_t end = nul == std::string::npos ? bytes.size() : nul;
      if (end != begin) {
        size_t bad = 0;
        const jb_json_span_t path{bytes.data() + begin, end - begin};
        require(jb_utf8_validate(path.data, path.length, &bad), 2,
                "repository path is not UTF-8; use raw output");
        require(jb_json_writer_string(&writer, path), 4, "cannot write path");
      }
      if (nul == std::string::npos)
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
  std::string finish() {
    require(jb_json_writer_object_end(&writer), 4, "cannot close result");
    size_t count;
    require(jb_json_writer_finish(&writer, &count), 4, "cannot finish result");
    text += '\n';
    return std::move(text);
  }
};

void stdout_bytes(const std::string &text) {
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
      ctx, jh::absolute_path(args.values[file], base), input_limit(args));
  const auto &document = owned->doc;
  const auto &tokens = owned->tokens;
  const auto bytes = document.input;
  jb_json_node_t selected;
  require(jb_json_pointer_get(&document, span(args.values[pointer]), &selected),
          2, "cannot resolve JSON Pointer");
  jb_json_span_t raw;
  require(jb_json_raw(selected, &raw), 2, "cannot read selected value");
  bool changed = false;
  std::string destination;
  if (args.present[output]) {
    destination = jh::absolute_path(args.values[output], base);
    std::string content(raw.data, raw.length);
    content += '\n';
    require(jb_file_write_atomic(destination.c_str(), content.data(),
                                 content.size(), args.present[check], &changed),
            3, "cannot publish output file");
  }
  const bool stale = args.present[check] && changed;
  if (ctx.json) {
    const size_t keys = static_cast<size_t>(
        std::count_if(tokens.begin(), tokens.end(),
                      [](const auto &token) { return token.has_key; }));
    result_writer result(bytes.length, keys);
    result.string("command", "json.inspect");
    result.string("status", stale ? "HAL_ESTATE" : "HAL_OK");
    result.string("source", ctx.source);
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
  const auto input_path = jh::absolute_path(args.values[file], base);
  auto input = jh::read_document(ctx, input_path, max);
  jh::schema_files state(ctx, max);
  const auto &root_schema =
      state.root(jh::absolute_path(args.values[schema], base));
  std::vector<unsigned char> scratch;
  const auto status = jh::validate_with(state, *input, scratch);
  ctx.source = input_path;
  for (const auto &error : state.reports) {
    std::fprintf(stderr, "%s:", input_path.c_str());
    if (error.pointer.empty())
      (void)std::fputc('/', stderr);
    else
      (void)std::fwrite(error.pointer.data(), 1, error.pointer.size(), stderr);
    (void)std::fputs(": ", stderr);
    (void)std::fwrite(error.message.data(), 1, error.message.size(), stderr);
    (void)std::fputc('\n', stderr);
  }
  if (ctx.json) {
    result_writer result(1024 * 1024, state.reports.size() * 8);
    result.string("command", "json.validate");
    result.string("status", hal_status_to_string(status));
    result.string("source", input_path);
    result.string("schema", root_schema.uri);
    result.schema_errors(state.reports);
    stdout_bytes(result.finish());
  }
  return status == HAL_OK ? 0 : 2;
}

int generate_defaults(context &ctx, const std::string &base) {
  const auto &args = ctx.args;
  require_value(args, root, "generate device-defaults requires --root");
  if (args.present[write_mode] == args.present[check])
    throw failure{HAL_EINVAL,
                  2,
                  "generate device-defaults requires exactly one of --write "
                  "and --check",
                  {},
                  {},
                  {}};
  constexpr size_t limit = 16 * 1024 * 1024;
  auto hal_root = jh::absolute_path(args.values[root], base);
  // A root given as "." resolves with a trailing separator; report it plain.
  if (hal_root.size() > 1 &&
      (hal_root.back() == '/' || hal_root.back() == '\\') &&
      !(hal_root.size() == 3 && hal_root[1] == ':'))
    hal_root.pop_back();
  const auto bindings_dir =
      args.present[bindings]
          ? jh::absolute_path(args.values[bindings], base)
          : jh::absolute_path("config/hardware/bindings", hal_root);
  const auto out_root = args.present[output_root]
                            ? jh::absolute_path(args.values[output_root], base)
                            : hal_root;
  const auto artifacts = jh::read_document(
      ctx, jh::absolute_path("config/tooling/artifacts.json", hal_root), limit);
  jb_json_node_t artifacts_root;
  require(jb_json_root(&artifacts->doc, &artifacts_root), 2,
          "artifacts.json has no root");
  jh::hardware::view header_entry;
  if (!jh::hardware::view{artifacts_root}.is(JB_JSON_OBJECT) ||
      !jh::hardware::view{artifacts_root}
           .member("generatedArtifacts")
           .find("deviceDefaultsHeader", &header_entry) ||
      !header_entry.is(JB_JSON_STRING))
    throw failure{HAL_ECONFIG,
                  2,
                  "config/tooling/artifacts.json lacks "
                  "generatedArtifacts.deviceDefaultsHeader",
                  {},
                  {},
                  {}};
  const std::string header_relative = header_entry.string();
  const auto output_path = jh::absolute_path(header_relative, out_root);
  const auto features = jh::hardware::hal_features(ctx, hal_root, limit);
  const auto capabilities =
      jh::hardware::board_capabilities(ctx, hal_root, limit);
  const auto types = jh::hardware::load_hal_catalogue(
      ctx, jh::absolute_path("config/hardware", hal_root), bindings_dir, limit);
  jh::hardware::check_catalogue(types, &features, &capabilities);
  const std::string content = jh::hardware::render_device_defaults(types);
  ctx.source = output_path;
  bool changed = false;
  bool current = true;
  if (args.present[write_mode]) {
    changed = jh::write_generated(output_path, content);
    if (!ctx.json)
      stdout_bytes("generated 1 device defaults artifacts (" +
                   std::to_string(changed ? 1 : 0) + " changed)\n");
  } else {
    current = jh::check_generated(output_path, content, 40,
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
  std::string bytes;
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
  return append(&output.bytes, bytes, length);
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
  const auto directory = jh::absolute_path(args.values[root], base);
  const auto executable = jh::absolute_path(args.values[git], base);
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
  ctx.source = directory;
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
    return inspect(ctx, base);
  } catch (const failure &error) {
    if (ctx.located) {
      std::fprintf(stderr, "%s:%zu:%zu: %s (%s)\n", ctx.source.c_str(),
                   ctx.line, ctx.column, error.message.c_str(),
                   hal_status_to_string(error.status));
    } else {
      std::fprintf(stderr, "jh: %s (%s)\n", error.message.c_str(),
                   hal_status_to_string(error.status));
    }
    if (ctx.json) {
      result_writer result(ctx.source.size() + error.message.size() + 1024);
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
        if (!ctx.source.empty())
          result.string("source", ctx.source);
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
