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
#include <jb/json_writer.h>
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

/** out becomes base joined with name (jb_fs_path_join). */
hal_status_t jh_path_join(const char *base, const char *name, jb_string_t *out);

/** Replace out with the lexical parent of path (jb_fs_path_parent).
 * path may point into out. Failure keeps out unchanged and has exit code 3,
 * or 4 for allocation failure. */
hal_status_t jh_path_parent(const char *path, jb_string_t *out,
                            jh_failure_t *failure);
/** Append Path.as_posix() of path: backslashes become slashes on Windows,
 * and remain filename characters elsewhere. path may point into out.
 * A failed append leaves out unchanged. */
hal_status_t jh_append_posix(jb_string_t *out, jb_json_span_t path);

/** sorted(directory.glob("*.json")): append the names ending in .json, in code
 * point order, to names (jb_string_t elements). A missing directory has none;
 * any other listing failure returns its status with names unsorted.
 * POSIX matches names before metadata or UTF-8 validation; unrelated names
 * cannot prevent discovery. Hidden .json names are included.
 */
hal_status_t jh_json_names(const char *directory, jb_vector_t *names);

/** One file jh_walk_files finds: its path, its path relative to the walked
 * root with the separator of the host, and its name. HAL_OK continues the
 * walk; any other status ends it and becomes its result.
 */
typedef hal_status_t (*jh_walk_fn)(void *user, const char *path,
                                   jb_json_span_t relative, const char *name);

/** Path.rglob("*") of directory in sorted order: for each name in code point
 * order, visit a regular file (also through a link) and descend into a
 * directory that is not a link; relative prefixes the relative paths. A
 * missing directory or one that is a file has nothing. A listing failure
 * records "<directory>: cannot list: <status>" with exit code 3 (4 for
 * allocation/overflow). Entry inspection errors also propagate and name
 * the path. Vanished entries and unresolvable link targets (including link
 * loops) are skipped; allocation/overflow while following a link remains
 * fatal and names the link path.
 */
hal_status_t jh_walk_files(const char *directory, jb_json_span_t relative,
                           jh_walk_fn visit, void *user, jh_failure_t *failure);

/** Path.resolve() of an existing root: its real path, else root itself, in
 * out. A Windows drive path loses the \\?\ prefix, as os.path.realpath
 * drops it. Fails only with the status of the filesystem or the string.
 */
hal_status_t jh_resolved_path(const char *root, jb_string_t *out);

/** Replace out with the working directory of the process (jb_fs_cwd). A
 * failure has exit code 3, memory 4.
 */
hal_status_t jh_working_directory(jb_string_t *out, jh_failure_t *failure);

/** Path(base, path).resolve(), as os.path.realpath(strict=False) computes it,
 * in out. A relative path joins base, or the process working directory when
 * base is NULL. On POSIX the components resolve from left to right: a
 * symbolic link becomes its target before a later ".." takes the parent, "."
 * drops out and a part that does not exist stays as written. Windows first
 * normalizes the joined path lexically, as ntpath.realpath does, and then
 * takes the real path of its longest existing prefix (jh_resolved_path). A
 * link that cannot be read or more than 40 links fail with exit code 3, memory
 * with 4.
 */
hal_status_t jh_python_resolve(const char *path, const char *base,
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
 * percent-escaped bytes outside the unreserved set). A document parsed from
 * memory keeps its bytes in text instead of input. The document borrows the
 * bytes and tokens, so a loaded document must not move.
 */
typedef struct {
  jb_file_data_t input;
  jb_string_t text;
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
/** Parse a copy of text, at most limit bytes, into out, which must be zero,
 * as the document uri names (its identity for schema references and the
 * source where records). On failure out is released.
 */
hal_status_t jh_json_parse_document(jh_json_location_t *where, const char *uri,
                                    jb_json_span_t text, size_t limit,
                                    jh_json_document_t *out,
                                    jh_failure_t *failure);
/** Release a document and reset it to zero. A zero document is harmless. */
void jh_json_document_destroy(jh_json_document_t *document);

/** Parse strict JSON text that jh composed itself into a tree with limits
 * sized by its length (nesting up to 8 containers, numbers up to 64 bytes).
 * *out must be NULL and receives the tree only on success; the status is
 * that of jb_json_tree_parse.
 */
hal_status_t jh_json_tree_from_text(jb_json_span_t text, jb_json_tree_t **out);

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

/** Write value, with every member and item, to writer: numbers keep their
 * tokens and objects their member order. Writer errors propagate.
 */
hal_status_t jh_json_write_tree(jb_json_writer_t *writer,
                                jb_json_tree_view_t value);
/** The type of a tree value, JB_JSON_NULL for a view jhbase rejects. */
jb_json_type_t jh_json_type(jb_json_tree_view_t value);
/** Whether object is an object with a member key, borrowed into out. */
bool jh_json_member(jb_json_tree_view_t object, const char *key,
                    jb_json_tree_view_t *out);
/** The member key of object, borrowed; a zero view (type JB_JSON_NULL, tree
 * NULL) when object is not an object or has no such member. */
jb_json_tree_view_t jh_json_field(jb_json_tree_view_t object, const char *key);

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
