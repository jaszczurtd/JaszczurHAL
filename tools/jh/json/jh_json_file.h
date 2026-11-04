#ifndef JH_JSON_FILE_H
#define JH_JSON_FILE_H

/** @file jh_json_file.h
 * JSON files for the jh host commands: bounded reads, parsed documents that
 * keep their bytes alive, VS Code files and project manifests, lexical path
 * resolution and the schema document loader shared by every command that
 * validates. C11. Each function records its failure in a caller-owned
 * jh_failure_t: exit code 2 for invalid JSON, 3 for files and 4 for limits
 * and memory.
 */
#include "jh_failure.h"
#include "jh_json_schema.h"
#include <jb/file.h>
#include <jb/json_tree.h>
#include <jb/vector.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Resolve path lexically against an absolute base (jb_path_resolve) and
 * replace out with the result. A failure has exit code 3.
 */
hal_status_t jh_absolute_path(const char *path, const char *base,
                              jb_string_t *out, jh_failure_t *failure);

/** Where the last read went and where its parse failed: source names the
 * file, located is set only after a parse error, with the jhbase error and
 * its one-based line and byte column.
 */
typedef struct {
  jb_string_t source;
  jb_json_error_t error;
  size_t line, column;
  bool located;
} jh_json_location_t;

/** Start an empty location on the host heap. where must be zero or
 * destroyed.
 */
void jh_json_location_init(jh_json_location_t *where);
/** Release the location and reset it to zero. */
void jh_json_location_destroy(jh_json_location_t *where);

/** A parsed file: its bytes, the jb_json_token_t elements of tokens, the
 * document over both, the path and its schema identity (the path with
 * percent-escaped bytes outside the unreserved set). The document borrows the
 * bytes and tokens, so a loaded document must not move.
 */
typedef struct {
  jb_file_data_t input;
  jb_vector_t tokens;
  jb_json_doc_t doc;
  jb_string_t uri, identity;
} jh_json_document_t;

/** Read and parse one file of at most limit bytes into out, which must be
 * zero; path may point into where->source. With hardware_numbers the hardware
 * loader policy applies: a token with a fraction or exponent must keep a
 * finite, nonzero magnitude in binary64, or the read fails with HAL_EPROTO at
 * the token. where records the path and, after a parse error, its place. On
 * failure out is released.
 */
hal_status_t jh_json_read_document(jh_json_location_t *where, const char *path,
                                   size_t limit, bool hardware_numbers,
                                   jh_json_document_t *out,
                                   jh_failure_t *failure);
/** Release a document and reset it to zero. A zero document is harmless. */
void jh_json_document_destroy(jh_json_document_t *document);

/** Read a file of at most limit bytes (path may point into where->source) as
 * load_json_file of the Python HAL tools reads VS Code files and project
 * manifests: comments outside strings,
 * the last value of a repeated name and NaN/Infinity are accepted, a missing
 * file reads as an empty object and the root must be an object. Numbers keep
 * their tokens. Unlike Python, a `\u` escape of an unpaired surrogate is
 * rejected, the parser limits apply (256 nested containers, 100000 members
 * each) and an integer keeps its token at any length, where int() of Python
 * 3.11+ refuses more than 4300 digits. A root of another kind fails with
 * HAL_ETYPE; a parse error locates the place in the file, comments included.
 * out receives the tree, destroyed with jb_json_tree_destroy, and exists
 * whether the file was there; on failure out is NULL.
 */
hal_status_t jh_json_read_settings(jh_json_location_t *where, const char *path,
                                   size_t limit, jb_json_tree_t **out,
                                   bool *exists, jh_failure_t *failure);

/** Read a file of at most limit bytes (path may point into where->source) as
 * json.loads reads it: the last value of a repeated name and NaN/Infinity are
 * accepted, comments are not, and a missing file fails. A value of any kind
 * may be the root. Numbers keep their tokens. out receives the tree,
 * destroyed with jb_json_tree_destroy; on failure out is NULL.
 */
hal_status_t jh_json_read_value(jh_json_location_t *where, const char *path,
                                size_t limit, jb_json_tree_t **out,
                                jh_failure_t *failure);

/** One validation failure as the CLI reports it: instance Pointer, schema
 * identity, schema Pointer of the rule, reason code and message.
 */
typedef struct {
  jb_string_t pointer, uri, rule, reason, message;
} jh_schema_report_t;

/** Schema documents loaded by reference and the reports of the last
 * validation. Only local files are loaded, at most 256 of them and 64 MiB in
 * all. A failed schema read records its place in where, which the files
 * borrow.
 */
typedef struct {
  jh_json_location_t *where;
  size_t max_bytes;      /**< Limit of one schema file. */
  size_t schema_bytes;   /**< Bytes of the loaded documents. */
  jb_vector_t documents; /**< jh_json_document_t pointers, the root first. */
  jb_vector_t reports;   /**< jh_schema_report_t of the last validation. */
  jb_vector_t scratch;   /**< Validator workspace, sized on first use. */
} jh_schema_files_t;

/** Start empty schema files reading through where. files must be zero or
 * destroyed; where must outlive them.
 */
void jh_schema_files_init(jh_schema_files_t *files, jh_json_location_t *where,
                          size_t max_bytes);
/** Release every document and report and reset files to zero. */
void jh_schema_files_destroy(jh_schema_files_t *files);
/** Load the root schema at path, which stays the first document. out may be
 * NULL; the document lives as long as files.
 */
hal_status_t jh_schema_files_root(jh_schema_files_t *files, const char *path,
                                  const jh_json_document_t **out,
                                  jh_failure_t *failure);
/** Number of reports of the last validation. */
size_t jh_schema_files_report_count(const jh_schema_files_t *files);
/** Report index of the last validation, or NULL past the end. */
const jh_schema_report_t *jh_schema_files_report(const jh_schema_files_t *files,
                                                 size_t index);
/** Validate instance against the root schema with the limits of every jh
 * command, replacing the reports. Returns HAL_OK for a valid instance and
 * HAL_ECONFIG for an invalid one; any other status is recorded in failure.
 */
hal_status_t jh_schema_files_validate(jh_schema_files_t *files,
                                      const jh_json_document_t *instance,
                                      jh_failure_t *failure);

#ifdef __cplusplus
}
#endif
#endif
