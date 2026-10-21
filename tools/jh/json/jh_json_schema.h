#ifndef JH_JSON_SCHEMA_H
#define JH_JSON_SCHEMA_H

/** @file jh_json_schema.h
 * Host validator for the hardware schema subset. Uses immutable jb documents,
 * caller-owned scratch and finite budgets; never reads files or accesses a
 * network.
 */
#include <jb/json.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Immutable document and UTF-8 identity without a fragment. Both stay alive
 * through validation. A loader must return the same identity/document each
 * time.
 */
typedef struct {
  jb_json_span_t uri;
  const jb_json_doc_t *doc;
} jh_json_schema_source_t;

/** Resolve the document part of a reference against base_uri. Fragment handling
 * belongs to the validator. HAL errors propagate; HAL_NONE is invalid.
 */
typedef hal_status_t (*jh_json_schema_load_fn)(
    void *user, jb_json_span_t base_uri, jb_json_span_t document_reference,
    jh_json_schema_source_t *out_source);

/** Ordered failure. Text and node references are borrowed during the callback.
 * instance_pointer may identify a missing field; schema_rule is the actual
 * keyword value after resolving references, or a boolean schema itself.
 */
typedef struct {
  jb_json_span_t instance_pointer, schema_uri, schema_pointer;
  jb_json_node_t schema_rule;
  jb_json_span_t reason_code, message;
} jh_json_schema_error_t;

/** HAL_OK continues reporting; a HAL error stops and propagates unchanged. */
typedef hal_status_t (*jh_json_schema_report_fn)(
    void *user, const jh_json_schema_error_t *error);

/** Finite allowances. Zero permits no work/storage, never unlimited use.
 * max_ref_depth bounds all nested schema evaluation, including $ref (at most
 * 128). max_schema_nodes counts unique loaded documents, including unused
 * $defs. max_value_bytes bounds any decoded string/key or number used by
 * validation. Regex memory is a reusable arena, not an allocation from the
 * process heap.
 */
typedef struct {
  size_t max_work, max_ref_depth, max_reports, max_pointer_bytes;
  size_t max_uri_bytes, max_report_bytes, max_schema_nodes, max_instance_nodes;
  size_t max_value_bytes, max_pattern_bytes, max_regex_bytes;
  uint32_t regex_match_limit, regex_depth_limit, regex_heap_limit_kib;
} jh_json_schema_limits_t;

/** Conservative byte scratch size with checked arithmetic. Unaligned scratch
 * is allowed. Required arguments; output changes only on HAL_OK.
 */
hal_status_t
jh_json_schema_workspace_size(const jh_json_schema_limits_t *limits,
                              size_t *out_bytes);

/** Validate without mutating input or applying defaults. Required instance,
 * schema, limits and sufficient nonoverlapping scratch. Optional load/report.
 * HAL_ECONFIG: invalid instance; HAL_EUNSUPPORTED: unsupported schema/regex;
 * HAL_EINVAL: bad arguments or malformed schema; HAL_EOVERFLOW: finite limit or
 * insufficient scratch. No callbacks occur before validation and report
 * ordering complete. Callback errors propagate; HAL_NONE/unknown codes give
 * HAL_EINTERNAL. Structural equality compares exact decimals, distinguishes
 * bool from number, and ignores object member order. Reports sort pointers
 * numerically for decimal tokens and otherwise by Unicode code point,
 * preserving ties.
 */
hal_status_t jh_json_schema_validate(
    const jb_json_doc_t *instance, const jh_json_schema_source_t *schema,
    jh_json_schema_load_fn load, void *load_user,
    jh_json_schema_report_fn report, void *report_user,
    jb_json_workspace_t work, const jh_json_schema_limits_t *limits);

#ifdef __cplusplus
}
#endif
#endif
