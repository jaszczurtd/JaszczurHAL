#include "jh_json_file.h"
#include <algorithm>
#include <cstring>
#include <jb/filesystem.h>
#include <jb/json_number.h>
#include <new>
#include <stdexcept>

namespace jh {

void require(hal_status_t status, int exit_code, const char *message) {
  if (status != HAL_OK)
    throw failure{status, exit_code, message, {}, {}, {}};
}

jb_json_span_t span(std::string_view text) {
  return {text.data(), text.size()};
}
jb_json_span_t span(const char *text) { return {text, std::strlen(text)}; }
owned_string copied(jb_json_span_t text) {
  return owned_string(text.data, text.length);
}

hal_status_t append_text(void *user, const char *data, size_t length) {
  try {
    static_cast<owned_string *>(user)->append(std::string_view(data, length));
    return HAL_OK;
  } catch (const std::bad_alloc &) {
    return HAL_ENOMEM;
  } catch (const std::length_error &) {
    return HAL_EOVERFLOW;
  } catch (...) {
    return HAL_EINTERNAL;
  }
}

namespace {
/* The result of a jhbase path function that measures with (NULL, 0) and then
 * writes a NUL-terminated path of the reported size. */
template <class Write> owned_string path_result(Write write) {
  size_t needed;
  require(write(nullptr, 0, &needed), 3, "cannot resolve path");
  owned_string result;
  if (needed > 1) {
    result.resize(needed - 1);
    require(write(result.mutable_data(), needed, &needed), 3,
            "cannot resolve path");
  }
  return result;
}
} // namespace

owned_string absolute_path(const char *path, const char *base) {
  return path_result([&](char *buffer, size_t capacity, size_t *needed) {
    return jb_path_resolve(base, path, buffer, capacity, needed);
  });
}

owned_string schema_identity(std::string_view path) {
  constexpr char hex[] = "0123456789ABCDEF";
  owned_string result;
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
/* Exit code 4 for an exhausted limit or allocation, otherwise the given one. */
int exit_code_for(hal_status_t status, int otherwise) {
  return status == HAL_EOVERFLOW || status == HAL_ENOMEM ? 4 : otherwise;
}

/* Parser limits of one file of at most limit bytes. */
jb_json_limits_t file_limits(size_t limit) {
  return {limit, 1000000, 256, limit, 1048576, 100000, limit * 32 + 1048576};
}

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
    owned_array<unsigned char> scratch;
    scratch.resize(scratch_bytes);
    const jb_json_float_options_t options{true, false,
                                          64 * token.length + 1000000};
    uint64_t bits;
    const hal_status_t status = jb_json_get_f64_bits(
        node, &options, {scratch.data(), scratch.size()}, &bits);
    if (status != HAL_OK) {
      where.error = {JB_JSON_ERROR_NONE, token.offset, SIZE_MAX, 0};
      where.located = true;
      (void)jb_json_locate(doc.input, token.offset, &where.line, &where.column);
      owned_string message("number ");
      message.append(std::string_view(text, token.length));
      message.append(" is outside the binary64 range or exceeds the "
                     "conversion budget");
      throw failure{HAL_EPROTO, 2, std::move(message), {}, {}, {}};
    }
  }
}
} // namespace

