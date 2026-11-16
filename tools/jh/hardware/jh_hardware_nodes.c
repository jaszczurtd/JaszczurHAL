#include "jh_hardware_internal.h"
#include <stdlib.h>
#include <string.h>

static const char *const node_suffixes[] = {
    "PRESENT",          "CONFIG_AVAILABLE",  "CONFIG_INIT",  "BUS_INDEX",
    "CONTROLLER_INDEX", "ADDRESS",           "FREQUENCY_HZ", "SPI_MODE",
    "SPI_BIT_ORDER",    "SPI_SETTINGS_INIT", "INPUT_MODE"};
static const char *const pin_suffixes[] = {"",       "_CONNECTED", "_DOMAIN",
                                           "_OWNER", "_PROVIDER",  "_INDEX"};
static const char *const bus_suffixes[] = {
    "INDEX",     "FREQUENCY_HZ", "UART_PORT", "BAUD_RATE",
    "DATA_BITS", "PARITY",       "STOP_BITS", "FRAME_CONFIG"};

static int node_order(const void *a, const void *b) {
  return strcmp((*(const jh_hw_node_t *const *)a)->path,
                (*(const jh_hw_node_t *const *)b)->path);
}

static void sort_nodes(jh_hw_t *h) {
  if (jb_vector_size(&h->nodes) > 1)
    qsort(jb_vector_data(&h->nodes), jb_vector_size(&h->nodes),
          sizeof(jh_hw_node_t *), node_order);
}

static jh_hw_node_t *add_node(jh_hw_t *h, const char *path,
                              const jh_hw_type_t *type, jh_hw_origin_t origin,
                              bool board) {
  if (h->memory.status != HAL_OK || type == NULL)
    return NULL;
  if (jb_vector_size(&h->nodes) >= 4096) {
    jh_node_storage(&h->memory, HAL_EOVERFLOW);
    return NULL;
  }
  jh_hw_node_t *node = calloc(1, sizeof *node);
  if (node == NULL) {
    jh_node_storage(&h->memory, HAL_ENOMEM);
    return NULL;
  }
  node->path = path;
  node->binding = type->binding;
  node->type = type->data;
  node->origin = origin;
  node->board = board;
  node->required = true;
  node->valid_properties = true;
  jh_node_storage(&h->memory,
                  jb_vector_init(&node->properties, sizeof(jh_hw_property_t),
                                 jb_heap_allocator()));
  jh_node_storage(&h->memory, jb_vector_init(&node->pins, sizeof(jh_hw_pin_t),
                                             jb_heap_allocator()));
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jb_vector_append(&h->nodes, &node, 1));
  if (h->memory.status != HAL_OK) {
    jb_vector_destroy(&node->pins);
    jb_vector_destroy(&node->properties);
    free(node);
    return NULL;
  }
  return node;
}

static void expand(jh_hw_t *h, const char *path, const jh_hw_type_t *type,
                   jh_hw_origin_t origin, size_t depth) {
  if (depth >= 128) {
    jh_node_storage(&h->memory, HAL_EOVERFLOW);
    return;
  }
  if (add_node(h, path, type, origin, false) == NULL)
    return;
  const jb_json_tree_view_t children = jh_json_field(type->data, "children");
  for (size_t i = 0; h->memory.status == HAL_OK && i < jh_json_count(children);
       ++i) {
    const jb_json_tree_view_t child = jh_json_item(children, i);
    const char *name = jh_hw_key(h, child);
    const char *full =
        jh_hw_join(h, JH_PARTS(jh_span(path), jh_span("."), jh_span(name)));
    const jh_hw_type_t *child_type =
        jh_hw_type(h, jh_hw_text(h, jh_json_field(child, "compatible")));
    expand(h, full, child_type,
           (jh_hw_origin_t){jb_string_data(&type->binding->path),
                            jh_hw_pointer(h, "/children", name)},
           depth + 1);
  }
}

