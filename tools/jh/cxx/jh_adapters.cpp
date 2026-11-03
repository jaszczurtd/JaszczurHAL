#include "jh_adapters.h"
#include <cstring>
#include <new>
#include <stdexcept>

namespace jh {

void require(hal_status_t status, int exit_code, const char *message) {
  if (status != HAL_OK)
    throw failure{status, exit_code, message, {}, {}, {}};
}

void failure_record::check(hal_status_t status) const {
  if (status == HAL_OK)
    return;
  // A record without a message is a jhbase container that could not grow.
  if (jb_string_size(&value.message) == 0)
    check_container(status);
  throw failure{status, value.exit_code, view(value.message), {}, {}, {}};
}

jb_json_span_t span(std::string_view text) {
  return {text.data(), text.size()};
}
jb_json_span_t span(const char *text) { return {text, std::strlen(text)}; }
owned_string copied(jb_json_span_t text) {
  return owned_string(text.data, text.length);
}

std::string_view view(const jb_string_t &text) {
  const char *bytes = jb_string_data(&text);
  return {bytes != nullptr ? bytes : "", jb_string_size(&text)};
}

void assign(jb_string_t &text, std::string_view value) {
  check_container(jb_string_assign(&text, value.data(), value.size()));
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

owned_string absolute_path(const char *path, const char *base) {
  owned_string result;
  failure_record failed;
  failed.check(jh_absolute_path(path, base, result.raw(), &failed.value));
  return result;
}

std::unique_ptr<json_document> read_document(parse_location &where,
                                             const char *path, size_t limit,
                                             bool hardware_numbers) {
  auto owned = std::make_unique<json_document>();
  failure_record failed;
  failed.check(jh_json_read_document(&where, path, limit, hardware_numbers,
                                     owned.get(), &failed.value));
  return owned;
}

settings_file read_settings(parse_location &where, const char *path,
                            size_t limit) {
  jb_json_tree_t *tree = nullptr;
  bool exists = false;
  failure_record failed;
  failed.check(jh_json_read_settings(&where, path, limit, &tree, &exists,
                                     &failed.value));
  return {tree_owner(tree), exists};
}

const jh_json_document_t &schema_files::root(const char *path) {
  const jh_json_document_t *loaded = nullptr;
  failure_record failed;
  failed.check(jh_schema_files_root(this, path, &loaded, &failed.value));
  return *loaded;
}

hal_status_t validate_with(schema_files &state,
                           const jh_json_document_t &instance) {
  failure_record failed;
  const hal_status_t status =
      jh_schema_files_validate(&state, &instance, &failed.value);
  if (status != HAL_ECONFIG)
    failed.check(status);
  return status;
}

owned_string python_repr(std::string_view text) {
  owned_string out;
  check_container(jh_python_repr_append(out.raw(), span(text)));
  return out;
}

bool write_generated(const char *path, std::string_view content) {
  bool changed = false;
  failure_record failed;
  failed.check(
      jh_write_generated(path, span(content), &changed, &failed.value));
  return changed;
}

bool check_generated(const char *path, std::string_view content,
                     size_t diff_limit, std::string_view label) {
  bool current = false;
  failure_record failed;
  failed.check(jh_check_generated(path, span(content), diff_limit, span(label),
                                  &current, &failed.value));
  return current;
}

} // namespace jh
