#include "jh_json_file.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <jb/json_number.h>
#include <new>
#include <stdexcept>

namespace jh {

void require(hal_status_t status, int exit_code, const char *message) {
  if (status != HAL_OK)
    throw failure{status, exit_code, message, {}, {}, {}};
}

jb_json_span_t span(const std::string &text) {
  return {text.data(), text.size()};
}
jb_json_span_t span(const char *text) { return {text, std::strlen(text)}; }
std::string copied(jb_json_span_t text) {
  return std::string(text.data, text.length);
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

std::string schema_identity(const std::string &path) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  for (char byte : path) {
    auto c = static_cast<unsigned char>(byte);
#ifdef _WIN32
    if (c == '\\')
      c = '/';
#endif
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
        c == '~' || c == '/' || c == ':') {
      result += static_cast<char>(c);
    } else {
      result += '%';
      result += hex[c >> 4];
      result += hex[c & 15];
    }
  }
  return result;
}

namespace {
/* The hardware loader keeps the exact token, but a decimal or exponent
 * spelling must still have a finite, nonzero magnitude in binary64, as the
 * Python loader required. Plain integers are exact at any size. */
void check_hardware_numbers(parse_location &where, json_document &owned) {
  const auto &doc = owned.doc;
  for (size_t index = 0; index < doc.token_count; ++index) {
    const jb_json_token_t &token = doc.tokens[index];
    if (token.type != JB_JSON_NUMBER)
      continue;
    const char *text = doc.input.data + token.offset;
    if (std::memchr(text, '.', token.length) == nullptr &&
        std::memchr(text, 'e', token.length) == nullptr &&
        std::memchr(text, 'E', token.length) == nullptr)
      continue;
    const jb_json_node_t node{&doc, index};
    size_t scratch_bytes;
    require(jb_json_float_workspace_size(node, 64, &scratch_bytes), 4,
            "number workspace overflow");
    std::vector<unsigned char> scratch(scratch_bytes);
    const jb_json_float_options_t options{true, false,
                                          64 * token.length + 1000000};
    uint64_t bits;
    const hal_status_t status = jb_json_get_f64_bits(
        node, &options, {scratch.data(), scratch.size()}, &bits);
    if (status != HAL_OK) {
      where.error = {JB_JSON_ERROR_NONE, token.offset, SIZE_MAX, 0};
      where.located = true;
      (void)jb_json_locate(doc.input, token.offset, &where.line, &where.column);
      throw failure{HAL_EPROTO,
                    2,
                    "number " + std::string(text, token.length) +
                        " is outside the binary64 range or exceeds the "
                        "conversion budget",
                    {},
                    {},
                    {}};
    }
  }
}
} // namespace

std::unique_ptr<json_document> read_document(parse_location &where,
                                             const std::string &path,
                                             size_t limit,
                                             bool hardware_numbers) {
  auto owned = std::make_unique<json_document>();
  owned->uri = path;
  owned->identity = schema_identity(path);
  where.source = path;
  auto &input = owned->input;
  const hal_status_t read_status =
      jb_file_read(where.source.c_str(), limit, &input.data);
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
      where.located = true;
      (void)jb_json_locate(bytes, where.error.offset, &where.line,
                           &where.column);
      require(status, status == HAL_EOVERFLOW || status == HAL_ENOMEM ? 4 : 2,
              "invalid JSON or parser limit exceeded");
    }
  };
  size_t scratch_bytes;
  require(jb_json_parse_workspace_size(&limits, &scratch_bytes), 4,
          "parser workspace overflow");
  std::vector<unsigned char> scratch(scratch_bytes);
  parsed(jb_json_measure(bytes, &limits, {scratch.data(), scratch.size()},
                         &needed, &where.error));
  owned->tokens.resize(needed.node_count);
  auto &tokens = owned->tokens;
  auto &document = owned->doc;
  parsed(jb_json_parse(bytes, &limits, tokens.data(), tokens.size(),
                       {scratch.data(), scratch.size()}, &document,
                       &where.error));
  where.located = false;
  if (hardware_numbers)
    check_hardware_numbers(where, *owned);
  return owned;
}