static void project_children(jh_hw_t *h, jb_json_tree_view_t entry,
                             const jh_hw_type_t *type, const char *at,
                             size_t depth) {
  if (depth >= 128) {
    jh_node_storage(&h->memory, HAL_EOVERFLOW);
    return;
  }
  const jb_json_tree_view_t declared = jh_json_field(type->data, "children");
  const jb_json_tree_view_t children = jh_json_field(entry, "children");
  for (size_t i = 0; h->memory.status == HAL_OK && i < jh_json_count(children);
       ++i) {
    const jb_json_tree_view_t child = jh_json_item(children, i);
    const char *name = jh_hw_key(h, child);
    const char *pointer =
        jh_hw_pointer(h, jh_hw_pointer(h, at, "children"), name);
    const jb_json_tree_view_t definition = jh_json_field(declared, name);
    if (definition.tree == NULL) {
      jh_hw_error(h, "JH-HW-BINDING", (jh_hw_origin_t){h->path, pointer}, NULL,
                  JH_PARTS(jh_string_span(&type->binding->compatible),
                           jh_span(" declares no child "), jh_span(name)));
    } else {
      const jh_hw_type_t *child_type =
          jh_hw_type(h, jh_hw_text(h, jh_json_field(definition, "compatible")));
      if (child_type != NULL)
        project_children(h, child, child_type, pointer, depth + 1);
    }
  }
}

static const char *board_compatible(const char *name,
                                    jb_json_tree_view_t device) {
  const jb_json_tree_view_t kind = jh_json_field(device, "kind");
  if (jh_hw_is(kind, "addressable"))
    return "jaszczurhal,addressable-led";
  if (jh_hw_is(kind, "bus-device"))
    return jh_hw_is(jh_json_field(device, "role"), "sx1262-radio")
               ? "semtech,sx1262"
               : "";
  if (strcmp(name, "statusLed") == 0)
    return "jaszczurhal,gpio-output";
  if (strcmp(name, "userButton") == 0 || strcmp(name, "bootButton") == 0)
    return "jaszczurhal,gpio-input";
  return "";
}

static void board_nodes(jh_hw_t *h, const jh_board_descriptor_t *board) {
  if (board == NULL)
    return;
  const jb_json_tree_view_t devices = jh_json_field(board->root, "devices");
  for (size_t i = 0; h->memory.status == HAL_OK && i < jh_json_count(devices);
       ++i) {
    const jb_json_tree_view_t device = jh_json_item(devices, i);
    const char *name = jh_hw_key(h, device);
    const jh_hw_type_t *type = jh_hw_type(h, board_compatible(name, device));
    const char *path =
        jh_hw_join(h, JH_PARTS(jh_span("board."), jh_span(name)));
    if (type == NULL || jh_hw_node(h, path) != NULL)
      continue;
    jh_hw_node_t *node =
        add_node(h, path, type,
                 (jh_hw_origin_t){jb_string_data(&board->path),
                                  jh_hw_pointer(h, "/devices", name)},
                 true);
    if (node != NULL) {
      node->present = true;
      node->board_facts = device;
    }
  }
}

typedef struct {
  const char *entity, *member;
  jh_hw_origin_t origin, member_origin;
} macro_claim_t;

static int origin_order(jh_hw_t *h, jh_hw_origin_t a, jh_hw_origin_t b) {
  const int order =
      strcmp(jh_hw_source_key(h, a.source), jh_hw_source_key(h, b.source));
  return order != 0
             ? order
             : jh_json_pointer_compare(jh_span(a.pointer), jh_span(b.pointer));
}

static void claim_macro(jh_hw_t *h, jb_map_t *names, const char *name,
                        const char *entity, const char *member,
                        jh_hw_origin_t origin, jh_hw_origin_t member_origin) {
  const jb_json_span_t key = jh_span(name);
  const void *found = NULL;
  if (h->memory.status != HAL_OK)
    return;
  const hal_status_t status = jb_map_find_const(names, &key, &found);
  if (status == HAL_ENOENT) {
    const macro_claim_t claim = {entity, member, origin, member_origin};
    jh_node_storage(&h->memory, jb_map_insert(names, &key, &claim));
    return;
  }
  jh_node_storage(&h->memory, status);
  if (status != HAL_OK)
    return;
  const macro_claim_t *previous = found;
  const bool same = strcmp(previous->entity, entity) == 0;
  if (same && strcmp(previous->member, member) == 0)
    return;
  jh_hw_origin_t first = same ? previous->member_origin : previous->origin;
  jh_hw_origin_t second = same ? member_origin : origin;
  if (origin_order(h, first, second) > 0) {
    const jh_hw_origin_t swap = first;
    first = second;
    second = swap;
  }
  jh_hw_error(h, "JH-HW-MACRO-NAME", second, first.pointer,
              JH_PARTS(jh_span(name), jh_span(" is also produced by "),
                       jh_span(first.source), jh_span(":"),
                       jh_span(first.pointer)));
}

