#include "jh_hardware_internal.h"
#include <string.h>

static void property_set(jh_hw_t *h, jh_hw_node_t *node, const char *name,
                         jb_json_tree_view_t value, jh_hw_origin_t origin) {
  const jh_hw_property_t property = {name, value, origin};
  jh_hw_property_t *existing = jh_hw_property(node, name);
  if (existing != NULL)
    *existing = property;
  else if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory,
                    jb_vector_append(&node->properties, &property, 1));
}

static void property_overlay(jh_hw_t *h, jh_hw_node_t *node,
                             jb_json_tree_view_t properties,
                             jh_hw_origin_t origin) {
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jh_json_count(properties); ++i) {
    const jb_json_tree_view_t value = jh_json_item(properties, i);
    const char *name = jh_hw_key(h, value);
    property_set(h, node, name, value,
                 (jh_hw_origin_t){origin.source,
                                  jh_hw_pointer(h, origin.pointer, name)});
  }
}

static void property_sources(jh_hw_t *h, jh_hw_node_t *node) {
  const jb_json_tree_view_t definitions =
      jh_json_field(node->type, "properties");
  const char *source = jb_string_data(&node->binding->path);
  for (size_t i = 0; i < jh_json_count(definitions); ++i) {
    const jb_json_tree_view_t definition = jh_json_item(definitions, i);
    jb_json_tree_view_t value = {0};
    if (jh_json_member(definition, "default", &value)) {
      const char *name = jh_hw_key(h, definition);
      property_set(
          h, node, name, value,
          (jh_hw_origin_t){
              source, jh_hw_pointer(h, jh_hw_pointer(h, "/properties", name),
                                    "default")});
    }
  }
  if (node->board) {
    const jb_json_tree_view_t level =
        jh_json_field(node->board_facts, "activeLevel");
    if (level.tree != NULL) {
      property_set(h, node, "activeLow",
                   jh_hw_boolean(h, jh_hw_is(level, "low")), node->origin);
    } else if (jh_hw_is(jh_json_field(node->board_facts, "kind"),
                        "bus-device")) {
      const jb_json_tree_view_t attributes =
          jh_json_field(node->board_facts, "attributes");
      for (size_t i = 0; i < jh_json_count(attributes); ++i) {
        const jb_json_tree_view_t value = jh_json_item(attributes, i);
        const char *name = jh_hw_key(h, value);
        if (jh_json_field(definitions, name).tree != NULL)
          property_set(h, node, name, value, node->origin);
      }
    }
    return;
  }
  const char *child = NULL;
  const jh_hw_node_t *parent = jh_hw_parent(h, node->path, &child);
  if (parent != NULL) {
    const jb_json_tree_view_t spec =
        jh_json_field(jh_json_field(parent->type, "children"), child);
    property_overlay(
        h, node, jh_json_field(spec, "properties"),
        (jh_hw_origin_t){jb_string_data(&parent->binding->path),
                         jh_hw_pointer(h, jh_hw_pointer(h, "/children", child),
                                       "properties")});
  }
  jb_json_tree_view_t project = jh_json_field(h->data, "nodes");
  const char *at = "/nodes";
  const char *start = node->path;
  for (;;) {
    const char *dot = strchr(start, '.');
    const char *name = dot == NULL
                           ? start
                           : jh_hw_join(h, JH_PARTS(((jb_json_span_t){
                                               start, (size_t)(dot - start)})));
    project = jh_json_field(project, name);
    at = jh_hw_pointer(h, at, name);
    if (dot == NULL)
      break;
    project = jh_json_field(project, "children");
    at = jh_hw_pointer(h, at, "children");
    start = dot + 1;
  }
  property_overlay(
      h, node, jh_json_field(project, "properties"),
      (jh_hw_origin_t){h->path, jh_hw_pointer(h, at, "properties")});
  property_overlay(
      h, node, jh_json_field(node->entry, "properties"),
      (jh_hw_origin_t){
          h->path,
          jh_hw_pointer(h, jh_hw_node_pointer(h, node->path), "properties")});
}

