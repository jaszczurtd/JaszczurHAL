#ifndef JH_HARDWARE_INTERNAL_H
#define JH_HARDWARE_INTERNAL_H

#include "jh_board_outputs.h"
#include "jh_catalogue_internal.h"
#include "jh_device_defaults.h"
#include "jh_hardware.h"
#include "jh_hardware_build.h"
#include "jh_json_check.h"
#include "jh_project_build.h"
#include "jh_python_repr.h"

/* Values borrowed from immutable inputs or from completed construction
 * trees. Never retain a view while its owner is still being assembled. */
typedef struct {
  const char *source, *pointer;
} jh_hw_origin_t;

typedef struct {
  const char *name;
  jb_json_tree_view_t value;
  jh_hw_origin_t origin;
} jh_hw_property_t;

typedef struct {
  const char *name, *owner, *site, *net;
  jb_json_tree_view_t value, endpoint;
  jh_hw_origin_t origin;
  bool active;
} jh_hw_pin_t;

typedef struct jh_hw_node {
  const char *path;
  const jh_binding_t *binding;
  jb_json_tree_view_t type, entry, board_facts;
  jh_hw_origin_t origin;
  bool board, required, present, valid_properties;
  jb_vector_t properties; /* jh_hw_property_t */
  jb_vector_t pins;       /* jh_hw_pin_t */
  jb_json_tree_view_t transport;
} jh_hw_node_t;

typedef struct {
  const jh_binding_t *binding;
  jb_json_tree_view_t data;
} jh_hw_type_t;

typedef struct {
  const char *owner, *net;
  jb_json_tree_view_t endpoint;
  jh_hw_origin_t origin;
  bool board;
} jh_hw_claim_t;

/* Borrowed from immutable normalized endpoints; numeric IDs fit uint32. */
typedef struct {
  jb_json_span_t domain, owner, text;
  int64_t number;
  bool numeric;
} jh_hw_endpoint_key_t;

typedef struct {
  const char *name, *owner, *board_signal;
  jb_json_tree_view_t endpoint;
} jh_hw_net_t;

typedef struct {
  const jh_hardware_registry_t *registry;
  jh_failure_t *failure;
  jh_node_reader_t memory; /* Owned strings and the first fatal error. */
  jh_failure_t first;      /* Smallest semantic error in the current phase. */
  const char *first_source_key;
  size_t errors;
  jh_json_location_t where;
  jh_json_document_t document;
  jh_catalogue_t project_types;
  jb_vector_t trees;      /* Owned jb_json_tree_t pointers. */
  jb_vector_t types;      /* jh_hw_type_t */
  jb_vector_t nodes;      /* Owned jh_hw_node_t pointers, sorted by path. */
  jb_map_t claims;        /* jh_hw_endpoint_key_t -> first jh_hw_claim_t */
  jb_vector_t net_states; /* jh_hw_net_t */
  jb_map_t connections;   /* Signal spans -> module signal, owned strings. */
  jb_map_t module_owners; /* Module signal spans -> owning child signal. */
  const char *path, *base, *assembly_name;
  jb_json_tree_view_t data, assembly, clock, buses, nets, status_led;
  jb_json_tree_view_t capabilities, providers, model;
  const jh_board_descriptor_t *target, *board;
} jh_hw_t;

extern const jb_json_tree_limits_t jh_hw_tree_limits;
void jh_hw_init(jh_hw_t *h, const jh_hardware_registry_t *registry,
                jh_failure_t *failure);
const char *jh_hw_path(jh_hw_t *h, const char *base, const char *name);
void jh_hw_file_digest(jh_hw_t *h, const char *path, char out[65]);
jb_json_tree_view_t jh_hw_resolved_document(jh_hw_t *h);
jb_json_tree_view_t jh_hw_restore(jh_hw_t *h, const char *file,
                                  const char *header, const char *config_dir);
jb_json_tree_view_t jh_hw_assembly_board(jh_hw_t *h, jb_json_tree_view_t model,
                                         jb_vector_t *definitions,
                                         jb_vector_t *components);
