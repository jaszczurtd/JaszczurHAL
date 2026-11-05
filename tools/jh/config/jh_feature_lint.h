#ifndef JH_FEATURE_LINT_H
#define JH_FEATURE_LINT_H

/** @file jh_feature_lint.h
 * The inputs of the feature lint and the findings of a project manifest, with
 * the rules and wording generate_hal_features.py --lint and jh-vscode used.
 * C11.
 */
#include "jh_failure.h"
#include <jb/json_tree.h>
#include <jb/vector.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Append to inputs (jb_string_t elements) the files the lint reads below
 * root, in no particular order. root is resolved through the filesystem
 * first.
 * A file root is its only input; below a directory every
 * hal_project_config.h and jaszczurhal.project.json counts, while
 * directories named .build, .git or third_party or starting with build are
 * skipped, and tests/hardware too unless include_hardware. A link to a
 * directory is not entered and a directory that cannot be listed is skipped,
 * as os.walk does. A root that is neither a file nor a directory fails with
 * HAL_ENOENT and exit code 2 ("<root>: lint input root does not exist").
 */
hal_status_t jh_feature_lint_inputs(const char *root, bool include_hardware,
                                    jb_vector_t *inputs, jh_failure_t *failure);

/** Append to findings (jb_string_t elements) the project configuration a
 * jaszczurhal.project.json carries although it belongs in
 * hal_project_config.h: the removed fields variants and example, and in
 * cmake.cache or targetProfiles.<target>.cmake.cache the key JH_VARIANT, the
 * keys EXTRA_HAL_DEFINES, JH_EXTRA_DEFINES and JH_PROJECT_SOURCES and HAL
 * feature symbols. Each finding starts with label and holds
 * [JH-CFG-MANIFEST]. Fails only for missing arguments and memory.
 */
hal_status_t jh_manifest_findings(jb_json_tree_view_t manifest,
                                  jb_json_span_t label, jb_vector_t *findings,
                                  jh_failure_t *failure);

#ifdef __cplusplus
}
#endif
#endif
