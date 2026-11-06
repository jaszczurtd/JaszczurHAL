#ifndef JH_CLOCK_REGISTRY_H
#define JH_CLOCK_REGISTRY_H

/** @file jh_clock_registry.h
 * The closed clock-tree registry: config/hardware/clocks/<target>.json lists
 * the trees each target supports. The checks confirm that every target has
 * its registry, that each tree's output frequencies follow from its
 * parameters and its sources from the target's clockInputs, that every board
 * can run the default tree of each of its targets, and that the trees match
 * what actually sets the clocks: the STM32G474 startup headers, the pinned
 * Pico SDK defaults and the sdkconfig of an ESP-IDF build. Nothing in the HAL
 * build may override the Pico SDK clock defaults.
 *
 * Messages keep the wording of clock_registry.py. A check that fails records
 * HAL_ECONFIG with exit code 1, as the script exited; an unreadable file has
 * exit code 3, invalid JSON 2 and memory exhaustion 4. C11.
 */
#include "jh_board_registry.h"
#include "jh_failure.h"
#include <jb/json_tree.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One registry file: the target it names, the path it was read from and
 * the document as the hardware loader read it. */
typedef struct {
  jb_string_t target, path;
  jb_json_tree_t *tree;
  jb_json_tree_view_t root;
} jh_clock_file_t;

/** A loaded registry. */
typedef struct {
  jb_vector_t files; /**< jh_clock_file_t in target order. */
} jh_clock_registry_t;

/** One reason a board cannot run a tree: the JSON Pointer into the board's
 * clockSources and the message. */
typedef struct {
  jb_string_t pointer, message;
} jh_clock_source_error_t;

/** The board component that makes the STM32G474 startup code build a tree;
 * component is NULL for the tree it builds without one. */
typedef struct {
  const char *tree, *component;
} jh_clock_tree_component_t;

/** The trees the STM32G474 startup code builds, in name order. */
extern const jh_clock_tree_component_t jh_clock_stm32g474_trees[];
/** Number of entries of jh_clock_stm32g474_trees. */
extern const size_t jh_clock_stm32g474_tree_count;

/** Start an empty registry; registry must be zero or destroyed. */
void jh_clock_registry_init(jh_clock_registry_t *registry);
/** Release a registry and reset it to zero. */
void jh_clock_registry_destroy(jh_clock_registry_t *registry);

/** load_registry: read every *.json of clocks_dir (absolute) in file name
 * order, at most limit bytes each, with the hardware loader (unique names,
 * finite numbers that keep their magnitude in binary64), validate it with
 * config/hardware/clock_tree.schema.json of checkout_root and require the
 * file name <target>.json. The first failure ends the load: "[JH-HW-SCHEMA]
 * <path>:<pointer>: <message>" for a schema failure.
 */
hal_status_t jh_clock_registry_load(jh_clock_registry_t *registry,
                                    const char *checkout_root,
                                    const char *clocks_dir, size_t limit,
                                    jh_failure_t *failure);

/** The file of target, or NULL. */
const jh_clock_file_t *
jh_clock_registry_find(const jh_clock_registry_t *registry,
                       jb_json_span_t target);

/** Number of trees of every file together. */
size_t jh_clock_registry_tree_count(const jh_clock_registry_t *registry);

/** check_registry: the registry has one file per target of boards, every
 * tree follows from its backend parameters (check_tree), and each required
 * source names a clock input of its target. */
hal_status_t jh_clock_check_registry(const jh_clock_registry_t *registry,
                                     const jh_board_registry_t *boards,
                                     jh_failure_t *failure);

/** source_errors: append a jh_clock_source_error_t to errors for each source
 * the tree object needs that the clockSources object sources does not provide
 * as required on a target with the clockInputs object inputs. sources may be
 * a zero view for a board without clockSources. errors must be an
 * initialized vector of jh_clock_source_error_t; release it with
 * jh_clock_source_errors_destroy.
 */
hal_status_t jh_clock_source_errors(jb_json_tree_view_t sources,
                                    jb_json_tree_view_t tree,
                                    jb_json_tree_view_t inputs,
                                    jb_vector_t *errors, jh_failure_t *failure);

/** Start an empty vector of jh_clock_source_error_t. */
hal_status_t jh_clock_source_errors_init(jb_vector_t *errors);
/** Release every error and the vector. */
void jh_clock_source_errors_destroy(jb_vector_t *errors);

/** check_boards: every board of boards can run the default tree of each of
 * its compatible targets. Requires a registry that passed
 * jh_clock_check_registry. */
hal_status_t jh_clock_check_boards(const jh_clock_registry_t *registry,
                                   const jh_board_registry_t *boards,
                                   jh_failure_t *failure);

/** Receives the source errors of one board, compatible target and tree of
 * jh_clock_board_sources; errors holds jh_clock_source_error_t elements and is
 * borrowed for the call. A status other than HAL_OK stops the walk. */
typedef hal_status_t (*jh_clock_board_sources_fn)(void *user,
                                                  jb_json_span_t board,
                                                  jb_json_span_t target,
                                                  jb_json_span_t tree,
                                                  const jb_vector_t *errors);

/** The source errors of every board of boards_root/profiles (file name order,
 * the board ID being the file stem), every compatible target with a file in
 * boards_root/targets and a registry, and every tree of that registry, in
 * file order. The files are read as json.loads reads them, at most limit
 * bytes each, without the board registry validation: jh board validate checks
 * them. */
hal_status_t jh_clock_board_sources(const jh_clock_registry_t *registry,
                                    const char *boards_root, size_t limit,
                                    jh_clock_board_sources_fn fn, void *user,
                                    jh_failure_t *failure);

/** check_stm32g474: the STM32G474 trees against stm32g474_clock.h and
 * stm32g474_clock_tree.h of port_dir, read up to limit bytes. */
hal_status_t jh_clock_check_stm32g474(const jh_clock_registry_t *registry,
                                      const char *port_dir, size_t limit,
                                      jh_failure_t *failure);

/** check_pico_sdk: the default RP trees against the Pico SDK source
 * directory sdk_src (third_party/pico-sdk/src). */
hal_status_t jh_clock_check_pico_sdk(const jh_clock_registry_t *registry,
                                     const char *sdk_src, size_t limit,
                                     jh_failure_t *failure);

/** check_no_hal_override: no .cmake, .txt, .h, .c or .cpp file below cmake,
 * link_libraries, src, config or examples of root defines a Pico SDK clock
 * macro. Directories are walked in name order without following directory
 * links, as Path.rglob of Python 3.12 does. */
hal_status_t jh_clock_check_no_hal_override(const char *root, size_t limit,
                                            jh_failure_t *failure);

/** check_esp_sdkconfig: CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ and CONFIG_XTAL_FREQ
 * of the sdkconfig at path against the default tree of target. */
hal_status_t jh_clock_check_esp_sdkconfig(const jh_clock_registry_t *registry,
                                          jb_json_span_t target,
                                          const char *path, size_t limit,
                                          jh_failure_t *failure);

#ifdef __cplusplus
}
#endif
#endif