bool jh_hw_owned(const char *name);
jb_json_tree_view_t jh_hw_compiler_abi(jh_hw_t *h, const char *compiler,
                                       const jb_vector_t *flags,
                                       jb_json_tree_view_t *layout);
void jh_hw_abi_checks(jh_hw_t *h, jb_json_tree_view_t abi, jb_string_t *out);
void jh_hw_layout_checks(jh_hw_t *h, jb_json_tree_view_t layout,
                         jb_vector_t *lines);
jb_json_tree_view_t jh_hw_program(jh_hw_t *h,
                                  const jh_hardware_build_options_t *options,
                                  jb_json_tree_view_t document,
                                  jh_project_build_t *program);

/* Construction trees belong to the context. Every mutation is checked;
 * later work becomes inert after a fatal error. */
jb_json_tree_t *jh_hw_object(jh_hw_t *h);
jb_json_tree_t *jh_hw_array(jh_hw_t *h);
/* Collect unique members in insertion order, then parse once. This avoids
 * copying a growing immutable tree on every insertion. Always finish to free
 * the buffer, also after a context failure. Child views are copied on put. */
typedef struct {
  jb_string_t text;
  bool object, first;
} jh_hw_container_t;
void jh_hw_container_init(jh_hw_t *h, jh_hw_container_t *out, bool object);
void jh_hw_container_put(jh_hw_t *h, jh_hw_container_t *out, jb_json_span_t key,
                         jb_json_tree_view_t value);
jb_json_tree_view_t jh_hw_container_finish(jh_hw_t *h, jh_hw_container_t *out);
jb_json_tree_t *jh_hw_copy(jh_hw_t *h, jb_json_tree_view_t value);
jb_json_tree_t *jh_hw_parse_tree(jh_hw_t *h, jb_json_span_t text);
jb_json_tree_view_t jh_hw_root(const jb_json_tree_t *tree);
jb_json_tree_view_t jh_hw_import(jh_hw_t *h, jb_json_node_t node);
jb_json_tree_view_t jh_hw_string(jh_hw_t *h, jb_json_span_t text);
jb_json_tree_view_t jh_hw_integer(jh_hw_t *h, int64_t number);
jb_json_tree_view_t jh_hw_boolean(jh_hw_t *h, bool value);
void jh_hw_set(jh_hw_t *h, jb_json_tree_t *tree, const char *key,
               jb_json_tree_view_t value);
void jh_hw_append(jh_hw_t *h, jb_json_tree_t *tree, jb_json_tree_view_t value);
hal_status_t jh_hw_format(jb_json_tree_view_t value, unsigned indent,
                          jb_string_t *out);
jb_json_node_t jh_hw_document(jh_hw_t *h, jb_json_tree_view_t value,
                              jh_json_document_t *document);
const char *jh_hw_text(jh_hw_t *h, jb_json_tree_view_t value);
const char *jh_hw_key(jh_hw_t *h, jb_json_tree_view_t value);
const char *jh_hw_repr(jh_hw_t *h, jb_json_tree_view_t value);
const char *jh_hw_join(jh_hw_t *h, const jb_json_span_t *parts, size_t count);
const char *jh_hw_pointer(jh_hw_t *h, const char *base, const char *key);
const char *jh_hw_token(jh_hw_t *h, const char *path);
bool jh_hw_is(jb_json_tree_view_t value, const char *text);
bool jh_hw_bool(jb_json_tree_view_t value);
bool jh_hw_i64(jh_hw_t *h, jb_json_tree_view_t value, int64_t *out);
const char *jh_hw_literal(jh_hw_t *h, const char *type,
                          jb_json_tree_view_t value);
jb_json_tree_view_t jh_hw_serialized(jh_hw_t *h, jb_json_tree_view_t value,
                                     bool hash);
void jh_hw_digest(jh_hw_t *h, jb_json_tree_view_t model, char out[65]);
void jh_hw_render_header(jh_hw_t *h, jb_json_tree_view_t model,
                         const char *digest, jb_string_t *out);
