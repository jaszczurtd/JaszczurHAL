#ifndef JH_ADAPTERS_H
#define JH_ADAPTERS_H

/** @file jh_adapters.h
 * C++ calls of the C layer of jh (jh_failure.h, jh_json_file.h,
 * jh_python_repr.h, jh_generated_output.h) for the modules that are still
 * C++: owners that release the C records, and exceptions in place of the
 * recorded failures. A failure with a message throws jh::failure; a failed
 * jhbase container throws what check_container() throws. The header goes away
 * with the last C++ caller. C++17.
 */
#include "jh_containers.h"
#include "jh_failure.h"
#include "jh_feature_registry.h"
#include "jh_generated_output.h"
#include "jh_json_file.h"
#include "jh_python_repr.h"
#include <memory>
#include <string_view>

namespace jh {

/** A failed operation: status, process exit code and a human message. Hardware
 * catalogue failures also carry their code, source file and JSON Pointer.
 */
struct failure {
  hal_status_t status;
  int exit_code;
  owned_string message;
  owned_string code, source, pointer;
};

/** Throw failure unless status is HAL_OK. */
void require(hal_status_t status, int exit_code, const char *message);

/** A C failure record released with its owner. */
struct failure_record {
  jh_failure_t value;
  failure_record() noexcept : value() { jh_failure_init(&value); }
  failure_record(const failure_record &) = delete;
  failure_record &operator=(const failure_record &) = delete;
  ~failure_record() { jh_failure_destroy(&value); }
  /** Throw what the record holds when status, the result of the call that
   * filled it, is not HAL_OK.
   */
  void check(hal_status_t status) const;
};

jb_json_span_t span(std::string_view text);
jb_json_span_t span(const char *text);
owned_string copied(jb_json_span_t text);
/** The bytes of a jhbase string. */
std::string_view view(const jb_string_t &text);
/** Replace the bytes of a jhbase string; throws like owned_string. */
void assign(jb_string_t &text, std::string_view value);
/** Output sink of the jhbase writer and formatter that appends to the
 * owned_string at user. HAL_ENOMEM or HAL_EOVERFLOW when it cannot grow.
 */
hal_status_t append_text(void *user, const char *data, size_t length);
/** jh_absolute_path as an owned string. */
owned_string absolute_path(const char *path, const char *base);

/** Owner of bytes read by jb_file_read. */
struct file_owner {
  jb_file_data_t data{};
  file_owner() = default;
  file_owner(const file_owner &) = delete;
  file_owner &operator=(const file_owner &) = delete;
  ~file_owner() { jb_file_release(&data); }
};

/** A jh_json_location_t released with its owner. */
struct parse_location : jh_json_location_t {
  parse_location() noexcept : jh_json_location_t() {
    jh_json_location_init(this);
  }
  /** Take the source of other, which starts empty again. */
  parse_location(parse_location &&other) noexcept : jh_json_location_t(other) {
    static_cast<jh_json_location_t &>(other) = jh_json_location_t();
    jh_json_location_init(&other);
  }
  parse_location(const parse_location &) = delete;
  parse_location &operator=(const parse_location &) = delete;
  ~parse_location() { jh_json_location_destroy(this); }
};

/** A jh_json_document_t released with its owner. */
struct json_document : jh_json_document_t {
  json_document() noexcept : jh_json_document_t() {}
  json_document(const json_document &) = delete;
  json_document &operator=(const json_document &) = delete;
  ~json_document() { jh_json_document_destroy(this); }
};

/** jh_json_read_document into a new document. */
std::unique_ptr<json_document> read_document(parse_location &where,
                                             const char *path, size_t limit,
                                             bool hardware_numbers = false);

/** Destroys an owned jhbase tree. */
struct tree_deleter {
  void operator()(jb_json_tree_t *tree) const { jb_json_tree_destroy(tree); }
};
using tree_owner = std::unique_ptr<jb_json_tree_t, tree_deleter>;

/** A VS Code file or project manifest and whether it exists. */
struct settings_file {
  tree_owner tree;
  bool exists = false;
};

/** jh_json_read_settings with an owned tree. */
settings_file read_settings(parse_location &where, const char *path,
                            size_t limit);

/** jh_schema_files_t released with its owner. */
struct schema_files : jh_schema_files_t {
  schema_files(parse_location &location, size_t limit) noexcept
      : jh_schema_files_t() {
    jh_schema_files_init(this, &location, limit);
  }
  schema_files(const schema_files &) = delete;
  schema_files &operator=(const schema_files &) = delete;
  ~schema_files() { jh_schema_files_destroy(this); }
  /** Load the root schema; its document stays first. */
  const jh_json_document_t &root(const char *path);
  size_t report_count() const { return jh_schema_files_report_count(this); }
  const jh_schema_report_t &report(size_t index) const {
    return *jh_schema_files_report(this, index);
  }
};

/** jh_schema_files_validate: HAL_OK or HAL_ECONFIG; any other outcome
 * throws.
 */
hal_status_t validate_with(schema_files &state,
                           const jh_json_document_t &instance);

/** A jh_feature_registry_t released with its owner. */
struct feature_registry : jh_feature_registry_t {
  feature_registry() noexcept : jh_feature_registry_t() {
    jh_feature_registry_init(this);
  }
  feature_registry(const feature_registry &) = delete;
  feature_registry &operator=(const feature_registry &) = delete;
  ~feature_registry() { jh_feature_registry_destroy(this); }
};

/** jh_feature_outputs_t released with its owner. */
struct feature_outputs : jh_feature_outputs_t {
  feature_outputs() noexcept : jh_feature_outputs_t() {}
  feature_outputs(const feature_outputs &) = delete;
  feature_outputs &operator=(const feature_outputs &) = delete;
  ~feature_outputs() { jh_feature_outputs_destroy(this); }
};

/** jh_python_repr_append into a new string. */
owned_string python_repr(std::string_view text);

/** jh_write_generated: whether the file changed. */
bool write_generated(const char *path, std::string_view content);
/** jh_check_generated: whether the file is current. */
bool check_generated(const char *path, std::string_view content,
                     size_t diff_limit, std::string_view label);

} // namespace jh
#endif
