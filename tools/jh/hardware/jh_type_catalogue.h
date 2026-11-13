#ifndef JH_TYPE_CATALOGUE_H
#define JH_TYPE_CATALOGUE_H

/** @file jh_type_catalogue.h
 * HAL binding documents and their semantic validation. C11. A catalogue owns
 * its documents at stable addresses; borrowed nodes and bindings live until
 * destruction. Failures retain the hardware diagnostic code and JSON Pointer.
 */
#include "jh_json_file.h"
#include <jb/literal.h>
#include <jb/map.h>

#ifdef __cplusplus
extern "C" {
#endif

/** JSON category represented by a binding's C type. */
typedef enum {
  JH_C_BOOLEAN,
  JH_C_INTEGER,
  JH_C_FLOATING,
  JH_C_STRING
} jh_c_kind_t;

/** A binding C type and its exact literal representation. */
typedef struct {
  const char *name;
  jh_c_kind_t category;
  bool is_signed;
  unsigned bits; /**< Integer/IEEE width, 1 for bool, 0 for string. */
  jb_c_literal_type_t literal;
} jh_c_type_t;

/** One owned binding. Its document and all nodes must stay at this address. */
typedef struct {
  jb_string_t compatible, path;
  const char *origin;
  jh_json_document_t document;
} jh_binding_t;

/** Bindings in sorted file order and an index by compatible. Initialize with
 * {0}, call init, then destroy. Do not copy or move a live catalogue. */
typedef struct {
  jb_vector_t bindings; /**< Owned jh_binding_t pointers. */
  jb_map_t
      index; /**< Borrowed compatible spans mapped to load-order indices. */
} jh_catalogue_t;

/** The named C type, or NULL for an unknown name. */
const jh_c_type_t *jh_c_type(jb_json_span_t name);
/** JSON schema type name for a C category. */
const char *jh_c_json_type(jh_c_kind_t kind);
/** Initialize an empty catalogue on the host heap. */
hal_status_t jh_catalogue_init(jh_catalogue_t *types);
/** Release all documents and containers; a zero catalogue is harmless. */
void jh_catalogue_destroy(jh_catalogue_t *types);
/** Binding in load order, or NULL past the end. */
const jh_binding_t *jh_catalogue_at(const jh_catalogue_t *types, size_t index);
/** Binding by compatible, or NULL when absent. */
const jh_binding_t *jh_catalogue_find(const jh_catalogue_t *types,
                                      jb_json_span_t compatible);
/** Read and schema-check the regular *.json files in bindings_dir, in filename
 * order. types must be initialized and empty. On failure destroy types to
 * release any bindings already read. All pointers are required. */
hal_status_t jh_catalogue_load(jh_catalogue_t *types, jh_json_location_t *where,
                               const char *schema_dir, const char *bindings_dir,
                               size_t max_bytes, jh_failure_t *failure);
/** Check C types, units, values, children, signals, connections and cycles.
 * features/capabilities are optional lists of jb_string_t (jh_list_init);
 * NULL skips that list's validation. Other pointers are required. */
hal_status_t jh_catalogue_check(const jh_catalogue_t *types,
                                const jb_vector_t *features,
                                const jb_vector_t *capabilities,
                                jh_failure_t *failure);
/** Append the declared HAL feature names to an initialized string list. */
hal_status_t jh_catalogue_features(jh_json_location_t *where, const char *root,
                                   size_t max_bytes, jb_vector_t *names,
                                   jh_failure_t *failure);
/** Append the declared board capabilities to an initialized string list. */
hal_status_t jh_catalogue_capabilities(jh_json_location_t *where,
                                       const char *root, size_t max_bytes,
                                       jb_vector_t *names,
                                       jh_failure_t *failure);

#ifdef __cplusplus
}
#endif
#endif
