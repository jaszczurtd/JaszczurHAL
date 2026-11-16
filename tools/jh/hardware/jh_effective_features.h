#ifndef JH_EFFECTIVE_FEATURES_H
#define JH_EFFECTIVE_FEATURES_H

/** @file jh_effective_features.h
 * Effective feature configurations of project manifests and standalone
 * headers. The batch shares the hardware and feature registries. C11. */
#include "jh_hardware.h"

#ifdef __cplusplus
extern "C" {
#endif
/** Evaluate the projects and standalone headers in inputs (sorted unique
 * absolute paths). roots determines the relative names used in the report.
 * Both lists contain jb_string_t entries; all inputs are borrowed. Findings
 * append to the caller's initialized string list and are not fatal errors.
 * out receives a complete JSON report only on success, with the matrix
 * digest and provenance. Temporary early headers are removed before return.
 * The caller keeps both registries alive throughout the call. */
hal_status_t jh_effective_features(const jh_hardware_registry_t *hardware,
                                   const jh_feature_registry_t *features,
                                   const jb_vector_t *roots,
                                   const jb_vector_t *inputs, jb_string_t *out,
                                   size_t *count, jb_vector_t *findings,
                                   jh_failure_t *failure);
#ifdef __cplusplus
}
#endif
#endif
