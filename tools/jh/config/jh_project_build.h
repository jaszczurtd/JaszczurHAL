#ifndef JH_PROJECT_BUILD_H
#define JH_PROJECT_BUILD_H

/** @file jh_project_build.h
 * One target and variant of a project, and its CMake configuration. C11.
 * The header reader is supplied by the caller until its separate port to C.
 */
#include "jh_board_registry.h"
#include "jh_feature_registry.h"
#include "jh_json_file.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Read a header into the config.inspect JSON model. definitions contains
 * jb_string_t entries. Empty header/preinclude means none. On success *out
 * owns a tree; on failure it remains NULL and failure records the reason.
 * The callback must catch exceptions before returning through C.
 */
typedef hal_status_t (*jh_project_read_fn)(void *user, const char *header,
                                           const jb_vector_t *definitions,
                                           const char *preinclude,
                                           jb_json_tree_t **out,
                                           jh_failure_t *failure);

/** Borrowed inputs of one build. Empty header/preinclude means none. Relative
 * paths retain the caller's spelling in build diagnostics and request sources.
 * required and extra hold jb_string_t entries. The target facts come from
 * the registry or a caller of the transitional Python adapter.
 */
typedef struct {
  const char *header, *preinclude, *target, *selector, *variant;
  const jb_vector_t *required, *extra;
} jh_project_build_options_t;

/** Result of evaluation. Lists contain owned jb_string_t entries, except
 * requests, whose jh_feature_request_t spans borrow config, definitions and
 * source lists.
 * variant borrows declarations. Destroy the result after all consumers.
 */
typedef struct {
  jb_json_tree_t *declarations, *config;
  jb_json_tree_view_t declarations_root, config_root, variant;
  jb_vector_t definitions, effective_definitions, sources, requested, requests;
  jb_string_t label;
  bool rejected; /**< A header #error, as ConfigurationRejected in Python. */
} jh_project_build_t;

/** Initialize an empty result; no allocation. */
void jh_project_build_init(jh_project_build_t *build);
/** Release all result data, including a partially evaluated result. */
void jh_project_build_destroy(jh_project_build_t *build);
/** Publish CMake bytes atomically, retaining an identical file and existing
 * POSIX permissions. A new POSIX file uses 0666 restricted by process umask,
 * as Path.write_text() did. Call from the single-threaded CLI: POSIX umask
 * is sampled and restored. Other publication rules follow jh_write_generated.
 */
hal_status_t jh_project_build_write(const char *path, jb_json_span_t content,
                                    bool *changed, jh_failure_t *failure);
/** Evaluate declarations, select a variant, then evaluate the build and its
 * feature requests. Does not validate requests against the feature registry.
 * result must be empty and must be destroyed on success or failure. Invalid
 * configuration has exit code 2; a header #error also sets rejected.
 */
hal_status_t
jh_project_build_evaluate(const jh_project_build_options_t *options,
                          jh_project_read_fn read, void *user,
                          jh_project_build_t *result, jh_failure_t *failure);
/** Validate the evaluated requests through the shared feature resolver.
 * Findings are returned as one LF-separated configuration error, exit 2.
 */
hal_status_t jh_project_build_validate(
    const jh_project_build_options_t *options, const jh_project_build_t *build,
    const jh_feature_registry_t *registry, jh_failure_t *failure);
/** Render one evaluated build as CMake assignments with a generator banner.
 * targets is the registry's target descriptor list. out is initialized by
 * the caller. Values keep ordering, CMake bracket quoting and range markers.
 */
hal_status_t jh_project_build_cmake(const jh_project_build_options_t *options,
                                    const jh_project_build_t *build,
                                    const jb_vector_t *targets,
                                    jb_string_t *out, jh_failure_t *failure);

#ifdef __cplusplus
}
#endif
#endif
