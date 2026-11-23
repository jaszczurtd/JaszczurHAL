#ifndef JH_LIBRARY_BUILD_H
#define JH_LIBRARY_BUILD_H

/** @file jh_library_build.h
 * Builds the linkable libJaszczurHAL.a of one registry target with its CMake
 * project under link_libraries/, as the family runners of scripts/ did:
 * the same output directory rules, managed components (scripts/ensure_*.sh),
 * CMake arguments, generated headers and checks. C11. */
#include "jh_board_registry.h"
#include "jh_failure.h"
#include <jb/cancel.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One library build. Paths are UTF-8; NULL marks an option the caller did
 * not give, so the target's provider applies its default. Relative paths
 * keep the provider's base: the HAL checkout for the Pico SDK options and
 * -p, the working directory for the STM32 toolchain file and -p. */
typedef struct {
  const char *root; /**< HAL checkout, absolute. */
  const char *base; /**< Absolute working directory of the command. */
  const char *target;
  const char *board, *assembly, *variant; /**< Empty when not given. */
  const char *project_config;             /**< -p; NULL when not given. */
  const jb_vector_t *defines;             /**< -D values, jb_string_t. */
  bool all_features, freertos, library_only, clean;
  const char *output; /**< Empty selects .build/static/<target>/<key>. */
  const char *jobs;   /**< Parallel jobs; NULL uses the online CPUs. */
  const char *sdk_dir, *toolchain, *picotool_dir, *picotool_build_dir;
  const char *example, *freertos_kernel;
  const char *idf_dir; /**< ESP-IDF checkout; NULL selects the pinned one. */
  bool log_to_stderr;  /**< Progress and child stdout go to stderr. */
  /** Polled while child processes run, with cancel_context; NULL disables
   * cancellation. A true result stops the child and fails with exit 130. */
  jb_cancel_fn canceled;
  void *cancel_context; /**< Passed unchanged to canceled; NULL allowed. */
} jh_library_build_options_t;

/** Where the build went. Release with jh_library_build_result_destroy. */
typedef struct {
  bool device_tree;      /**< A device tree selected the assembly path. */
  jb_string_t output;    /**< Resolved build directory. */
  jb_string_t library;   /**< Path of libJaszczurHAL.a. */
  jb_string_t selection; /**< "board <id>" or "assembly <name>". */
} jh_library_build_result_t;

void jh_library_build_result_init(jh_library_build_result_t *result);
void jh_library_build_result_destroy(jh_library_build_result_t *result);

/** Build options->target's library with registry, writing progress and the
 * child processes' output to stdout and stderr. Returns HAL_OK, or records
 * the failure with the exit code the runner gave: 2 for a board or assembly
 * that contradicts the device tree, the exit code of a failed child process,
 * otherwise 1 (4 for memory). */
hal_status_t jh_library_build(const jh_library_build_options_t *options,
                              const jh_board_registry_t *registry,
                              jh_library_build_result_t *result,
                              jh_failure_t *failure);

/** Install the library package of build_dir into prefix with
 * `cmake --install` (cmake of PATH), forwarding its output as
 * jh_library_build does, and require prefix/lib/libJaszczurHAL.a. A build
 * directory without CMake install rules fails with exit code 2. canceled
 * and cancel_context work as in jh_library_build_options_t. */
hal_status_t jh_library_install(const char *build_dir, const char *prefix,
                                const char *base, bool log_to_stderr,
                                jb_cancel_fn canceled, void *cancel_context,
                                jh_failure_t *failure);

/** The build directory key of a device tree library:
 * "<assembly or auto>/<16 hex digits>" of SHA-256 over the Path.resolve()
 * of config_dir in POSIX form, the assembly and the requests in sorted
 * order, joined by NUL. base resolves a relative config_dir. */
hal_status_t jh_library_key(const char *config_dir, const char *base,
                            const char *assembly, const jb_vector_t *requests,
                            jb_string_t *out, jh_failure_t *failure);

#ifdef __cplusplus
}
#endif
#endif
