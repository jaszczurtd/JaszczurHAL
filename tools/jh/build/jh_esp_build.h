#ifndef JH_ESP_BUILD_H
#define JH_ESP_BUILD_H

/** @file jh_esp_build.h
 * The ESP-IDF backend: the build and artifacts actions of
 * scripts/build_esp_idf.py in C, with the same generated inputs
 * (generated/jaszczurhal/, sdkconfig.defaults and its stamp,
 * jh_esp_idf_project.{cmake,json}), toolchain provenance, artifact manifest,
 * stage names, failure diagnostic and messages. The component tables come
 * from config/tooling/esp_idf_components.json, which the Python runner reads
 * too. The flash action stays in scripts/build_esp_idf.py and accepts a build
 * directory this backend produced. C11.
 */
#include "jh_failure.h"
#include <jb/cancel.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The actions of the backend. */
typedef enum { JH_ESP_ACTION_BUILD, JH_ESP_ACTION_ARTIFACTS } jh_esp_action_t;

/** One invocation, as the options of build_esp_idf.py. Paths are UTF-8; an
 * empty string marks an option the caller did not give. Relative project,
 * output, source, include directory and link library paths resolve against
 * the project directory as the runner did; a relative project or project
 * configuration directory resolves against base. */
typedef struct {
  const char *root; /**< HAL checkout (--repo-root), absolute. */
  const char *base; /**< Absolute working directory of the command. */
  jh_esp_action_t action;
  const char *project;               /**< Required. */
  const char *target;                /**< Empty selects esp32s3. */
  const char *board, *assembly;      /**< Empty when not given. */
  const char *name, *output;         /**< Empty when not given. */
  const char *variant, *idf_dir;     /**< Empty when not given. */
  const char *port;                  /**< Empty; any value is rejected. */
  const char *project_config;        /**< Empty selects the project. */
  const jb_vector_t *sources;        /**< --source, jb_string_t; required. */
  const jb_vector_t *include_dirs;   /**< --include-dir, jb_string_t. */
  const jb_vector_t *link_libraries; /**< --link-library, jb_string_t. */
  const jb_vector_t *defines;        /**< --define, jb_string_t. */
  bool clean, all_features;
  bool log_to_stderr;    /**< Progress and child stdout go to stderr. */
  jb_cancel_fn canceled; /**< Polled while child processes run. */
  void *cancel_context;
} jh_esp_options_t;

/** What a successful invocation verified. Release with
 * jh_esp_result_destroy. */
typedef struct {
  jb_string_t build_dir, target, board, assembly; /**< assembly may be empty. */
  bool device_tree;
  size_t flash_images;
} jh_esp_result_t;

void jh_esp_result_init(jh_esp_result_t *result);
void jh_esp_result_destroy(jh_esp_result_t *result);

/** Run one action, writing progress and the child processes' output to
 * stdout and stderr, and print "ESP-IDF verified: ..." on success. A failure
 * is recorded as "stage=<stage> failed: <message>" with exit code 1 (4 for
 * memory, 130 when canceled) and, once the build directory is known, also
 * written to its jh_esp_idf_failure.txt, as the runner did. */
hal_status_t jh_esp_run(const jh_esp_options_t *options,
                        jh_esp_result_t *result, jh_failure_t *failure);

#ifdef __cplusplus
}
#endif
#endif
