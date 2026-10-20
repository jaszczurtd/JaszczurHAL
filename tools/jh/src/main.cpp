#include <algorithm>
#include <array>
#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <jb/array.h>
#include <jb/file.h>
#include <jb/json_pointer.h>
#include <jb/json_writer.h>
#include <jb/utf8.h>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

namespace {
enum option_id {
  file,
  pointer,
  output,
  check,
  max_bytes,
  machine,
  help,
  version,
  option_count
};
struct option_spec {
  const char *name, *value, *description;
};
constexpr option_spec options[] = {
    {"file", "<path>", "Read this JSON file (required for json inspect)"},
    {"pointer", "<path>", "Select a plain RFC 6901 pointer (default: root)"},
    {"output", "<path>", "Atomically write the selected JSON value"},
    {"check", nullptr,
     "Compare --output without changing files; exit 1 on drift"},
    {"max-bytes", "<count>",
     "Maximum input bytes (default 16777216, maximum 67108864)"},
    {"json", nullptr, "Write a versioned JSON result on stdout"},
    {"help", nullptr, "Show help"},
    {"version", nullptr, "Show the tool version"},
};
static_assert(COUNTOF(options) == option_count);
struct arguments {
  std::array<bool, option_count> present{};
  std::array<std::string, option_count> values;
  std::vector<std::string> command;
};
struct failure {
  hal_status_t status;
  int exit_code;
  std::string message;
};
struct context {
  arguments args;
  bool json = false;
  std::string source;
  jb_json_error_t error{};
  size_t line = 0, column = 0;
  bool located = false;
};
struct file_owner {
  jb_file_data_t data{};
  ~file_owner() { jb_file_release(&data); }
};

void require(hal_status_t status, int exit_code, const char *message) {
  if (status != HAL_OK)
    throw failure{status, exit_code, message};
}

jb_json_span_t span(const std::string &text) {
  return {text.data(), text.size()};
}
jb_json_span_t span(const char *text) { return {text, std::strlen(text)}; }

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
      throw failure{HAL_EINVAL, 2, "unknown option: " + text};
    if (result.present[id])
      throw failure{HAL_EINVAL, 2, "option specified more than once: " + name};
    result.present[id] = true;
    if (options[id].value != nullptr) {
      if (equal != std::string::npos) {
        result.values[id] = expanded.substr(equal + 1);
      } else {
        if (i + 1 == argv.size() || argv[i + 1].rfind("--", 0) == 0)
          throw failure{HAL_EINVAL, 2, "option requires a value: " + name};
        result.values[id] = argv[++i];
        require(jb_utf8_validate(result.values[id].data(),
                                 result.values[id].size(), &bad),
                2, "argument is not valid UTF-8");
      }
    } else if (equal != std::string::npos) {
      throw failure{HAL_EINVAL, 2, "flag does not take a value: " + name};
    }
  }
  return result;
}

std::string help_text() {
  std::string text =
      "Usage: jh [--help | --version] [--json]\n"
      "       jh json inspect --file <path> [options]\n\n"
      "Read JSON, select a value and optionally compare or write it.\n";
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
    throw failure{HAL_EIO, 3, "cannot write stdout"};
}

std::string absolute_path(const std::string &path, const std::string &base) {
  size_t needed;
  require(jb_path_resolve(base.c_str(), path.c_str(), nullptr, 0, &needed), 3,
          "cannot resolve path");
  std::vector<char> bytes(needed);
  require(jb_path_resolve(base.c_str(), path.c_str(), bytes.data(),
                          bytes.size(), &needed),
          3, "cannot resolve path");
  return std::string(bytes.data(), needed - 1);
}

