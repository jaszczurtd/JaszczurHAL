#ifndef JH_FEATURE_REGISTRY_H
#define JH_FEATURE_REGISTRY_H

/** @file jh_feature_registry.h
 * The HAL feature registry: the symbols of config/features/<domain>.json,
 * validated with the managed dependencies of config/features.schema.json and
 * the rules beyond the schema (relations, implies cycles, conflicts), the
 * generated header and CMake data of the feature graph, and the resolution of
 * the features one build requests. Messages keep the
 * wording of generate_hal_features.py; every registry failure has exit code 2
 * and memory exhaustion 4. C11.
 */
#include "jh_failure.h"
#include <jb/string.h>
#include <jb/vector.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One registered symbol. Every list holds jb_string_t elements in code point
 * order.
 */
typedef struct {
  jb_string_t name, domain, note;
  bool derived;  /**< kind "derived"; otherwise "feature". */
  bool has_note; /**< note holds the stripped note when set. */
  jb_vector_t implies, requirements, conflicts;
  jb_vector_t feature_sources, portable_sources, dependencies;
  size_t owner; /**< Index in fragments of the declaring file. */
} jh_feature_t;

/** A validated registry, its features in code point order of their names. */
typedef struct {
  jb_vector_t features;   /**< jh_feature_t elements. */
  jb_vector_t fragments;  /**< jb_string_t paths of the files read. */
  char digest[65];        /**< SHA-256 of the normalized registry, hex. */
  char schema_digest[65]; /**< SHA-256 of the schema document, hex. */
} jh_feature_registry_t;

/** Start an empty registry; registry must be zero or destroyed. */
void jh_feature_registry_init(jh_feature_registry_t *registry);
/** Release a registry and reset it to zero. */
void jh_feature_registry_destroy(jh_feature_registry_t *registry);

/** Load and validate the registry below the absolute config_root, reading
 * each file up to limit bytes, into an empty registry.
 */
hal_status_t jh_feature_registry_load(jh_feature_registry_t *registry,
                                      const char *config_root, size_t limit,
                                      jh_failure_t *failure);

/** Replace indices, a vector of size_t, with the indices of the features the
 * feature at index implies directly or through other features, in name
 * order; the feature itself is not among them.
 */
hal_status_t jh_feature_registry_closure(const jh_feature_registry_t *registry,
                                         size_t index, jb_vector_t *indices);

/** One requested symbol, as a header or the command line defines it: value
 * holds the text after the name when has_value is set (a bare request has
 * none), and source names where it comes from in findings and provenance.
 * The spans are borrowed for the call.
 */
typedef struct {
  jb_json_span_t symbol, value, source;
  bool has_value;
} jh_feature_request_t;

/** The target whose required features a resolution adds: its id and the
 * entries of its requiredFeatures, each SYMBOL or SYMBOL=1.
 */
typedef struct {
  jb_json_span_t id;
  const jb_json_span_t *required;
  size_t required_count;
} jh_feature_target_t;

/** The sources that request one symbol, in code point order. */
typedef struct {
  jb_string_t symbol;
  jb_vector_t sources; /**< jb_string_t elements. */
} jh_feature_provenance_t;

/** One resolved configuration. Every list holds jb_string_t elements. */
typedef struct {
  /** Symbols of the valid explicit requests, in code point order. */
  jb_vector_t requested;
  /** The valid requests, the target's and what they imply, same order. */
  jb_vector_t resolved;
  /** jh_feature_provenance_t of each valid request in code point order,
   * then of each required feature of the target that is not one. */
  jb_vector_t provenance;
  /** Messages in the order they are reported. */
  jb_vector_t findings;
  /** SHA-256 hex of the resolved names joined by LF. */
  char digest[65];
} jh_feature_resolution_t;

/** Start an empty resolution; resolution must be zero or destroyed. */
void jh_feature_resolution_init(jh_feature_resolution_t *resolution);
/** Release a resolution and reset it to zero. */
void jh_feature_resolution_destroy(jh_feature_resolution_t *resolution);

/** Resolve count requests against registry into an empty resolution, the way
 * resolve_target_feature_requests did. A symbol the registry lacks, a value
 * other than none or 1, a derived symbol, a symbol requested more than once,
 * a missing requirement or a conflict becomes a finding: request findings
 * start with the source (followed by :$ when it holds no colon) and
 * configuration findings with context. With a target, its
 * required features join the requests under the source
 * target:<id>:requiredFeatures[<index>] unless requested already, a request of
 * the HAL_DISABLE_ twin of one is a finding, and requested keeps only the
 * explicit requests. Findings are not failures: the call fails only for
 * missing arguments (HAL_EINVAL) and memory (exit code 4).
 */
hal_status_t jh_feature_resolve(const jh_feature_registry_t *registry,
                                const jh_feature_request_t *requests,
                                size_t count, jb_json_span_t context,
                                const jh_feature_target_t *target,
                                jh_feature_resolution_t *resolution,
                                jh_failure_t *failure);

/** Replace out with the generated header of the feature graph. */
hal_status_t jh_feature_registry_header(const jh_feature_registry_t *registry,
                                        jb_string_t *out);
/** Replace out with the generated CMake data and functions of the graph. */
hal_status_t jh_feature_registry_cmake(const jh_feature_registry_t *registry,
                                       jb_string_t *out);

/** The two generated artifacts of the registry, relative to an output root.
 */
typedef struct {
  jb_string_t header, cmake;
} jh_feature_outputs_t;

/** Read the relative paths of both artifacts from
 * <root>/config/tooling/artifacts.json (generatedArtifacts.featureHeader and
 * featureCmake) into outputs, which must be zero; release them with
 * jh_feature_outputs_destroy.
 */
hal_status_t jh_feature_outputs_read(const char *root,
                                     jh_feature_outputs_t *outputs,
                                     jh_failure_t *failure);
/** Release the paths and reset outputs to zero. */
void jh_feature_outputs_destroy(jh_feature_outputs_t *outputs);

/** Write both artifacts below output_root with mode 0644 (changed counts the
 * files written or given that mode), or compare them without changing
 * anything (changed counts the files that differ, current reports whether
 * both match; drift goes to stderr with a unified diff of at most 80 lines
 * each).
 */
hal_status_t jh_feature_registry_publish(const jh_feature_registry_t *registry,
                                         const jh_feature_outputs_t *outputs,
                                         const char *output_root, bool check,
                                         size_t *changed, bool *current,
                                         jh_failure_t *failure);

#ifdef __cplusplus
}
#endif
#endif
