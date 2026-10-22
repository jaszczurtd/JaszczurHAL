#ifndef JH_JSON_FILE_H
#define JH_JSON_FILE_H

/** @file jh_json_file.h
 * JSON files for the jh host commands: bounded reads, parsed documents that
 * keep their bytes alive, lexical path resolution and the schema document
 * loader shared by every command that validates. C++17; exceptions stay
 * inside the tool and never cross the jhbase C API.
 */
#include "jh_json_schema.h"
#include <jb/file.h>
#include <memory>
#include <string>
#include <vector>

namespace jh {

/** A failed operation: status, process exit code and a human message. Hardware
 * catalogue failures also carry their code, source file and JSON Pointer.
 */
struct failure {
  hal_status_t status;
  int exit_code;
  std::string message;
  std::string code, source, pointer;
};

/** Throw failure unless status is HAL_OK. */
void require(hal_status_t status, int exit_code, const char *message);
jb_json_span_t span(const std::string &text);
jb_json_span_t span(const char *text);
std::string copied(jb_json_span_t text);
/** Resolve path lexically against an absolute base (jb_path_resolve). */
std::string absolute_path(const std::string &path, const std::string &base);
/** Percent-escaped identity of a file path for schema URIs. */
std::string schema_identity(const std::string &path);

/** Owner of bytes read by jb_file_read. */
struct file_owner {
  jb_file_data_t data{};
  file_owner() = default;
  file_owner(const file_owner &) = delete;
  file_owner &operator=(const file_owner &) = delete;
  ~file_owner() { jb_file_release(&data); }
};

/** Where the last parse failed; located is set only after a parse error. */
struct parse_location {
  std::string source;
  jb_json_error_t error{};
  size_t line = 0, column = 0;
  bool located = false;
};

/** A parsed file: its bytes, tokens, document, path and schema identity. */
struct json_document {
  file_owner input;
  std::vector<jb_json_token_t> tokens;
  jb_json_doc_t doc{};
  std::string uri;
  std::string identity;
};

/** Read and parse one file of at most limit bytes. With hardware_numbers the
 * hardware loader policy applies: a token with a fraction or exponent must
 * keep a finite, nonzero magnitude in binary64, or the read fails with
 * HAL_EPROTO at the token. Failures throw with exit code 2 (invalid JSON),
 * 3 (file) or 4 (limits).
 */
std::unique_ptr<json_document> read_document(parse_location &where,
                                             const std::string &path,
                                             size_t limit,
                                             bool hardware_numbers = false);

/** One validation failure as the CLI reports it. */
struct schema_report {
  std::string pointer, uri, rule, reason, message;
};

/** Schema documents loaded by reference and the reports of one validation. */
struct schema_files {
  parse_location &where;
  size_t max_bytes;
  size_t schema_bytes = 0;
  std::vector<std::unique_ptr<json_document>> documents;
  std::vector<schema_report> reports;
  schema_files(parse_location &location, size_t limit)
      : where(location), max_bytes(limit) {}
  /** Load the root schema; its document stays first in documents. */
  const json_document &root(const std::string &path);
};

/** jh_json_schema_load_fn over schema_files: local files only. */
hal_status_t load_schema(void *user, jb_json_span_t base_uri,
                         jb_json_span_t reference,
                         jh_json_schema_source_t *out);
/** jh_json_schema_report_fn appending to schema_files::reports. */
hal_status_t collect_report(void *user, const jh_json_schema_error_t *error);
/** The validation limits every jh command uses. */
const jh_json_schema_limits_t &schema_limits();
/** Validate instance against the root schema of state, replacing its reports.
 * Returns HAL_OK or HAL_ECONFIG; any other outcome throws. scratch is sized
 * on first use and reused afterwards.
 */
hal_status_t validate_with(schema_files &state, const json_document &instance,
                           std::vector<unsigned char> &scratch);

} // namespace jh
#endif