std::unique_ptr<json_document> read_document(parse_location &where,
                                             const char *path, size_t limit,
                                             bool hardware_numbers) {
  auto owned = std::make_unique<json_document>();
  owned->uri = path;
  owned->identity = schema_identity(path);
  where.source = path;
  auto &input = owned->input;
  const hal_status_t read_status =
      jb_file_read(where.source.c_str(), limit, &input.data);
  require(read_status, exit_code_for(read_status, 3), "cannot read input file");
  const jb_json_span_t bytes{reinterpret_cast<const char *>(input.data.data),
                             input.data.length};
  const jb_json_limits_t limits = file_limits(limit);
  jb_json_requirements_t needed;
  const auto parsed = [&](hal_status_t status) {
    if (status != HAL_OK) {
      where.located = true;
      (void)jb_json_locate(bytes, where.error.offset, &where.line,
                           &where.column);
      require(status, exit_code_for(status, 2),
              "invalid JSON or parser limit exceeded");
    }
  };
  size_t scratch_bytes;
  require(jb_json_parse_workspace_size(&limits, &scratch_bytes), 4,
          "parser workspace overflow");
  owned_array<unsigned char> scratch;
  scratch.resize(scratch_bytes);
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

settings_file read_settings(parse_location &where, const char *path,
                            size_t limit) {
  where.source = path;
  where.located = false;
  file_owner input;
  const hal_status_t read_status = jb_file_read(path, limit, &input.data);
  if (read_status != HAL_ENOENT)
    require(read_status, exit_code_for(read_status, 3),
            "cannot read input file");
  // load_json_file reads a missing file as an empty object.
  const jb_json_span_t bytes =
      read_status == HAL_ENOENT
          ? span("{}")
          : jb_json_span_t{reinterpret_cast<const char *>(input.data.data),
                           input.data.length};
  const jb_json_limits_t limits = file_limits(limit);
  const jb_json_tree_limits_t tree_limits{limits.max_nodes, limits.max_depth,
                                          limit, limits.max_key_compare_bytes};
  constexpr uint32_t python_reading = JB_JSON_TREE_COMMENTS |
                                      JB_JSON_TREE_LAST_DUPLICATE |
                                      JB_JSON_TREE_NONFINITE;
  jb_json_tree_t *tree = nullptr;
  const hal_status_t status = jb_json_tree_parse(
      bytes, &limits, &tree_limits, python_reading, &tree, &where.error);
  settings_file result{tree_owner(tree), read_status == HAL_OK};
  if (status != HAL_OK) {
    // The tree limits leave the code NONE and have no place in the file.
    if (where.error.code != JB_JSON_ERROR_NONE) {
      where.located = true;
      (void)jb_json_locate(bytes, where.error.offset, &where.line,
                           &where.column);
    }
    require(status, exit_code_for(status, 2),
            "invalid JSON or parser limit exceeded");
  }
  jb_json_tree_view_t root;
  jb_json_type_t type;
  require(jb_json_tree_root(result.tree.get(), &root), 2,
          "cannot read the settings root");
  require(jb_json_tree_type(root, &type), 2, "cannot read the settings root");
  if (type != JB_JSON_OBJECT) {
    owned_string message(path);
    message.append(" must contain a JSON object");
    throw failure{HAL_ETYPE, 2, std::move(message), {}, {}, {}};
  }
  return result;
}

const json_document &schema_files::root(const char *path) {
  documents.push_back(read_document(where, path, max_bytes));
  const auto &loaded = documents.front();
  schema_bytes = loaded.doc.input.length;
  return loaded;
}

hal_status_t load_schema(void *user, jb_json_span_t base_uri,
                         jb_json_span_t reference,
                         jh_json_schema_source_t *out) {
  auto &state = *static_cast<schema_files *>(user);
  try {
    const std::string_view ref(reference.data, reference.length);
    if (ref.find(':') != std::string_view::npos || ref.rfind("//", 0) == 0 ||
        ref.find('\\') != std::string_view::npos ||
        ref.find('\0') != std::string_view::npos)
      return HAL_EUNSUPPORTED;
    const std::string_view base_identifier(base_uri.data, base_uri.length);
    const json_document *base_document = nullptr;
    for (size_t index = 0; index < state.documents.size(); ++index)
      if (state.documents[index].identity.view() == base_identifier) {
        base_document = &state.documents[index];
        break;
      }
    if (base_document == nullptr)
      return HAL_EINTERNAL;
    const owned_string parent =
        path_result([&](char *buffer, size_t capacity, size_t *needed) {
          return jb_fs_path_parent(base_document->uri.c_str(), buffer, capacity,
                                   needed);
        });
    // A reference without NUL bytes (checked above) is a C string.
    const owned_string relative(ref);
    const owned_string path = absolute_path(relative.c_str(), parent.c_str());
    for (size_t index = 0; index < state.documents.size(); ++index) {
      const json_document &doc = state.documents[index];
      // An entry that cannot be compared (missing, unreadable) is another file.
      bool same = false;
      if (doc.uri.view() == path.view() ||
          (jb_fs_equivalent(doc.uri.c_str(), path.c_str(), &same) == HAL_OK &&
           same)) {
        *out = {span(doc.identity), &doc.doc};
        return HAL_OK;
      }
    }
    if (state.documents.size() >= 256)
      return HAL_EOVERFLOW;
    constexpr size_t total_limit = 64 * 1024 * 1024;
    if (state.schema_bytes >= total_limit)
      return HAL_EOVERFLOW;
    auto doc = read_document(
        state.where, path.c_str(),
        std::min(state.max_bytes, total_limit - state.schema_bytes));
    state.schema_bytes += doc->doc.input.length;
    const json_document &loaded = state.documents.push_back(std::move(doc));
    *out = {span(loaded.identity), &loaded.doc};
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
    reports.push_back(std::make_unique<schema_report>(
        schema_report{copied(error->instance_pointer),
                      copied(error->schema_uri), copied(error->schema_pointer),
                      copied(error->reason_code), copied(error->message)}));
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
                           owned_array<unsigned char> &scratch) {
  if (state.documents.empty())
    throw failure{HAL_ESTATE, 3, "no root schema is loaded", {}, {}, {}};
  const auto &root = state.documents.front();
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
            exit_code_for(status, status == HAL_ENOENT || status == HAL_EIO ||
                                          status == HAL_EPERM
                                      ? 3
                                      : 2),
            "cannot validate schema or validation limit exceeded");
  return status;
}

} // namespace jh