typedef struct {
  const char *c, *cpp, *comment;
} jh_hw_initializer_t;
const char *jh_hw_renderer(const char *compatible);
typedef struct {
  const char *name, *config_type, *clock_field;
  int64_t fixed_frequency;
  size_t bus_lines;
} jh_hw_renderer_t;
const jh_hw_renderer_t *jh_hw_renderer_info(const char *name);
const char *jh_hw_transport_problem(jh_hw_t *h, const char *renderer,
                                    int64_t rate, int64_t mode,
                                    jb_json_tree_view_t order, size_t *field);
const char *jh_hw_uart_problem(jh_hw_t *h, const char *target,
                               jb_json_tree_view_t frame);
jh_hw_initializer_t jh_hw_initializer(jh_hw_t *h, const char *path,
                                      jb_json_tree_view_t node,
                                      jb_json_tree_view_t hardware);
jh_hw_initializer_t jh_hw_spi_initializer(jh_hw_t *h,
                                          jb_json_tree_view_t settings);
const char *jh_hw_uart_frame(jh_hw_t *h, jb_json_tree_view_t frame);
unsigned jh_hw_parity(jb_json_tree_view_t parity);
bool jh_hw_equal(jh_hw_t *h, jb_json_tree_view_t a, jb_json_tree_view_t b);
/* Sorted object keys; initialize/destroy the list at the call site. */
void jh_hw_keys(jh_hw_t *h, jb_json_tree_view_t object, jb_vector_t *keys);
const char *jh_hw_source_key(jh_hw_t *h, const char *path);
const char *jh_hw_definition_name(jh_hw_t *h, jb_json_span_t definition);
void jh_hw_error(jh_hw_t *h, const char *code, jh_hw_origin_t origin,
                 const char *related, const jb_json_span_t *parts,
                 size_t count);
void jh_hw_record(jh_hw_t *h, const jh_failure_t *failure);
hal_status_t jh_hw_phase(jh_hw_t *h);
const jh_hw_type_t *jh_hw_type(jh_hw_t *h, const char *compatible);
jh_hw_node_t *jh_hw_node(jh_hw_t *h, const char *path);
jh_hw_property_t *jh_hw_property(jh_hw_node_t *node, const char *name);
jh_hw_pin_t *jh_hw_pin(jh_hw_node_t *node, const char *name);
const char *jh_hw_node_pointer(jh_hw_t *h, const char *path);
jh_hw_node_t *jh_hw_parent(jh_hw_t *h, const char *path, const char **child);
void jh_hw_declarations(jh_hw_t *h);
void jh_hw_presence(jh_hw_t *h);
void jh_hw_properties(jh_hw_t *h);
void jh_hw_signals(jh_hw_t *h);
bool jh_hw_condition(jh_hw_t *h, jh_hw_node_t *node,
                     jb_json_tree_view_t signal);
void jh_hw_clock(jh_hw_t *h);
void jh_hw_wiring(jh_hw_t *h);
void jh_hw_transport(jh_hw_t *h);
bool jh_hw_endpoint_equal(jh_hw_t *h, jb_json_tree_view_t a,
                          jb_json_tree_view_t b);
int jh_hw_endpoint_order(const void *left, const void *right, void *context);
bool jh_hw_list_has(jb_json_tree_view_t list, const char *text);
const char *jh_hw_value_text(jh_hw_t *h, jb_json_tree_view_t value);
const char *jh_hw_assembly_pointer(jh_hw_t *h, const char *section,
                                   const char *name);
typedef struct {
  const char *kind;
  const char *const *required, *const *optional;
  size_t required_count, optional_count;
} jh_hw_bus_pins_t;
const jh_hw_bus_pins_t *jh_hw_bus_pins(const char *kind);
void jh_hw_status_led(jh_hw_t *h);
void jh_hw_providers(jh_hw_t *h);
void jh_hw_build_model(jh_hw_t *h);
bool jh_hw_complete(jh_hw_t *h, const jh_hw_node_t *node);

#endif
