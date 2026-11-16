#include "jh_hardware_internal.h"
#include <inttypes.h>
#include <jb/json_number.h>
#include <stdio.h>
#include <string.h>

void jh_hw_status_led(jh_hw_t *h) {
  const jb_json_tree_view_t selection = jh_json_field(h->assembly, "statusLed");
  const jb_json_tree_view_t selected = jh_json_field(selection, "node");
  const jh_hw_origin_t origin = {
      h->path, jh_hw_assembly_pointer(h, "statusLed", "node")};
  if (jh_json_type(selected) == JB_JSON_NULL) {
    if (strcmp(jb_string_data(&h->target->id), "mock") != 0)
      jh_hw_error(h, "JH-HW-STATUS-LED", origin, NULL,
                  JH_PARTS(jh_span("only the mock target has no status LED")));
    return;
  }
  const char *path = jh_hw_text(h, selected);
  jh_hw_node_t *node = jh_hw_node(h, path);
  if (node == NULL || !node->present) {
    jh_hw_error(h, "JH-HW-STATUS-LED", origin, NULL,
                JH_PARTS(jh_span(node == NULL ? "no node " : ""), jh_span(path),
                         jh_span(node == NULL ? "" : " is absent")));
    return;
  }
  const bool addressable = strcmp(jb_string_data(&node->binding->compatible),
                                  "jaszczurhal,addressable-led") == 0;
  const char *signal = addressable ? "data" : "out";
  const jh_hw_pin_t *pin = jh_hw_pin(node, signal);
  if (!jh_hw_is(jh_json_field(node->type, "domain"), "gpio") || pin == NULL) {
    jh_hw_error(h, "JH-HW-STATUS-LED", origin, NULL,
                JH_PARTS(jh_string_span(&node->binding->compatible),
                         jh_span(" is not an LED")));
    return;
  }
  jh_hw_property_t *property =
      jh_hw_property(node, addressable ? "pixelOrder" : "activeLow");
  if (property == NULL) {
    jh_hw_error(h, "JH-HW-STATUS-LED", origin, NULL,
                JH_PARTS(jh_string_span(&node->binding->compatible),
                         jh_span(" has no known polarity")));
    return;
  }
  if (jh_json_type(pin->endpoint) == JB_JSON_NULL) {
    jh_hw_error(h, "JH-HW-STATUS-LED", origin, NULL,
                JH_PARTS(jh_span(path), jh_span("."), jh_span(signal),
                         jh_span(" is not connected")));
    return;
  }
  const jb_json_tree_view_t order = jh_json_field(selection, "pixelOrder");
  if (jh_json_type(order) != JB_JSON_NULL) {
    const jb_json_tree_view_t allowed = jh_json_field(
        jh_json_field(node->board_facts, "pixelOrder"), "allowed");
    if (!addressable || (node->board && allowed.tree != NULL &&
                         !jh_hw_list_has(allowed, jh_hw_text(h, order)))) {
      jh_hw_error(h, "JH-HW-STATUS-LED",
                  (jh_hw_origin_t){h->path, jh_hw_assembly_pointer(
                                                h, "statusLed", "pixelOrder")},
                  NULL,
                  JH_PARTS(jh_span(path), jh_span(" takes no pixel order "),
                           jh_span(jh_hw_text(h, order))));
      return;
    }
    property->value = order;
    property->origin = (jh_hw_origin_t){
        h->path, jh_hw_assembly_pointer(h, "statusLed", "pixelOrder")};
  }
  jb_json_tree_t *model = jh_hw_object(h);
  jh_hw_set(h, model, "node", selected);
  jh_hw_set(h, model, "endpoint", pin->endpoint);
  jh_hw_set(h, model, "signal", jh_hw_string(h, jh_span(signal)));
  jh_hw_set(h, model, "kind",
            jh_hw_string(h, jh_span(addressable ? "addressable" : "gpio")));
  jh_hw_set(h, model, addressable ? "pixelOrder" : "activeLow",
            property->value);
  h->status_led = jh_hw_root(model);
}