void jh_hw_properties(jh_hw_t *h) {
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&h->nodes); ++i) {
    jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    if (!node->present)
      continue;
    const size_t before = h->errors;
    property_sources(h, node);
    jb_json_node_t definitions = {0};
    const jb_json_node_t root = jh_binding_root(&h->memory, node->binding);
    (void)jh_node_find(&h->memory, root, "properties", &definitions);
    for (size_t j = 0;
         h->memory.status == HAL_OK && j < jb_vector_size(&node->properties);
         ++j) {
      const jh_hw_property_t *property =
          jb_vector_const_at(&node->properties, j);
      jb_json_node_t definition = {0};
      if (definitions.doc == NULL ||
          !jh_node_find(&h->memory, definitions, property->name, &definition)) {
        jh_hw_error(h, "JH-HW-PROPERTY", property->origin, NULL,
                    JH_PARTS(jh_string_span(&node->binding->compatible),
                             jh_span(" has no property "),
                             jh_span(property->name)));
        continue;
      }
      jh_json_document_t value = {0};
      const jb_json_node_t parsed = jh_hw_document(h, property->value, &value);
      jh_failure_t problem;
      jh_failure_init(&problem);
      if (h->memory.status == HAL_OK &&
          jh_catalogue_property(definition, parsed, property->origin.source,
                                property->origin.pointer, &problem) != HAL_OK)
        jh_hw_record(h, &problem);
      jh_failure_destroy(&problem);
      jh_json_document_destroy(&value);
    }
    const jb_json_tree_view_t specs = jh_json_field(node->type, "properties");
    for (size_t j = 0; j < jh_json_count(specs); ++j) {
      const jb_json_tree_view_t definition = jh_json_item(specs, j);
      const char *name = jh_hw_key(h, definition);
      if (jh_hw_bool(jh_json_field(definition, "required")) &&
          jh_hw_property(node, name) == NULL)
        jh_hw_error(
            h, "JH-HW-PROPERTY",
            (jh_hw_origin_t){
                h->path, jh_hw_pointer(
                             h,
                             jh_hw_pointer(h, jh_hw_node_pointer(h, node->path),
                                           "properties"),
                             name)},
            NULL,
            JH_PARTS(jh_string_span(&node->binding->compatible),
                     jh_span(" requires "), jh_span(name)));
    }
    node->valid_properties = before == h->errors;
  }
  if (h->memory.status == HAL_OK)
    jh_hw_signals(h);
}

static const char *mapped(const jb_map_t *map, const char *key) {
  const jb_json_span_t span = jh_span(key);
  const void *value = NULL;
  return jb_map_find_const(map, &span, &value) == HAL_OK
             ? *(const char *const *)value
             : NULL;
}

static void map_signal(jh_hw_t *h, jb_map_t *map, const char *key,
                       const char *value) {
  if (h->memory.status != HAL_OK)
    return;
  const jb_json_span_t span = jh_span(key);
  void *existing = NULL;
  if (jb_map_find(map, &span, &existing) == HAL_OK)
    *(const char **)existing = value;
  else
    jh_node_storage(&h->memory, jb_map_insert(map, &span, &value));
}

static void connections(jh_hw_t *h) {
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&h->nodes); ++i) {
    const jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    const jb_json_tree_view_t table = jh_json_field(node->type, "connections");
    for (size_t j = 0; j < jh_json_count(table); ++j) {
      const jb_json_tree_view_t connection = jh_json_item(table, j);
      const char *module =
          jh_hw_join(h, JH_PARTS(jh_span(node->path), jh_span("."),
                                 jh_span(jh_hw_key(h, connection))));
      const char *owner = jh_hw_join(
          h,
          JH_PARTS(jh_span(node->path), jh_span("."),
                   jh_span(jh_hw_text(h, jh_json_field(connection, "owner")))));
      map_signal(h, &h->module_owners, module, owner);
      map_signal(h, &h->connections, owner, module);
      const jb_json_tree_view_t aliases = jh_json_field(connection, "aliases");
      for (size_t k = 0; k < jh_json_count(aliases); ++k) {
        const char *alias = jh_hw_join(
            h, JH_PARTS(jh_span(node->path), jh_span("."),
                        jh_span(jh_hw_text(h, jh_json_item(aliases, k)))));
        map_signal(h, &h->connections, alias, module);
      }
    }
  }
}