int inspect(context &ctx, const std::string &base) {
  const arguments &args = ctx.args;
  if (!args.present[file] || args.values[file].empty())
    throw failure{HAL_EINVAL, 2, "json inspect requires --file"};
  if (args.present[check] && !args.present[output])
    throw failure{HAL_EINVAL, 2, "--check requires --output"};
  size_t limit = 16 * 1024 * 1024;
  if (args.present[max_bytes]) {
    const std::string &value = args.values[max_bytes];
    const auto converted =
        std::from_chars(value.data(), value.data() + value.size(), limit);
    if (converted.ec != std::errc{} ||
        converted.ptr != value.data() + value.size() ||
        limit > 64 * 1024 * 1024)
      throw failure{HAL_EINVAL, 2,
                    "--max-bytes requires an integer between 0 and 67108864"};
  }
  ctx.source = absolute_path(args.values[file], base);
  file_owner input;
  const hal_status_t read_status =
      jb_file_read(ctx.source.c_str(), limit, &input.data);
  require(read_status,
          read_status == HAL_EOVERFLOW || read_status == HAL_ENOMEM ? 4 : 3,
          "cannot read input file");
  const jb_json_span_t bytes{reinterpret_cast<const char *>(input.data.data),
                             input.data.length};
  const jb_json_limits_t limits{
      limit, 1000000, 256, limit, 1048576, 100000, limit * 32 + 1048576};
  jb_json_requirements_t needed;
  const auto parsed = [&](hal_status_t status) {
    if (status != HAL_OK) {
      ctx.located = true;
      (void)jb_json_locate(bytes, ctx.error.offset, &ctx.line, &ctx.column);
      require(status, status == HAL_EOVERFLOW || status == HAL_ENOMEM ? 4 : 2,
              "invalid JSON or parser limit exceeded");
    }
  };
  size_t scratch_bytes;
  require(jb_json_parse_workspace_size(&limits, &scratch_bytes), 4,
          "parser workspace overflow");
  std::vector<unsigned char> scratch(scratch_bytes);
  parsed(jb_json_measure(bytes, &limits, {scratch.data(), scratch.size()},
                         &needed, &ctx.error));
  std::vector<jb_json_token_t> tokens(needed.node_count);
  jb_json_doc_t document;
  parsed(jb_json_parse(bytes, &limits, tokens.data(), tokens.size(),
                       {scratch.data(), scratch.size()}, &document,
                       &ctx.error));
  ctx.located = false;
  jb_json_node_t selected;
  require(jb_json_pointer_get(&document, span(args.values[pointer]), &selected),
          2, "cannot resolve JSON Pointer");
  jb_json_span_t raw;
  require(jb_json_raw(selected, &raw), 2, "cannot read selected value");
  bool changed = false;
  std::string destination;
  if (args.present[output]) {
    destination = absolute_path(args.values[output], base);
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

int run(const std::vector<std::string> &argv) {
  context ctx;
  ctx.json = std::find(argv.begin() + 1, argv.end(), "--json") != argv.end();
  try {
    ctx.args = parse_arguments(argv);
    const auto &args = ctx.args;
    if (!args.command.empty() &&
        args.command != std::vector<std::string>{"json", "inspect"})
      throw failure{HAL_EINVAL, 2, "unknown command; use jh --help"};
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
          throw failure{HAL_EINVAL, 2,
                        "--version does not take command options"};
      if (!args.command.empty())
        throw failure{HAL_EINVAL, 2, "--version is a root option"};
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
    if (args.command.empty())
      throw failure{HAL_EINVAL, 2, "a command is required"};
    return inspect(ctx, std::filesystem::current_path().u8string());
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
      result_writer result(ctx.source.size() + 1024);
      result.string("command",
                    ctx.args.command ==
                            std::vector<std::string>{"json", "inspect"}
                        ? "json.inspect"
                        : "jh");
      result.string("status", hal_status_to_string(error.status));
      result.string("message", error.message);
      if (!ctx.source.empty())
        result.string("source", ctx.source);
      if (ctx.args.present[pointer])
        result.string("pointer", ctx.args.values[pointer]);
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
        throw failure{HAL_EPROTO, 2, "argument is not valid UTF-16"};
      std::string text(static_cast<size_t>(length), '\0');
      if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1,
                              text.data(), length, nullptr, nullptr) != length)
        throw failure{HAL_EPROTO, 2, "cannot convert argument to UTF-8"};
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
