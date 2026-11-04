#ifndef JH_FEATURE_REGISTRY_H
#define JH_FEATURE_REGISTRY_H

/** @file jh_feature_registry.h
 * The HAL feature registry: the symbols of config/features/<domain>.json,
 * validated with the managed dependencies of config/features.schema.json and
 * the rules beyond the schema (relations, implies cycles, conflicts), and the
 * generated header and CMake data of the feature graph. Messages keep the
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