static void claim_suffixes(jh_hw_t *h, jb_map_t *names, const char *prefix,
                           const char *const *suffixes, size_t count,
                           const char *entity, const char *member,
                           jh_hw_origin_t origin,
                           jh_hw_origin_t member_origin) {
  for (size_t i = 0; h->memory.status == HAL_OK && i < count; ++i)
    claim_macro(h, names,
                jh_hw_join(h, JH_PARTS(jh_span(prefix), jh_span(suffixes[i]))),
                entity, member, origin, member_origin);
}

static void macro_names(jh_hw_t *h) {
  static const char *const globals[] = {
      "JH_HW_SCHEMA_VERSION", "JH_HW_TARGET_NAME",     "JH_HW_BOARD_NAME",
      "JH_HW_ASSEMBLY_NAME",  "JH_HW_HARDWARE_SHA256", "JH_HW_HARDWARE_ID"};
  jb_map_t names = {0};
  jh_node_storage(&h->memory,
                  jb_map_init(&names, sizeof(jb_json_span_t),
                              sizeof(macro_claim_t), jh_catalogue_span_order,
                              NULL, jb_heap_allocator()));
  const jh_hw_origin_t global = {h->path, ""};
  for (size_t i = 0; i < COUNTOF(globals); ++i)
    claim_macro(h, &names, globals[i], "global", "", global, global);
  for (size_t i = 0; i < jb_vector_size(&h->registry->capabilities); ++i) {
    const char *cap = jb_string_data(jh_list_at(&h->registry->capabilities, i));
    claim_macro(h, &names,
                jh_hw_join(h, JH_PARTS(jh_span("JH_HW_HAS_"),
                                       jh_span(jh_hw_token(h, cap)))),
                "global", "", global, global);
  }
  const jb_json_tree_view_t assemblies = jh_json_field(h->data, "assemblies");
  jb_vector_t sorted = {0};
  jh_node_storage(&h->memory, jh_list_init(&sorted));
  jh_hw_keys(h, assemblies, &sorted);
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&sorted);
       ++i) {
    const char *assembly = jb_string_data(jh_list_at(&sorted, i));
    const jb_json_tree_view_t item = jh_json_field(assemblies, assembly);
    const char *board = jh_hw_text(h, jh_json_field(item, "board"));
    board_nodes(
        h, jh_board_registry_find(&h->registry->boards.boards, jh_span(board)));
    const jb_json_tree_view_t buses = jh_json_field(item, "buses");
    for (size_t j = 0; j < jh_json_count(buses); ++j) {
      const jb_json_tree_view_t bus = jh_json_item(buses, j);
      const char *name = jh_hw_key(h, bus);
      const char *prefix =
          jh_hw_join(h, JH_PARTS(jh_span("JH_HW_BUS_"),
                                 jh_span(jh_hw_token(h, name)), jh_span("_")));
      const char *entity =
          jh_hw_join(h, JH_PARTS(jh_span("bus:"), jh_span(name)));
      const jh_hw_origin_t origin = {
          h->path,
          jh_hw_pointer(h,
                        jh_hw_pointer(h,
                                      jh_hw_pointer(h, "/assemblies", assembly),
                                      "buses"),
                        name)};
      claim_suffixes(h, &names, prefix, bus_suffixes, COUNTOF(bus_suffixes),
                     entity, "", origin, origin);
      const jb_json_tree_view_t pins = jh_json_field(bus, "pins");
      for (size_t k = 0; k < jh_json_count(pins); ++k) {
        const char *pin = jh_hw_key(h, jh_json_item(pins, k));
        const char *pin_prefix =
            jh_hw_join(h, JH_PARTS(jh_span(prefix), jh_span("PIN_"),
                                   jh_span(jh_hw_token(h, pin))));
        claim_suffixes(h, &names, pin_prefix, pin_suffixes, 2, entity, pin,
                       origin, origin);
      }
    }
  }
  jh_list_destroy(&sorted);
  sort_nodes(h);
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&h->nodes); ++i) {
    const jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    const char *entity =
        jh_hw_join(h, JH_PARTS(jh_span("node:"), jh_span(node->path)));
    const char *prefix = jh_hw_join(
        h, JH_PARTS(jh_span("JH_HW_NODE_"), jh_span(jh_hw_token(h, node->path)),
                    jh_span("_")));
    claim_suffixes(h, &names, prefix, node_suffixes, COUNTOF(node_suffixes),
                   entity, "", node->origin, node->origin);
    static const char *const tables[] = {"properties", "signals"};
    for (size_t t = 0; t < COUNTOF(tables); ++t) {
      const jb_json_tree_view_t values = jh_json_field(node->type, tables[t]);
      for (size_t j = 0; j < jh_json_count(values); ++j) {
        const char *name = jh_hw_key(h, jh_json_item(values, j));
        const char *member = jh_hw_join(
            h, JH_PARTS(jh_span(tables[t]), jh_span(":"), jh_span(name)));
        const char *token = jh_hw_join(
            h, JH_PARTS(jh_span(prefix), jh_span(t == 0 ? "PROP_" : "PIN_"),
                        jh_span(jh_hw_token(h, name))));
        const jh_hw_origin_t origin = {
            jb_string_data(&node->binding->path),
            jh_hw_pointer(h, t == 0 ? "/properties" : "/signals", name)};
        claim_suffixes(h, &names, token, pin_suffixes,
                       t == 0 ? 1 : COUNTOF(pin_suffixes), entity, member,
                       node->origin, origin);
      }
    }
  }
  jb_map_destroy(&names);
}

