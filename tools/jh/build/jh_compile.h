#ifndef JH_COMPILE_H
#define JH_COMPILE_H

/** @file jh_compile.h
 * The firmware build of a VS Code project (jh compile): the project model
 * jh-vscode load_config_for_action computes (manifest, settings, local
 * selection, device tree assembly, registry target profile, variant, command
 * line overrides and the HAL feature resolution), and the plan of the build
 * that follows from it: the CMake source and build trees, the artifact
 * directory, the configure and build commands with their environment, the
 * cache keys to forget and whether a stale cache is reset. ESP-IDF projects
 * get the jh esp build and artifacts commands. The model has the members and
 * values of `jh-vscode config-dump --json`. C11.
 */
#include "jh_failure.h"
#include <jb/json_tree.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One invocation. Paths are UTF-8; an empty string marks an option the
 * caller did not give. */
typedef struct {
  const char *root;    /**< HAL checkout, absolute; the checkout of jh. */
  const char *base;    /**< Absolute working directory of the command. */
  const char *project; /**< Firmware project directory; required. */
  const char *target, *board, *assembly, *variant;
  const char *port, *bootsel_volume;
  /** Interpreter for Python3_EXECUTABLE; empty selects JH_COMPONENT_PYTHON,
   * then python3 (POSIX) or python (Windows) from PATH. */
  const char *python;
  /** The jh executable the configure verifies (JH_EXECUTABLE). */
  const char *jh;
  bool verbose, debug;
} jh_compile_options_t;

/** The model and the plan of one build. Release with jh_compile_destroy. */
typedef struct {
  jb_json_tree_t *config; /**< The config-dump object. */
  jb_json_tree_t *plan;   /**< The plan object (jh_compile_plan). */
} jh_compile_t;

void jh_compile_init(jh_compile_t *compile);
void jh_compile_destroy(jh_compile_t *compile);

/** Compute the model and the plan without starting processes or changing
 * files. A configuration error fails with exit code 2 and the message
 * jh-vscode prints after "error: "; memory exhaustion has exit code 4.
 * compile must be empty. */
hal_status_t jh_compile_plan(const jh_compile_options_t *options,
                             jh_compile_t *compile, jh_failure_t *failure);

#ifdef __cplusplus
}
#endif
#endif
