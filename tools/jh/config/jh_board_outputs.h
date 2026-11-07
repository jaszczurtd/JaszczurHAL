#ifndef JH_BOARD_OUTPUTS_H
#define JH_BOARD_OUTPUTS_H

/** @file jh_board_outputs.h
 * The artifacts of the board registry: the headers and CMake data checked
 * into the repository (jh generate boards) and the files one build
 * configures for a target and board (jh board config), with the text
 * generate_board_config.py wrote except for the generator named in their
 * first comment. Failures of the configuration have exit code 2, memory
 * exhaustion 4 and file access 3. C11.
 */
#include "jh_board_registry.h"
#include "jh_feature_registry.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The artifacts checked into the repository. */
enum {
  JH_BOARD_STATIC_REGISTRY,   /**< jh_board_registry.h */
  JH_BOARD_STATIC_FALLBACK,   /**< jh_board_fallback_config.h */
  JH_BOARD_STATIC_COMPONENTS, /**< jh_board_components_registry.cmake */
  JH_BOARD_STATIC_COUNT
};

/** The generatedArtifacts keys of config/tooling/artifacts.json, by
 * JH_BOARD_STATIC_* artifact. */
extern const char *const jh_board_static_keys[JH_BOARD_STATIC_COUNT];

/** Replace out with the text of artifact, one of JH_BOARD_STATIC_*, for
 * registry. The fallback header fails with exit code 2 for a board whose
 * compatible targets need different board definitions.
 */
hal_status_t jh_board_static_text(const jh_board_registry_t *registry,
                                  int artifact, jb_string_t *out,
                                  jh_failure_t *failure);

/** Write the three artifacts below output_root at the paths of the
 * artifacts.json of the checkout root (changed counts the files written), or
 * with check compare them without changing anything (changed counts the
 * files that differ, current reports whether all match; each drift goes to
 * stderr with a diff of at most 120 lines).
 */
hal_status_t jh_board_static_publish(const jh_board_registry_t *registry,
                                     const char *root, const char *output_root,
                                     bool check, size_t *changed, bool *current,
                                     jh_failure_t *failure);

/** validate_definitions of generate_board_config.py: every compile
 * definition, stripped, holds no generator expression ($<), and one that
 * names a feature symbol is SYMBOL or SYMBOL=1 (with an optional leading
 * -D); a definition that only contains HAL_ENABLE_ or HAL_DISABLE_ is
 * rejected too. definitions holds jb_string_t elements.
 */
hal_status_t jh_board_validate_definitions(const jb_vector_t *definitions,
                                           jh_failure_t *failure);

/** The values an assembly owns. A NULL list keeps the board's value: the
 * board definitions in the header and on the compiler command line, the
 * components of target and board. An empty contract_hash keeps the feature
 * hash in the link contract symbol. header_prelude lines follow the includes
 * of jh_board_config.h; generation_extra paths end generation.d as given
 * (with / separators on Windows). Every list holds jb_string_t elements.
 */
typedef struct {
  const jb_vector_t *header_definitions;
  const jb_vector_t *cmake_definitions;
  const jb_vector_t *components;
  jb_json_span_t contract_hash;
  const jb_vector_t *header_prelude;
  const jb_vector_t *generation_extra;
} jh_board_overrides_t;

/** One build configuration. board is the registry descriptor of the board or
 * the board an assembly resolved from it; requested holds the raw feature
 * requests (jb_string_t). checkout_root is the absolute HAL checkout whose
 * config directory the generation reads.
 */
typedef struct {
  const jh_board_registry_t *registry;
  const jh_feature_registry_t *features;
  jb_json_tree_view_t target, board;
  const jb_vector_t *requested;
  const char *checkout_root;
  const jh_board_overrides_t *overrides; /**< NULL: none. */
} jh_board_build_t;

/** The files of one build configuration. */
enum {
  JH_BOARD_OUTPUT_CMAKE,           /**< jh_board_config.cmake */
  JH_BOARD_OUTPUT_HEADER,          /**< jh_board_config.h */
  JH_BOARD_OUTPUT_LINK_HEADER,     /**< jh_link_contract.h */
  JH_BOARD_OUTPUT_LINK_DEFINITION, /**< jh_link_contract_definition.c */
  JH_BOARD_OUTPUT_LINK_REFERENCE,  /**< jh_link_contract_reference.c */
  JH_BOARD_OUTPUT_RESOLVED,        /**< jh_board_resolved.json */
  JH_BOARD_OUTPUT_DEPENDENCIES,    /**< generation.d */
  JH_BOARD_OUTPUT_COUNT
};