void jh_hw_declarations(jh_hw_t *h) {
  const jb_json_tree_view_t nodes = jh_json_field(h->data, "nodes");
  for (size_t i = 0; h->memory.status == HAL_OK && i < jh_json_count(nodes);
       ++i) {
    const jb_json_tree_view_t entry = jh_json_item(nodes, i);
    const char *name = jh_hw_key(h, entry);
    const char *at = jh_hw_pointer(h, "/nodes", name);
    const char *compatible = jh_hw_text(h, jh_json_field(entry, "compatible"));
    const jh_hw_type_t *type = jh_hw_type(h, compatible);
    if (strcmp(name, "board") == 0 || strcmp(name, "bus") == 0)
      jh_hw_error(h, "JH-HW-MACRO-NAME", (jh_hw_origin_t){h->path, at}, NULL,
                  JH_PARTS(jh_span("root node "), jh_span(name),
                           jh_span(" is reserved")));
    else if (type == NULL)
      jh_hw_error(h, "JH-HW-BINDING",
                  (jh_hw_origin_t){h->path, jh_hw_pointer(h, at, "compatible")},
                  NULL,
                  JH_PARTS(jh_span("unknown type "), jh_span(compatible)));
    else {
      project_children(h, entry, type, at, 0);
      expand(h, name, type, (jh_hw_origin_t){h->path, at}, 0);
    }
  }
  if (jh_hw_phase(h) == HAL_OK)
    macro_names(h);
}

/* The declaration pass collected every board's macro names. Only the selected
 * board contributes actual nodes, properties and claims. */
static void select_board_nodes(jh_hw_t *h) {
  for (size_t i = 0; i < jb_vector_size(&h->nodes);) {
    jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    if (node->board) {
      jb_vector_destroy(&node->pins);
      jb_vector_destroy(&node->properties);
      free(node);
      jh_node_storage(&h->memory, jb_vector_erase(&h->nodes, i, 1));
    } else {
      ++i;
    }
  }
  board_nodes(h, h->board);
  sort_nodes(h);
}

const char *jh_hw_node_pointer(jh_hw_t *h, const char *path) {
  return jh_hw_pointer(
      h,
      jh_hw_pointer(h, jh_hw_pointer(h, "/assemblies", h->assembly_name),
                    "nodes"),
      path);
}

jh_hw_node_t *jh_hw_parent(jh_hw_t *h, const char *path, const char **child) {
  const char *dot = strrchr(path, '.');
  if (dot == NULL)
    return NULL;
  *child = dot + 1;
  return jh_hw_node(
      h,
      jh_hw_join(h, JH_PARTS(((jb_json_span_t){path, (size_t)(dot - path)}))));
}

