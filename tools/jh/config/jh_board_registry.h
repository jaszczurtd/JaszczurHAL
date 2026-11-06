#ifndef JH_BOARD_REGISTRY_H
#define JH_BOARD_REGISTRY_H

/** @file jh_board_registry.h
 * The board registry: boards/capabilities.json, the target descriptors
 * boards/targets/<id>.json and the board profiles boards/profiles/<id>.json,
 * validated with the component registry of
 * config/tooling/board_components.json, the HAL feature registry of config/ and
 * the clock input and source schemas of config/hardware. Messages keep the
 * wording of generate_board_config.py; every registry failure has exit code 2
 * and memory exhaustion 4. C11.
 */
#include "jh_failure.h"
#include <jb/json_tree.h>
#include <jb/json_writer.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One GPIO pin ID: an integer, or a string such as "PA5". */
typedef struct {
  bool text;        /**< A string ID in name; otherwise number. */
  int64_t number;   /**< The integer ID when text is not set. */
  jb_string_t name; /**< The string ID when text is set. */
} jh_board_pin_t;

/** One descriptor file as json.loads reads it, except that the tree holds
 * every USB ID (vid, pid) as an integer, also one written as a "0x" string. */
typedef struct {
  jb_string_t id, path;
  jb_json_tree_t *tree;
  jb_json_tree_view_t root;
  /** jh_board_pin_t of a target's $.gpio.validPins in pin order (integers
   * by value, then strings in code point order); empty for a board. */
  jb_vector_t valid_pins;
} jh_board_descriptor_t;

/** One entry of config/tooling/board_components.json. */
typedef struct {
  jb_string_t id, slot;
  jb_vector_t providers; /**< jb_string_t elements in file order. */
} jh_board_component_t;

/** A validated registry. */
typedef struct {
  jb_string_t boards_root; /**< The real path of the boards directory. */
  jb_vector_t targets;     /**< jh_board_descriptor_t in file name order. */
  jb_vector_t boards;      /**< jh_board_descriptor_t in file name order. */
  jb_json_tree_t *capabilities_tree;
  jb_json_tree_view_t capabilities; /**< The $.capabilities object. */
  jb_vector_t components;           /**< jh_board_component_t, file order. */
} jh_board_registry_t;

/** Start an empty registry; registry must be zero or destroyed. */
void jh_board_registry_init(jh_board_registry_t *registry);
/** Release a registry and reset it to zero. */
void jh_board_registry_destroy(jh_board_registry_t *registry);

/** Load and validate the registry of the boards directory boards_root with
 * the component, feature and hardware registries of the HAL checkout
 * checkout_root (both absolute), reading each file up to limit bytes, into an
 * empty registry. The first failure ends the load, as the DescriptorError of
 * generate_board_config.py did.
 */
hal_status_t jh_board_registry_load(jh_board_registry_t *registry,
                                    const char *checkout_root,
                                    const char *boards_root, size_t limit,
                                    jh_failure_t *failure);

/** The descriptor with id among the jh_board_descriptor_t elements of
 * descriptors, or NULL.
 */
const jh_board_descriptor_t *
jh_board_registry_find(const jb_vector_t *descriptors, jb_json_span_t id);

/** source_problems of clock_registry.py: problem receives the first reason
 * the clock source object source (kind, pins) cannot drive the clock input
 * key of a target with the clockInputs object inputs, or becomes empty when
 * it can: the target has no such input for the kind, or the source lists
 * other pin IDs than the kind occupies. Fails only with the status of the
 * string.
 */
hal_status_t jh_board_clock_source_problem(jb_json_tree_view_t inputs,
                                           jb_json_span_t key,
                                           jb_json_tree_view_t source,
                                           jb_string_t *problem);

/** Write the fixed tables the registry validates against as members of the
 * open JSON object of writer: "deviceRoles" (busKinds, macroPrefix, signals
 * with their mode, attributes with type and their min, values and
 * conditional when set, ordered pairs and requires rules, sets sorted),
 * "canControllers" (targets, instances, backend), "canTransceivers",
 * "canChannelSignals" and "singleEndpointDeviceKinds" (the optional fields
 * of each kind besides kind and endpoint).
 */
hal_status_t jh_board_write_tables(jb_json_writer_t *writer);

#ifdef __cplusplus
}
#endif
#endif
