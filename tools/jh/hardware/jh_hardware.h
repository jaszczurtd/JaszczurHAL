#ifndef JH_HARDWARE_H
#define JH_HARDWARE_H

/** @file jh_hardware.h
 * Project device-tree resolution. A context owns the shared registries and
 * the immutable inputs of one resolution; no operation writes project files.
 */
#include "jh_clock_registry.h"
#include "jh_type_catalogue.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Shared HAL registries, loaded once for a batch of project resolutions. */
typedef struct {
  jb_string_t root, schema_dir;
  jh_board_registry_t boards;
  jh_clock_registry_t clocks;
  jh_catalogue_t catalogue;
  jh_feature_registry_t
      feature_registry; /**< Owned, also used to validate boards. */
  jb_vector_t features, capabilities; /**< Owned string lists. */
  size_t max_bytes;
} jh_hardware_registry_t;

/** Start an empty registry; zero-initialize before calling. */
void jh_hardware_registry_init(jh_hardware_registry_t *registry);
/** Release a registry, including a partially loaded one. */
void jh_hardware_registry_destroy(jh_hardware_registry_t *registry);
/** Load the registries of an absolute HAL checkout, with bounded file reads.
 * On failure the registry remains destroyable; it must not be used. */
hal_status_t jh_hardware_registry_load(jh_hardware_registry_t *registry,
                                       const char *root, size_t max_bytes,
                                       jh_failure_t *failure);

/** One owned resolution. Initialize with zero; destroy after the last use.
 * registry must outlive this object. Internal state is opaque. */
typedef struct {
  void *state;
} jh_hardware_t;

/** Read and schema-check device_tree.json. Bindings and declarations are
 * checked by resolve; listing only needs the device-tree schema. */
hal_status_t jh_hardware_load(jh_hardware_t *hardware,
                              const jh_hardware_registry_t *registry,
                              const char *device_tree_path,
                              jh_failure_t *failure);
/** Release inputs and resolution state; a zero object is harmless. */
void jh_hardware_destroy(jh_hardware_t *hardware);
/** Write the sorted list of {name,target,board} assemblies into out. */
hal_status_t jh_hardware_list(const jh_hardware_t *hardware, jb_string_t *out,
                              jh_failure_t *failure);
/** Select from the schema-checked assembly declarations without resolving
 * bindings or loading a board registry. Append {name,target,board} as JSON.
 * NULL target/assembly selects automatically. Call once after load. */
hal_status_t jh_hardware_select(jh_hardware_t *hardware, const char *target,
                                const char *assembly, jb_string_t *out,
                                jh_failure_t *failure);
/** Select and resolve an assembly. NULL target/assembly selects automatically.
 * Call once after load. Failure leaves no usable model. */
hal_status_t jh_hardware_resolve(jh_hardware_t *hardware, const char *target,
                                 const char *assembly, jh_failure_t *failure);
/** Borrow the resolved {selection,hardware} model; zero when unresolved. */
jb_json_tree_view_t jh_hardware_model(const jh_hardware_t *hardware);
/** Append the resolved model as sorted, indented JSON with a trailing LF.
 * Float properties use their declared IEEE type and exact literal digits. */
hal_status_t jh_hardware_json(jh_hardware_t *hardware, jb_string_t *out,
                              jh_failure_t *failure);
/** Append the early jh_hardware.h, including its content identity and
 * deferred C11/C++17 driver initializers, without writing files. */
hal_status_t jh_hardware_header(jh_hardware_t *hardware, jb_string_t *out,
                                jh_failure_t *failure);
/** Append the resolved document with its schema version, hardware digest
 * and content digests of all inputs. No project or output files are changed. */
hal_status_t jh_hardware_document(jh_hardware_t *hardware, jb_string_t *out,
                                  jh_failure_t *failure);
/** Append the resolved board, header/compiler definitions and component
 * names as JSON. The assembly is already resolved; no program configuration
 * is read. The same values feed finalize and the ESP-IDF build adapter. */
hal_status_t jh_hardware_board_json(jh_hardware_t *hardware, jb_string_t *out,
                                    jh_failure_t *failure);

#ifdef __cplusplus
}
#endif
#endif