void jh_hw_presence(jh_hw_t *h) {
  select_board_nodes(h);
  const jb_json_tree_view_t entries = jh_json_field(h->assembly, "nodes");
  for (size_t i = 0; h->memory.status == HAL_OK && i < jh_json_count(entries);
       ++i) {
    const jb_json_tree_view_t entry = jh_json_item(entries, i);
    const char *path = jh_hw_key(h, entry);
    const jh_hw_origin_t origin = {h->path, jh_hw_node_pointer(h, path)};
    if (strcmp(path, "board") == 0 || strncmp(path, "board.", 6) == 0)
      jh_hw_error(h, "JH-HW-BOARD", origin, NULL,
                  JH_PARTS(jh_span(
                      "board-owned devices cannot be changed by an assembly")));
    else if (jh_hw_node(h, path) == NULL)
      jh_hw_error(h, "JH-HW-PRESENCE", origin, NULL,
                  JH_PARTS(jh_span("no such root node or declared child")));
  }
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&h->nodes); ++i) {
    jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    if (node->board)
      continue;
    node->entry = jh_json_field(entries, node->path);
    const char *at = jh_hw_node_pointer(h, node->path);
    jb_json_tree_view_t present = {0};
    const bool specified = jh_json_member(node->entry, "present", &present);
    node->present = jh_hw_bool(present);
    const char *child = NULL;
    jh_hw_node_t *parent = jh_hw_parent(h, node->path, &child);
    if (parent == NULL) {
      if (!specified)
        jh_hw_error(h, "JH-HW-PRESENCE",
                    (jh_hw_origin_t){h->path, jh_hw_pointer(h, at, "present")},
                    NULL,
                    JH_PARTS(jh_span(
                        "every root node states present in every assembly")));
    } else {
      const jb_json_tree_view_t spec =
          jh_json_field(jh_json_field(parent->type, "children"), child);
      node->required = jh_hw_bool(jh_json_field(spec, "required"));
      if (!specified)
        node->present = parent->present;
      if (!parent->present && jh_json_count(node->entry) != 0) {
        jb_vector_t keys = {0};
        jh_node_storage(&h->memory, jh_list_init(&keys));
        jh_hw_keys(h, node->entry, &keys);
        const char *first =
            node->present ? "present" : jb_string_data(jh_list_at(&keys, 0));
        jh_hw_error(h, "JH-HW-PRESENCE",
                    (jh_hw_origin_t){h->path, jh_hw_pointer(h, at, first)},
                    NULL,
                    JH_PARTS(jh_span(parent->path), jh_span(" is absent")));
        jh_list_destroy(&keys);
      } else if (parent->present && node->required && !node->present) {
        jh_hw_error(
            h, "JH-HW-PRESENCE",
            (jh_hw_origin_t){h->path, jh_hw_pointer(h, at, "present")}, NULL,
            JH_PARTS(jh_span(child), jh_span(" is a required child of "),
                     jh_span(parent->path)));
      }
    }
  }
  for (size_t i = 0; h->memory.status == HAL_OK && i < jh_json_count(entries);
       ++i) {
    const jb_json_tree_view_t entry = jh_json_item(entries, i);
    const char *at = jh_hw_node_pointer(h, jh_hw_key(h, entry));
    const jb_json_tree_view_t bus = jh_json_field(entry, "bus");
    if (jh_json_type(bus) != JB_JSON_NULL &&
        jh_json_field(jh_json_field(h->assembly, "buses"), jh_hw_text(h, bus))
                .tree == NULL)
      jh_hw_error(h, "JH-HW-TRANSPORT",
                  (jh_hw_origin_t){h->path, jh_hw_pointer(h, at, "bus")}, NULL,
                  JH_PARTS(jh_span("no bus "), jh_span(jh_hw_text(h, bus)),
                           jh_span(" in this assembly")));
    const jb_json_tree_view_t pins = jh_json_field(entry, "pins");
    for (size_t j = 0; j < jh_json_count(pins); ++j) {
      const jb_json_tree_view_t pin = jh_json_item(pins, j);
      const jb_json_tree_view_t net = jh_json_field(pin, "net");
      if (net.tree != NULL &&
          jh_json_field(jh_json_field(h->assembly, "nets"), jh_hw_text(h, net))
                  .tree == NULL)
        jh_hw_error(h, "JH-HW-NET-OWNER",
                    (jh_hw_origin_t){
                        h->path, jh_hw_pointer(h, jh_hw_pointer(h, at, "pins"),
                                               jh_hw_key(h, pin))},
                    NULL,
                    JH_PARTS(jh_span("no net "), jh_span(jh_hw_text(h, net)),
                             jh_span(" in this assembly")));
    }
  }
}