bool jh_hw_complete(jh_hw_t *h, const jh_hw_node_t *node) {
  if (node == NULL || !node->present)
    return false;
  if (node->board)
    return true;
  const jb_json_tree_view_t signals = jh_json_field(node->type, "signals");
  for (size_t i = 0; i < jb_vector_size(&node->pins); ++i) {
    const jh_hw_pin_t *pin = jb_vector_const_at(&node->pins, i);
    const jb_json_tree_view_t spec = jh_json_field(signals, pin->name);
    if (jh_hw_bool(jh_json_field(spec, "required")) && pin->active &&
        jh_json_type(pin->endpoint) == JB_JSON_NULL)
      return false;
  }
  static const char *const required[] = {"bus", "controller"};
  for (size_t i = 0; i < COUNTOF(required); ++i) {
    const jb_json_tree_view_t spec = jh_json_field(node->type, required[i]);
    if (jh_hw_bool(jh_json_field(spec, "required")) &&
        jh_json_type(jh_json_field(node->entry, required[i])) == JB_JSON_NULL)
      return false;
  }
  if (jh_hw_is(jh_json_field(jh_json_field(node->type, "bus"), "kind"),
               "i2c") &&
      jh_json_type(jh_json_field(node->entry, "address")) == JB_JSON_NULL)
    return false;
  const size_t length = strlen(node->path);
  for (size_t i = 0; i < jb_vector_size(&h->nodes); ++i) {
    const jh_hw_node_t *child = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    const char *dot = strrchr(child->path, '.');
    if (dot != NULL && (size_t)(dot - child->path) == length &&
        memcmp(node->path, child->path, length) == 0 && !child->board &&
        child->present && child->required && !jh_hw_complete(h, child))
      return false;
  }
  return true;
}

void jh_hw_providers(jh_hw_t *h) {
  const jb_json_tree_view_t chosen = jh_json_field(h->assembly, "providers");
  const jb_json_tree_view_t board_caps =
      jh_json_field(h->board->root, "capabilities");
  for (size_t i = 0; h->memory.status == HAL_OK && i < jh_json_count(chosen);
       ++i) {
    const jb_json_tree_view_t choice = jh_json_item(chosen, i);
    const char *capability = jh_hw_key(h, choice);
    const char *path = jh_hw_text(h, choice);
    const jh_hw_node_t *node = jh_hw_node(h, path);
    const jh_hw_origin_t origin = {
        h->path, jh_hw_assembly_pointer(h, "providers", capability)};
    if (!jh_list_contains(&h->registry->capabilities, jh_span(capability)))
      jh_hw_error(
          h, "JH-HW-FEATURE", origin, NULL,
          JH_PARTS(jh_span("unknown capability "), jh_span(capability)));
    else if (strcmp(path, "board") == 0) {
      if (!jh_hw_bool(
              jh_json_field(jh_json_field(board_caps, capability), "present")))
        jh_hw_error(h, "JH-HW-FEATURE", origin, NULL,
                    JH_PARTS(jh_string_span(&h->board->id), jh_span(" has no "),
                             jh_span(capability)));
    } else if (node == NULL) {
      jh_hw_error(h, "JH-HW-FEATURE", origin, NULL,
                  JH_PARTS(jh_span("no node "), jh_span(path)));
    } else if (!jh_hw_list_has(jh_json_field(node->type, "provides"),
                               capability)) {
      jh_hw_error(h, "JH-HW-FEATURE", origin, NULL,
                  JH_PARTS(jh_string_span(&node->binding->compatible),
                           jh_span(" does not provide "), jh_span(capability)));
    }
  }
  if (h->errors != 0 || h->memory.status != HAL_OK)
    return;
  jb_json_tree_t *providers = jh_hw_object(h), *capabilities = jh_hw_object(h);
  for (size_t i = 0; h->memory.status == HAL_OK &&
                     i < jb_vector_size(&h->registry->capabilities);
       ++i) {
    const char *capability =
        jb_string_data(jh_list_at(&h->registry->capabilities, i));
    const jb_json_tree_view_t choice = jh_json_field(chosen, capability);
    const char *candidate = NULL;
    size_t candidates = 0;
    for (size_t j = 0; j < jb_vector_size(&h->nodes); ++j) {
      const jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, j);
      if (node->present && !node->board &&
          jh_hw_list_has(jh_json_field(node->type, "provides"), capability)) {
        ++candidates;
        candidate = node->path;
      }
    }
    if (jh_hw_bool(
            jh_json_field(jh_json_field(board_caps, capability), "present"))) {
      ++candidates;
      candidate = "board";
    }
    if (candidates != 1)
      candidate = NULL;
    if (choice.tree != NULL)
      candidate = jh_hw_text(h, choice);
    if (candidate != NULL)
      jh_hw_set(h, providers, capability, jh_hw_string(h, jh_span(candidate)));
    const bool usable =
        candidate != NULL && (strcmp(candidate, "board") == 0 ||
                              jh_hw_complete(h, jh_hw_node(h, candidate)));
    jh_hw_set(h, capabilities, capability, jh_hw_boolean(h, usable));
  }
  h->capabilities = jh_hw_root(capabilities);
  h->providers = jh_hw_root(providers);
}

