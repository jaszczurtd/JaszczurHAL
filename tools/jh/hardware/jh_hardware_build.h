#ifndef JH_HARDWARE_BUILD_H
#define JH_HARDWARE_BUILD_H

/** @file jh_hardware_build.h
 * Finalize one resolved assembly with the program configuration and ABI.
 * All returned artifacts remain in memory until explicitly published. */
#include "jh_feature_registry.h"
#include "jh_hardware.h"

#ifdef __cplusplus
extern "C" {
#endif
/** Borrowed finalize inputs. Paths are absolute; variant and compiler may be
 * NULL. String lists contain jb_string_t entries and may be NULL. Registries
 * must be loaded from the same HAL checkout and outlive the call. */
typedef struct {
  const jh_hardware_registry_t *registry;
  const jh_feature_registry_t *features;
  const char *config_directory, *hardware_file, *header_file, *output_directory;
  const char *variant, *compiler;
  const jb_vector_t *definitions, *compiler_flags;
} jh_hardware_build_options_t;

/** Generated finalize artifacts. */
enum {
  JH_HARDWARE_BOARD_HEADER,
  JH_HARDWARE_CMAKE,
  JH_HARDWARE_PACKAGE,
  JH_HARDWARE_LINK_HEADER,
  JH_HARDWARE_LINK_DEFINITION,
  JH_HARDWARE_LINK_REFERENCE,
  JH_HARDWARE_CONFIG,
  JH_HARDWARE_DEPENDENCIES,
  JH_HARDWARE_OUTPUT_COUNT
};
/** File names in JH_HARDWARE_* artifact order. */
extern const char *const jh_hardware_output_names[JH_HARDWARE_OUTPUT_COUNT];
/** Owned texts, initialized with init and released with destroy. */
typedef struct {
  jb_string_t content[JH_HARDWARE_OUTPUT_COUNT];
} jh_hardware_outputs_t;
/** Initialize empty outputs without allocation. */
void jh_hardware_outputs_init(jh_hardware_outputs_t *outputs);
/** Release every output, including partial failures. */
void jh_hardware_outputs_destroy(jh_hardware_outputs_t *outputs);
/** Verify the resolved document, header and input digests, evaluate the
 * program and render all finalize outputs. outputs must be empty. Compiler
 * probes, when requested, run only in a temporary directory removed before
 * return. No project or build output is published. */
hal_status_t jh_hardware_finalize(const jh_hardware_build_options_t *options,
                                  jh_hardware_outputs_t *outputs,
                                  jh_failure_t *failure);
#ifdef __cplusplus
}
#endif
#endif