const json_document &schema_files::root(const std::string &path) {
  documents.push_back(read_document(where, path, max_bytes));
  const auto &loaded = *documents.front();
  schema_bytes = loaded.doc.input.length;
  return loaded;
}

hal_status_t load_schema(void *user, jb_json_span_t base_uri,
                         jb_json_span_t reference,
                         jh_json_schema_source_t *out) {
  auto &state = *static_cast<schema_files *>(user);
  try {
    const std::string ref = copied(reference);
    if (ref.find(':') != std::string::npos || ref.rfind("//", 0) == 0 ||
        ref.find('\\') != std::string::npos ||
        ref.find('\0') != std::string::npos)
      return HAL_EUNSUPPORTED;
    const json_document *base_document = nullptr;
    const auto base_identifier = copied(base_uri);
    for (const auto &doc : state.documents)
      if (doc->identity == base_identifier) {
        base_document = doc.get();
        break;
      }
    if (base_document == nullptr)
      return HAL_EINTERNAL;
    const auto parent =
        std::filesystem::u8path(base_document->uri).parent_path().u8string();
    const auto path = absolute_path(ref, parent);
    for (const auto &doc : state.documents) {
      std::error_code error;
      if (doc->uri == path ||
          std::filesystem::equivalent(std::filesystem::u8path(doc->uri),
                                      std::filesystem::u8path(path), error)) {
        *out = {span(doc->identity), &doc->doc};
        return HAL_OK;
      }
    }
    if (state.documents.size() >= 256)
      return HAL_EOVERFLOW;
    constexpr size_t total_limit = 64 * 1024 * 1024;
    if (state.schema_bytes >= total_limit)
      return HAL_EOVERFLOW;
    auto doc = read_document(
        state.where, path,
        std::min(state.max_bytes, total_limit - state.schema_bytes));
    state.schema_bytes += doc->doc.input.length;
    *out = {span(doc->identity), &doc->doc};
    state.documents.push_back(std::move(doc));
    return HAL_OK;
  } catch (const failure &e) {
    return e.status;
  } catch (const std::bad_alloc &) {
    return HAL_ENOMEM;
  } catch (...) {
    return HAL_EINTERNAL;
  }
}

hal_status_t collect_report(void *user, const jh_json_schema_error_t *error) {
  try {
    auto &reports = static_cast<schema_files *>(user)->reports;
    reports.push_back({copied(error->instance_pointer),
                       copied(error->schema_uri), copied(error->schema_pointer),
                       copied(error->reason_code), copied(error->message)});
    return HAL_OK;
  } catch (const std::bad_alloc &) {
    return HAL_ENOMEM;
  } catch (...) {
    return HAL_EINTERNAL;
  }
}

const jh_json_schema_limits_t &schema_limits() {
  static const jh_json_schema_limits_t limits{
      50000000,        64,      1024,    8192,  32768,
      1024 * 1024,     100000,  1000000, 16384, 4096,
      2 * 1024 * 1024, 1000000, 128,     1024};
  return limits;
}

hal_status_t validate_with(schema_files &state, const json_document &instance,
                           std::vector<unsigned char> &scratch) {
  if (state.documents.empty())
    throw failure{HAL_ESTATE, 3, "no root schema is loaded", {}, {}, {}};
  const auto &root = *state.documents.front();
  const jh_json_schema_source_t source{span(root.identity), &root.doc};
  const auto &limits = schema_limits();
  if (scratch.empty()) {
    size_t needed;
    require(jh_json_schema_workspace_size(&limits, &needed), 4,
            "schema workspace overflow");
    scratch.resize(needed);
  }
  state.reports.clear();
  const auto status = jh_json_schema_validate(
      &instance.doc, &source, load_schema, &state, collect_report, &state,
      {scratch.data(), scratch.size()}, &limits);
  if (status != HAL_OK && status != HAL_ECONFIG)
    require(status,
            status == HAL_EOVERFLOW || status == HAL_ENOMEM ? 4
            : status == HAL_ENOENT || status == HAL_EIO || status == HAL_EPERM
                ? 3
                : 2,
            "cannot validate schema or validation limit exceeded");
  return status;
}

} // namespace jh
