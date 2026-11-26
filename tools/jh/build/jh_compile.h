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
 * values of `jh-vscode config-dump --json`. jh_compile_configure then runs
 * the configure step of a CMake plan, and jh_compile_build the whole build
 * jh-vscode build runs. C11.
 */
#include "jh_failure.h"
#include <jb/cancel.h>
#include <jb/json_tree.h>
#include <jb/string.h>
#include <stdio.h>

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
  /** Stream of the echoed commands, CMake's output and the memory map
   * overview of jh_compile_configure and jh_compile_build; NULL means
   * stdout. */
  FILE *log;
  /** Polled with cancel_context while a child process runs; true stops it
   * (exit code 130). NULL disables cancellation. */
  jb_cancel_fn canceled;
  void *cancel_context; /**< Passed unchanged to canceled; NULL allowed. */
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
 * jh-vscode prints after "error: "; a build jh-vscode does not implement (an
 * ESP-IDF debug build, an unknown toolchain) with exit code 8, its
 * EXIT_UNSUPPORTED; memory exhaustion has exit code 4. The plan member
 * preflight lists the messages that stop a build before CMake runs (a
 * skeleton target, STM32G474 network modules without the CYW43 gSPI/lwIP
 * profile); objdump names the program of the memory map overview when one
 * exists. compile must be empty. */
hal_status_t jh_compile_plan(const jh_compile_options_t *options,
                             jh_compile_t *compile, jh_failure_t *failure);

/** Run the configure step of a CMake plan the way jh-vscode
 * configure_cmake_project does, holding the exclusive lock of the plan's
 * lockFile (.jh-build.lock of the artifact directory, which is created); a
 * lock another process holds is announced on stderr and awaited, and the
 * model and plan in compile are then computed again (options as for
 * jh_compile_plan), since the other build may have changed the tree. A stale
 * cache (plan member resetCache) loses
 * CMakeCache.txt and CMakeFiles/ with a warning on stderr, or stops the step
 * when its tree lies outside the managed artifact roots. The configure
 * command then runs in base with the plan's configureEnvironment added to the
 * current environment; with the plan's verbose flag "+ <argv>" goes to log
 * first. A program named cmake writes both of its streams to log, any other
 * keeps stderr; streams that share one destination pass whole lines, so the
 * pieces of one message stay together. After a successful configure the
 * sorted cache keys go to
 * .jh-vscode-cache-keys.json of the CMake tree, as json.dumps(indent=2) and a
 * newline (CRLF on Windows). A plan of another toolchain fails with exit
 * code 2; a failed step has exit code 5, the build failure of jh-vscode,
 * memory exhaustion 4 and cancellation 130. */
hal_status_t jh_compile_configure(const jh_compile_options_t *options,
                                  jh_compile_t *compile, jh_failure_t *failure);

/** Run the build of a CMake plan the way jh-vscode command_build does, under
 * the lock of jh_compile_configure: remove the uploadable files of the
 * artifact directory (firmware.elf, .bin, .hex, .map, .uf2, .ota), stop on
 * the preflight messages, configure as jh_compile_configure, run the build
 * command with the plan's buildEnvironment (both streams to log; for the
 * diagnostics the end of whole lines of at most 250000 bytes stays in
 * memory), then copy every file of the plan's
 * artifacts list from the CMake tree to the artifact directory (firmware.map
 * may be firmware.elf.map), also after a build that changed nothing, and
 * remove a UF2 or OTA image the list leaves out. A failed build prints the
 * linker's region overflows and sections that do not fit as "JaszczurHAL
 * diagnostics:" on stderr, as do the preflight messages. Unless
 * JH_VSCODE_MEMORY_OVERVIEW is 0, false, no or off, the sections of
 * firmware.elf (plan member objdump, -h) follow on log; a missing or failing
 * objdump is a warning. Exit codes as jh_compile_configure; a project of
 * another toolchain fails with exit code 2. */
hal_status_t jh_compile_build(const jh_compile_options_t *options,
                              jh_compile_t *compile, jh_failure_t *failure);

/** Append "+ <argv>" and a newline, the echo of jh-vscode run_command with
 * verbose, for the string array argv of a plan command. A member that is not
 * a string adds an empty word. Returns HAL_OK or the allocation status. */
hal_status_t jh_compile_command_line(jb_json_tree_view_t argv,
                                     jb_string_t *out);

#ifdef __cplusplus
}
#endif
#endif