static bool assignment(jh_hw_t *h, const char *key, jh_hw_pin_t *pin) {
  for (size_t depth = 0; depth < 128 && h->memory.status == HAL_OK; ++depth) {
    const char *name = NULL;
    jh_hw_node_t *node = jh_hw_parent(h, key, &name);
    if (node == NULL)
      return false;
    const jb_json_tree_view_t pins = jh_json_field(node->entry, "pins");
    if (jh_json_member(pins, name, &pin->value)) {
      pin->site = key;
      pin->owner = key;
      const char *owner = NULL;
      size_t steps = 0;
      while ((owner = mapped(&h->module_owners, pin->owner)) != NULL) {
        if (++steps >= 128) {
          jh_node_storage(&h->memory, HAL_EOVERFLOW);
          return false;
        }
        pin->owner = owner;
      }
      pin->origin = (jh_hw_origin_t){
          h->path,
          jh_hw_pointer(
              h, jh_hw_pointer(h, jh_hw_node_pointer(h, node->path), "pins"),
              name)};
      return true;
    }
    key = mapped(&h->connections, key);
    if (key == NULL)
      return false;
  }
  jh_node_storage(&h->memory, HAL_EOVERFLOW);
  return false;
}

bool jh_hw_condition(jh_hw_t *h, jh_hw_node_t *node,
                     jb_json_tree_view_t signal) {
  const jb_json_tree_view_t when = jh_json_field(signal, "when");
  if (jh_json_type(when) == JB_JSON_NULL)
    return true;
  const jh_hw_property_t *property =
      jh_hw_property(node, jh_hw_text(h, jh_json_field(when, "property")));
  return jh_hw_equal(
      h, property == NULL ? (jb_json_tree_view_t){0} : property->value,
      jh_json_field(when, "equals"));
}

void jh_hw_signals(jh_hw_t *h) {
  connections(h);
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&h->nodes); ++i) {
    jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    if (!node->present || node->board || !node->valid_properties)
      continue;
    const char *at =
        jh_hw_pointer(h, jh_hw_node_pointer(h, node->path), "pins");
    const jb_json_tree_view_t signals = jh_json_field(node->type, "signals");
    const jb_json_tree_view_t pins = jh_json_field(node->entry, "pins");
    for (size_t j = 0; j < jh_json_count(pins); ++j) {
      const char *name = jh_hw_key(h, jh_json_item(pins, j));
      if (jh_json_field(signals, name).tree == NULL)
        jh_hw_error(h, "JH-HW-PROPERTY",
                    (jh_hw_origin_t){h->path, jh_hw_pointer(h, at, name)}, NULL,
                    JH_PARTS(jh_string_span(&node->binding->compatible),
                             jh_span(" has no signal "), jh_span(name)));
    }
    jb_vector_t names = {0};
    jh_node_storage(&h->memory, jh_list_init(&names));
    jh_hw_keys(h, signals, &names);
    for (size_t j = 0; h->memory.status == HAL_OK && j < jb_vector_size(&names);
         ++j) {
      const char *name =
          jh_hw_join(h, JH_PARTS(jh_string_span(jh_list_at(&names, j))));
      const jb_json_tree_view_t signal = jh_json_field(signals, name);
      const char *key = jh_hw_join(
          h, JH_PARTS(jh_span(node->path), jh_span("."), jh_span(name)));
      const char *connected = mapped(&h->connections, key);
      const jh_hw_origin_t origin = {h->path, jh_hw_pointer(h, at, name)};
      if (connected != NULL && jh_json_field(pins, name).tree != NULL) {
        jh_hw_error(h, "JH-HW-NET-OWNER", origin, NULL,
                    JH_PARTS(jh_span(connected),
                             jh_span(" already wires this signal")));
        continue;
      }
      jh_hw_pin_t pin = {0};
      pin.name = name;
      pin.owner = key;
      pin.origin = origin;
      const bool assigned = assignment(h, key, &pin);
      pin.active = jh_hw_condition(h, node, signal);
      if (!pin.active) {
        const jb_json_tree_view_t when = jh_json_field(signal, "when");
        if (assigned && jh_json_type(pin.value) != JB_JSON_NULL)
          jh_hw_error(
              h, "JH-HW-PROPERTY", pin.origin, NULL,
              JH_PARTS(jh_span(name), jh_span(" must be unconnected while "),
                       jh_span(jh_hw_text(h, jh_json_field(when, "property"))),
                       jh_span(" is not "),
                       jh_span(jh_hw_repr(h, jh_json_field(when, "equals")))));
        pin.value = (jb_json_tree_view_t){0};
        pin.origin = origin;
        pin.owner = key;
        pin.site = NULL;
      } else if (!assigned && jh_hw_bool(jh_json_field(signal, "required"))) {
        jh_hw_error(h, "JH-HW-PROPERTY", origin, NULL,
                    JH_PARTS(jh_string_span(&node->binding->compatible),
                             jh_span(" requires signal "), jh_span(name)));
      }
      jh_node_storage(&h->memory, jb_vector_append(&node->pins, &pin, 1));
    }
    jh_list_destroy(&names);
  }
}