/** The file names of the JH_BOARD_OUTPUT_* files. */
extern const char *const jh_board_output_names[JH_BOARD_OUTPUT_COUNT];

/** The text of each JH_BOARD_OUTPUT_* file. */
typedef struct {
  jb_string_t content[JH_BOARD_OUTPUT_COUNT];
} jh_board_outputs_t;

/** Start empty outputs; outputs must be zero or destroyed. */
hal_status_t jh_board_outputs_init(jh_board_outputs_t *outputs);
/** Release the outputs and reset them to zero. */
void jh_board_outputs_destroy(jh_board_outputs_t *outputs);

/** build_outputs of generate_board_config.py: the text of every file of the
 * build into outputs. The feature requests resolve with the required
 * features of the target; a rejected request or definition fails with exit
 * code 2.
 */
hal_status_t jh_board_build_outputs(const jh_board_build_t *build,
                                    jh_board_outputs_t *outputs,
                                    jh_failure_t *failure);

/** The checks of a generation before its outputs: a definition of a
 * HAL_BOARD_PROFILE_* or HAL_CYW43_PROFILE_* selector other than the
 * selector or a legacy selector of board ("compile definition <repr>
 * conflicts with resolved board <repr>").
 */
hal_status_t jh_board_check_selectors(jb_json_tree_view_t board,
                                      const jb_vector_t *definitions,
                                      jh_failure_t *failure);

/** require_managed_output: out receives the resolved output_dir, which must
 * lie below the managed root: output_root when not NULL, else
 * <repository_root>/.build. The managed root must have a .build component or
 * lie below the runtime-managed build root of JH_MANAGED_BUILD_ROOT. The
 * paths resolve with jh_python_resolve, a relative one against the working
 * directory, so a link resolves before a ".." that follows it.
 */
hal_status_t jh_board_managed_output(const char *output_dir,
                                     const char *output_root,
                                     const char *repository_root,
                                     jb_string_t *out, jh_failure_t *failure);

/** Write the outputs into the directory output_dir, created with its parents
 * when missing, after removing a jh_board_registry.h an older generator left
 * there; changed counts the files written.
 */
hal_status_t jh_board_write_outputs(const jh_board_outputs_t *outputs,
                                    const char *output_dir, size_t *changed,
                                    jh_failure_t *failure);

/** One jh board config request. definitions and features hold the raw
 * --define and --feature values (jb_string_t); board empty selects the
 * default board of the target. output_dir, output_root and assembly are
 * absolute paths or NULL; without output_dir the outputs are only returned,
 * which output_optional must allow. assembly names a JSON object with the
 * values an assembly owns: "board" (the descriptor it resolved),
 * "headerDefinitions", "cmakeDefinitions", "components", "headerPrelude",
 * "generationExtra" (arrays of strings) and "contractHash" (a string).
 */
typedef struct {
  const jh_board_registry_t *registry;
  const char *checkout_root;
  jb_json_span_t target, board;
  const jb_vector_t *definitions, *features;
  const char *output_dir, *output_root, *assembly;
  bool output_optional;
  size_t limit; /**< Bytes of each file read. */
} jh_board_config_request_t;

/** The result of a request: the selected target and board, the directory
 * written (empty without one), the files that changed there and the outputs.
 */
typedef struct {
  jb_string_t target, board, output_dir;
  size_t changed;
  jh_board_outputs_t outputs;
} jh_board_config_result_t;

/** Start an empty result; result must be zero or destroyed. */
hal_status_t jh_board_config_result_init(jh_board_config_result_t *result);
/** Release a result and reset it to zero. */
void jh_board_config_result_destroy(jh_board_config_result_t *result);

/** The generation of generate_board_config.py: validate the feature requests
 * and definitions, select the target and board ("generation requires
 * --target and --output-dir", "unknown target <repr>", "unknown board
 * <repr>", "board <repr> is not compatible with target <repr>"), check the
 * selectors and the output directory, read the assembly, then build the
 * outputs and write them when output_dir is set.
 */
hal_status_t jh_board_configure(const jh_board_config_request_t *request,
                                jh_board_config_result_t *result,
                                jh_failure_t *failure);

#ifdef __cplusplus
}
#endif
#endif