static jb_json_tree_view_t exact_numbers(jh_hw_t *h, jb_json_tree_view_t value,
                                         bool preserve) {
  const jb_json_type_t kind = jh_json_type(value);
  if (kind == JB_JSON_NUMBER && !preserve) {
    int64_t integer = 0;
    if (jh_hw_i64(h, value, &integer))
      return jh_hw_integer(h, integer);
    jh_json_document_t document = {0};
    const jb_json_node_t number = jh_hw_document(h, value, &document);
    uint64_t large = 0;
    if (h->memory.status == HAL_OK &&
        jb_json_get_u64(number, &large) == HAL_OK) {
      char text[32];
      (void)snprintf(text, sizeof text, "%" PRIu64, large);
      jh_json_document_t canonical = {0};
      h->memory.status =
          jh_json_parse_document(&h->where, "<integer>", jh_span(text),
                                 sizeof text, &canonical, h->failure);
      jb_json_node_t root = {0};
      if (h->memory.status == HAL_OK)
        jh_node_storage(&h->memory, jb_json_root(&canonical.doc, &root));
      if (h->memory.status == HAL_OK)
        value = jh_hw_import(h, root);
      jh_json_document_destroy(&canonical);
    }
    jh_json_document_destroy(&document);
    return value;
  }
  if (kind != JB_JSON_OBJECT && kind != JB_JSON_ARRAY)
    return value;
  jh_hw_container_t out;
  jh_hw_container_init(h, &out, kind == JB_JSON_OBJECT);
  const jb_json_tree_view_t type = jh_json_field(value, "cType");
  const bool typed = jh_hw_is(type, "float") || jh_hw_is(type, "double") ||
                     jh_hw_is(type, "bool") || jh_hw_is(type, "string");
  for (size_t i = 0; h->memory.status == HAL_OK && i < jh_json_count(value);
       ++i) {
    const jb_json_tree_view_t item = jh_json_item(value, i);
    const jb_json_span_t key =
        kind == JB_JSON_OBJECT ? jh_json_key(item) : (jb_json_span_t){0};
    jh_hw_container_put(
        h, &out, key,
        exact_numbers(h, item, typed && jh_span_equal(key, jh_span("value"))));
  }
  return jh_hw_container_finish(h, &out);
}

static jb_json_tree_view_t model_node(jh_hw_t *h, const jh_hw_node_t *node) {
  jb_json_tree_t *item = jh_hw_object(h);
  jh_hw_set(h, item, "compatible",
            jh_hw_string(h, jh_string_span(&node->binding->compatible)));
  jh_hw_set(h, item, "domain", jh_json_field(node->type, "domain"));
  jh_hw_set(h, item, "present", jh_hw_boolean(h, node->present));
  if (node->present) {
    jb_json_tree_t *properties = jh_hw_object(h), *signals = jh_hw_object(h);
    const jb_json_tree_view_t definitions =
        jh_json_field(node->type, "properties");
    for (size_t i = 0; i < jb_vector_size(&node->properties); ++i) {
      const jh_hw_property_t *property =
          jb_vector_const_at(&node->properties, i);
      const jb_json_tree_view_t definition =
          jh_json_field(definitions, property->name);
      jb_json_tree_t *typed = jh_hw_object(h);
      jh_hw_set(h, typed, "value", property->value);
      jh_hw_set(h, typed, "cType", jh_json_field(definition, "cType"));
      const jb_json_tree_view_t unit = jh_json_field(definition, "unit");
      if (unit.tree != NULL)
        jh_hw_set(h, typed, "unit", unit);
      jh_hw_set(h, properties, property->name, jh_hw_root(typed));
    }
    for (size_t i = 0; i < jb_vector_size(&node->pins); ++i) {
      const jh_hw_pin_t *pin = jb_vector_const_at(&node->pins, i);
      jb_json_tree_t *signal = jh_hw_object(h);
      jh_hw_set(h, signal, "endpoint", pin->endpoint);
      jh_hw_set(h, signal, "owner",
                jh_json_type(pin->endpoint) == JB_JSON_NULL
                    ? (jb_json_tree_view_t){0}
                    : jh_hw_string(h, jh_span(pin->owner)));
      if (pin->net != NULL)
        jh_hw_set(h, signal, "net", jh_hw_string(h, jh_span(pin->net)));
      jh_hw_set(h, signals, pin->name, jh_hw_root(signal));
    }
    jh_hw_set(h, item, "properties", jh_hw_root(properties));
    jh_hw_set(h, item, "signals", jh_hw_root(signals));
    static const char *const copied[] = {"bus", "controller", "address"};
    for (size_t i = 0; i < COUNTOF(copied); ++i) {
      const jb_json_tree_view_t value = jh_json_field(node->entry, copied[i]);
      if (jh_json_type(value) != JB_JSON_NULL)
        jh_hw_set(h, item, copied[i], value);
    }
    if (node->board &&
        jh_hw_is(jh_json_field(node->board_facts, "kind"), "bus-device")) {
      const jb_json_tree_view_t bus = jh_json_field(node->board_facts, "bus");
      const jh_hw_bus_pins_t *spec =
          jh_hw_bus_pins(jh_hw_text(h, jh_json_field(bus, "kind")));
      jb_json_tree_t *copy = jh_hw_copy(h, bus), *pins = jh_hw_object(h);
      const jb_json_tree_view_t board_signals =
          jh_json_field(node->board_facts, "signals");
      for (size_t i = 0; spec != NULL && i < spec->required_count; ++i)
        jh_hw_set(h, pins, spec->required[i],
                  jh_json_field(board_signals, spec->required[i]));
      jh_hw_set(h, copy, "pins", jh_hw_root(pins));
      jh_hw_set(h, item, "bus", jh_hw_root(copy));
      const jb_json_tree_view_t frequency = jh_json_field(
          jh_json_field(node->board_facts, "attributes"), "defaultSpiClockHz");
      if (jh_hw_is(jh_json_field(bus, "kind"), "spi") &&
          frequency.tree != NULL) {
        jb_json_tree_t *settings = jh_hw_object(h);
        jh_hw_set(h, settings, "frequencyHz", frequency);
        jh_hw_set(h, settings, "mode", jh_hw_integer(h, 0));
        jh_hw_set(h, settings, "bitOrder", jh_hw_string(h, jh_span("msb")));
        jh_hw_set(h, item, "transport", jh_hw_root(settings));
      }
    }
    if (jh_json_count(node->transport) != 0)
      jh_hw_set(h, item, "transport", node->transport);
  }
  return jh_hw_root(item);
}