void jh_hw_clock(jh_hw_t *h) {
  const jb_json_tree_view_t name =
      jh_json_field(jh_json_field(h->assembly, "clock"), "tree");
  const jh_clock_file_t *file = jh_clock_registry_find(
      &h->registry->clocks, jh_string_span(&h->target->id));
  const jb_json_tree_view_t trees = file == NULL
                                        ? (jb_json_tree_view_t){0}
                                        : jh_json_field(file->root, "trees");
  const jb_json_tree_view_t tree = jh_json_field(trees, jh_hw_text(h, name));
  const jh_hw_origin_t origin = {
      h->path,
      jh_hw_pointer(
          h,
          jh_hw_pointer(h, jh_hw_pointer(h, "/assemblies", h->assembly_name),
                        "clock"),
          "tree")};
  if (tree.tree == NULL) {
    jb_vector_t names = {0};
    jb_string_t repr = {0};
    (void)jb_string_init(&repr, jb_heap_allocator());
    jh_node_storage(&h->memory, jh_list_init(&names));
    jh_hw_keys(h, trees, &names);
    jh_node_storage(&h->memory, jh_repr_list(&repr, &names));
    jh_hw_error(h, "JH-HW-CLOCK", origin, NULL,
                JH_PARTS(jh_string_span(&h->target->id),
                         jh_span(" supports the trees "),
                         jh_string_span(&repr)));
    jb_string_destroy(&repr);
    jh_list_destroy(&names);
    return;
  }
  jb_vector_t errors = {0};
  jh_node_storage(&h->memory, jh_clock_source_errors_init(&errors));
  const jb_json_tree_view_t board_sources =
      jh_json_field(h->board->root, "clockSources");
  if (h->memory.status == HAL_OK)
    h->memory.status = jh_clock_source_errors(
        board_sources, tree, jh_json_field(h->target->root, "clockInputs"),
        &errors, h->failure);
  if (jb_vector_size(&errors) != 0) {
    jb_string_t message = {0};
    (void)jb_string_init(&message, jb_heap_allocator());
    for (size_t i = 0; i < jb_vector_size(&errors); ++i) {
      const jh_clock_source_error_t *error = jb_vector_const_at(&errors, i);
      if (i != 0)
        jh_put(&message, &h->memory.status, "; ");
      jh_put_span(&message, &h->memory.status, jh_string_span(&error->message));
    }
    jh_hw_error(h, "JH-HW-CLOCK", origin, NULL,
                JH_PARTS(jh_string_span(&message)));
    jb_string_destroy(&message);
  }
  jh_clock_source_errors_destroy(&errors);
  if (h->errors != 0 || h->memory.status != HAL_OK)
    return;
  jb_json_tree_t *clock = jh_hw_object(h), *sources = jh_hw_object(h);
  const jb_json_tree_view_t required = jh_json_field(tree, "requiredSources");
  for (size_t i = 0; i < jh_json_count(required); ++i) {
    const char *key = jh_hw_key(h, jh_json_item(required, i));
    jh_hw_set(h, sources, key, jh_json_field(board_sources, key));
  }
  jh_hw_set(h, clock, "tree", name);
  static const char *const fields[] = {"backend", "frequenciesHz",
                                       "parameters"};
  for (size_t i = 0; i < COUNTOF(fields); ++i)
    jh_hw_set(h, clock, fields[i], jh_json_field(tree, fields[i]));
  jh_hw_set(h, clock, "sources", jh_hw_root(sources));
  h->clock = jh_hw_root(clock);
}