void jh_hw_build_model(jh_hw_t *h) {
  jb_json_tree_t *model = jh_hw_object(h), *hardware = jh_hw_object(h);
  jb_json_tree_t *selection = jh_hw_object(h);
  jh_hw_container_t nodes;
  jh_hw_container_init(h, &nodes, true);
  jh_hw_set(h, selection, "target",
            jh_hw_string(h, jh_string_span(&h->target->id)));
  jh_hw_set(h, selection, "board",
            jh_hw_string(h, jh_string_span(&h->board->id)));
  jh_hw_set(h, selection, "assembly",
            jh_hw_string(h, jh_span(h->assembly_name)));
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&h->nodes); ++i) {
    const jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    jh_hw_container_put(h, &nodes, jh_span(node->path), model_node(h, node));
  }
  const jb_json_tree_view_t node_values = jh_hw_container_finish(h, &nodes);
  jb_json_tree_t *reservations = jh_hw_object(h);
  const jb_json_tree_view_t table =
      jh_json_field(jh_json_field(h->board->root, "gpio"), "reservations");
  for (size_t i = 0; h->memory.status == HAL_OK && i < jh_json_count(table);
       ++i) {
    const jb_json_tree_view_t input = jh_json_item(table, i);
    const char *key = jh_hw_key(h, input);
    jb_json_tree_t *item = jh_hw_object(h);
    static const char *const fields[] = {"strength", "owner", "pins"};
    for (size_t j = 0; j < COUNTOF(fields); ++j)
      jh_hw_set(h, item, fields[j], jh_json_field(input, fields[j]));
    jh_hw_set(
        h, item, "released",
        jh_hw_boolean(
            h, jh_hw_list_has(jh_json_field(h->assembly, "releaseReservations"),
                              key)));
    jh_hw_set(h, reservations, key, jh_hw_root(item));
  }
  jh_hw_set(h, hardware, "nodes", node_values);
  jh_hw_set(h, hardware, "buses", h->buses);
  jh_hw_set(h, hardware, "nets", h->nets);
  jh_hw_set(h, hardware, "clock", h->clock);
  jh_hw_set(h, hardware, "statusLed", h->status_led);
  jh_hw_set(h, hardware, "capabilities", h->capabilities);
  jh_hw_set(h, hardware, "providers", h->providers);
  jh_hw_set(h, hardware, "reservations", jh_hw_root(reservations));
  jh_hw_set(h, model, "selection", jh_hw_root(selection));
  jh_hw_set(h, model, "hardware", jh_hw_root(hardware));
  h->model = exact_numbers(h, jh_hw_root(model), false);
}
