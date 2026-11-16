#include "jh_hardware_internal.h"
#include <string.h>

const jh_hw_bus_pins_t *jh_hw_bus_pins(const char *kind) {
  static const char *const spi[] = {"miso", "mosi", "sck"};
  static const char *const i2c[] = {"scl", "sda"};
  static const char *const uart[] = {"rx", "tx"};
  static const char *const flow[] = {"cts", "rts"};
  static const jh_hw_bus_pins_t table[] = {
      {"spi", spi, NULL, COUNTOF(spi), 0},
      {"i2c", i2c, NULL, COUNTOF(i2c), 0},
      {"uart", uart, flow, COUNTOF(uart), COUNTOF(flow)}};
  for (size_t i = 0; i < COUNTOF(table); ++i)
    if (strcmp(table[i].kind, kind) == 0)
      return &table[i];
  return NULL;
}

const char *jh_hw_assembly_pointer(jh_hw_t *h, const char *section,
                                   const char *name) {
  return jh_hw_pointer(
      h,
      jh_hw_pointer(h, jh_hw_pointer(h, "/assemblies", h->assembly_name),
                    section),
      name);
}

const char *jh_hw_value_text(jh_hw_t *h, jb_json_tree_view_t value) {
  return jh_json_type(value) == JB_JSON_STRING ? jh_hw_text(h, value)
                                               : jh_hw_repr(h, value);
}

static jh_hw_endpoint_key_t endpoint_key(jh_hw_t *h,
                                         jb_json_tree_view_t endpoint) {
  jh_hw_endpoint_key_t key = {0};
  const jb_json_tree_view_t domain = jh_json_field(endpoint, "domain");
  (void)jh_json_string(domain, &key.domain);
  const bool device = jh_hw_is(domain, "device-pin");
  if (device || !jh_hw_is(domain, "soc-gpio"))
    (void)jh_json_string(jh_json_field(endpoint, device ? "node" : "component"),
                         &key.owner);
  const jb_json_tree_view_t id =
      jh_json_field(endpoint, device ? "index" : "id");
  key.numeric = jh_json_type(id) == JB_JSON_NUMBER;
  if (key.numeric) {
    if (!jh_hw_i64(h, id, &key.number))
      jh_node_require(&h->memory, HAL_ESTATE, 3,
                      "invalid resolved endpoint identifier");
  } else {
    (void)jh_json_string(id, &key.text);
  }
  return key;
}

int jh_hw_endpoint_order(const void *left, const void *right, void *context) {
  (void)context;
  const jh_hw_endpoint_key_t *a = left, *b = right;
  int order = jh_span_compare(a->domain, b->domain);
  if (order == 0)
    order = jh_span_compare(a->owner, b->owner);
  if (order == 0)
    order = (a->numeric > b->numeric) - (a->numeric < b->numeric);
  if (order == 0)
    order = a->numeric ? (a->number > b->number) - (a->number < b->number)
                       : jh_span_compare(a->text, b->text);
  return order;
}

bool jh_hw_endpoint_equal(jh_hw_t *h, jb_json_tree_view_t a,
                          jb_json_tree_view_t b) {
  if (jh_json_type(a) == JB_JSON_NULL || jh_json_type(b) == JB_JSON_NULL)
    return false;
  const jh_hw_endpoint_key_t left = endpoint_key(h, a),
                             right = endpoint_key(h, b);
  return h->memory.status == HAL_OK &&
         jh_hw_endpoint_order(&left, &right, NULL) == 0;
}

static const jh_hw_claim_t *previous_claim(jh_hw_t *h,
                                           jb_json_tree_view_t endpoint,
                                           jh_hw_endpoint_key_t *key) {
  if (jh_json_type(endpoint) == JB_JSON_NULL)
    return NULL;
  const jh_hw_endpoint_key_t candidate = endpoint_key(h, endpoint);
  if (key != NULL)
    *key = candidate;
  const void *found = NULL;
  if (h->memory.status == HAL_OK) {
    const hal_status_t status =
        jb_map_find_const(&h->claims, &candidate, &found);
    if (status != HAL_ENOENT)
      jh_node_storage(&h->memory, status);
  }
  return found;
}

static void claim(jh_hw_t *h, jb_json_tree_view_t endpoint, const char *owner,
                  jh_hw_origin_t origin, bool board, const char *net) {
  jh_hw_endpoint_key_t key = {0};
  const jh_hw_claim_t *earlier = previous_claim(h, endpoint, &key);
  if (h->memory.status != HAL_OK)
    return;
  if (earlier == NULL) {
    const jh_hw_claim_t item = {owner, net, endpoint, origin, board};
    jh_node_storage(&h->memory, jb_map_insert(&h->claims, &key, &item));
  } else if (!board && !(net != NULL && (earlier->board ||
                                         (earlier->net != NULL &&
                                          strcmp(net, earlier->net) == 0)))) {
    jh_hw_error(
        h, "JH-HW-PIN-CONFLICT", origin, earlier->origin.pointer,
        JH_PARTS(jh_span(owner), jh_span(" and "), jh_span(earlier->owner),
                 jh_span(" use the same endpoint without a shared net")));
  }
}

static jb_json_tree_view_t reservations(jh_hw_t *h) {
  return jh_json_field(jh_json_field(h->board->root, "gpio"), "reservations");
}

static void releases(jh_hw_t *h) {
  const jb_json_tree_view_t table = reservations(h);
  const jb_json_tree_view_t release =
      jh_json_field(h->assembly, "releaseReservations");
  for (size_t i = 0; h->memory.status == HAL_OK && i < jh_json_count(release);
       ++i) {
    const char *key = jh_hw_text(h, jh_json_item(release, i));
    const jb_json_tree_view_t item = jh_json_field(table, key);
    const jh_hw_origin_t origin = {
        h->path, jh_hw_assembly_pointer(h, "releaseReservations",
                                        jh_node_index(&h->memory, i))};
    if (item.tree == NULL)
      jh_hw_error(h, "JH-HW-RESERVATION", origin, NULL,
                  JH_PARTS(jh_string_span(&h->board->id),
                           jh_span(" has no reservation "), jh_span(key)));
    else if (!jh_hw_is(jh_json_field(item, "strength"), "soft"))
      jh_hw_error(h, "JH-HW-RESERVATION", origin, NULL,
                  JH_PARTS(jh_span(key), jh_span(" is a hard reservation")));
  }
}

static void board_claims(jh_hw_t *h) {
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&h->nodes); ++i) {
    jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    if (!node->board)
      continue;
    jb_json_tree_view_t endpoints = jh_json_field(node->board_facts, "signals");
    const jb_json_tree_view_t endpoint =
        jh_json_field(node->board_facts, "endpoint");
    if (endpoint.tree != NULL) {
      const char *compatible = jb_string_data(&node->binding->compatible);
      const char *signal =
          strcmp(compatible, "jaszczurhal,addressable-led") == 0
              ? "data"
              : (strcmp(compatible, "jaszczurhal,gpio-output") == 0 ? "out"
                                                                    : "in");
      jb_json_tree_t *single = jh_hw_object(h);
      jh_hw_set(h, single, signal, endpoint);
      endpoints = jh_hw_root(single);
    }
    const jb_json_tree_view_t signals = jh_json_field(node->type, "signals");
    jb_vector_t names = {0};
    jh_node_storage(&h->memory, jh_list_init(&names));
    jh_hw_keys(h, endpoints, &names);
    for (size_t j = 0; h->memory.status == HAL_OK && j < jb_vector_size(&names);
         ++j) {
      const char *name =
          jh_hw_join(h, JH_PARTS(jh_string_span(jh_list_at(&names, j))));
      const jb_json_tree_view_t value = jh_json_field(endpoints, name);
      const char *owner = jh_hw_join(
          h, JH_PARTS(jh_span(node->path), jh_span("."), jh_span(name)));
      const jh_hw_origin_t origin = {
          node->origin.source,
          endpoint.tree != NULL
              ? jh_hw_pointer(h, node->origin.pointer, "endpoint")
              : jh_hw_pointer(h,
                              jh_hw_pointer(h, node->origin.pointer, "signals"),
                              name)};
      if (jh_json_field(signals, name).tree != NULL) {
        const jh_hw_pin_t pin = {name,  owner, owner,  NULL,
                                 value, value, origin, true};
        jh_node_storage(&h->memory, jb_vector_append(&node->pins, &pin, 1));
      }
      if (jh_json_type(value) != JB_JSON_NULL)
        claim(h, value, owner, origin, true, NULL);
    }
    jh_list_destroy(&names);
    for (size_t j = 0; j < jh_json_count(signals); ++j) {
      const char *name = jh_hw_key(h, jh_json_item(signals, j));
      if (jh_hw_pin(node, name) == NULL) {
        jh_hw_pin_t pin = {0};
        pin.name = name;
        pin.owner = jh_hw_join(
            h, JH_PARTS(jh_span(node->path), jh_span("."), jh_span(name)));
        pin.origin = node->origin;
        jh_node_storage(&h->memory, jb_vector_append(&node->pins, &pin, 1));
      }
    }
  }
  const jb_json_tree_view_t sources = jh_json_field(h->clock, "sources");
  jb_vector_t names = {0};
  jh_node_storage(&h->memory, jh_list_init(&names));
  jh_hw_keys(h, sources, &names);
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&names);
       ++i) {
    const char *name = jb_string_data(jh_list_at(&names, i));
    const jb_json_tree_view_t pins =
        jh_json_field(jh_json_field(sources, name), "pins");
    for (size_t j = 0; j < jh_json_count(pins); ++j)
      claim(h, jh_json_item(pins, j),
            jh_hw_join(h, JH_PARTS(jh_span("clock."), jh_span(name))),
            (jh_hw_origin_t){
                jb_string_data(&h->board->path),
                jh_hw_pointer(
                    h,
                    jh_hw_pointer(h, jh_hw_pointer(h, "/clockSources", name),
                                  "pins"),
                    jh_node_index(&h->memory, j))},
            true, NULL);
  }
  jh_list_destroy(&names);
}

static bool pin_valid(jh_hw_t *h, jb_json_tree_view_t id) {
  int64_t integer = 0;
  jb_json_span_t text = {0};
  const bool numeric = jh_hw_i64(h, id, &integer);
  const bool string = jh_json_string(id, &text);
  for (size_t i = 0; i < jb_vector_size(&h->target->valid_pins); ++i) {
    const jh_board_pin_t *pin = jb_vector_const_at(&h->target->valid_pins, i);
    if (pin->text ? (string && jh_span_equal(text, jh_string_span(&pin->name)))
                  : (numeric && integer == pin->number))
      return true;
  }
  return false;
}

static jb_json_tree_view_t normalize(jh_hw_t *h, jb_json_tree_view_t value,
                                     jb_json_tree_view_t allowed,
                                     jh_hw_origin_t origin) {
  const jb_json_tree_view_t none = {0};
  if (jh_json_type(value) == JB_JSON_NULL)
    return none;
  if (jh_json_type(value) != JB_JSON_OBJECT) {
    jb_json_tree_t *endpoint = jh_hw_object(h);
    jh_hw_set(h, endpoint, "domain", jh_hw_string(h, jh_span("soc-gpio")));
    jh_hw_set(h, endpoint, "id", value);
    value = jh_hw_root(endpoint);
  }
  const char *domain = jh_hw_text(h, jh_json_field(value, "domain"));
  if (!jh_hw_list_has(allowed, domain)) {
    jh_hw_error(h, "JH-HW-PIN-RANGE", origin, NULL,
                JH_PARTS(jh_span(domain), jh_span(" is not one of "),
                         jh_span(jh_hw_repr(h, allowed))));
    return none;
  }
  if (strcmp(domain, "device-pin") == 0) {
    const char *name = jh_hw_text(h, jh_json_field(value, "node"));
    const jh_hw_node_t *provider = jh_hw_node(h, name);
    if (provider == NULL || !provider->present) {
      jh_hw_error(h, "JH-HW-PRESENCE",
                  (jh_hw_origin_t){origin.source,
                                   jh_hw_pointer(h, origin.pointer, "node")},
                  NULL,
                  JH_PARTS(jh_span(name), jh_span(" is not a present node")));
      return none;
    }
    int64_t count = 0, index = 0;
    (void)jh_hw_i64(h, jh_json_field(provider->type, "pinCount"), &count);
    (void)jh_hw_i64(h, jh_json_field(value, "index"), &index);
    if (index >= count) {
      jh_hw_error(h, "JH-HW-PIN-RANGE",
                  (jh_hw_origin_t){origin.source,
                                   jh_hw_pointer(h, origin.pointer, "index")},
                  NULL,
                  JH_PARTS(jh_span(name), jh_span(" has "),
                           jh_span(jh_hw_repr(h, jh_hw_integer(h, count))),
                           jh_span(" device pins")));
      return none;
    }
    return value;
  }
  const jb_json_tree_view_t id = jh_json_field(value, "id");
  if (!pin_valid(h, id)) {
    jh_hw_error(h, "JH-HW-PIN-RANGE", origin, NULL,
                JH_PARTS(jh_string_span(&h->target->id),
                         jh_span(" has no GPIO "),
                         jh_span(jh_hw_value_text(h, id))));
    return none;
  }
  return value;
}

static const char *board_node(jh_hw_t *h, const char *owner) {
  jb_string_t name = {0};
  (void)jb_string_init(&name, jb_heap_allocator());
  const char *result = NULL;
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&h->nodes); ++i) {
    const jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    if (!node->board)
      continue;
    jh_node_storage(&h->memory,
                    jh_board_device_owner(jh_span(node->path + 6), &name));
    if (strcmp(jb_string_data(&name), owner) == 0) {
      result = node->path;
      break;
    }
  }
  jb_string_destroy(&name);
  return result;
}

static bool exposed(jh_hw_t *h, jb_json_tree_view_t id) {
  const jb_json_tree_view_t pins =
      jh_json_field(jh_json_field(h->board->root, "gpio"), "exposedPins");
  const jb_json_tree_view_t values = jh_json_field(pins, "values");
  for (size_t i = 0; i < jh_json_count(values); ++i)
    if (jh_hw_equal(h, id, jh_json_item(values, i)))
      return true;
  int64_t number = 0;
  if (!jh_hw_i64(h, id, &number))
    return false;
  const jb_json_tree_view_t ranges = jh_json_field(pins, "ranges");
  for (size_t i = 0; i < jh_json_count(ranges); ++i) {
    const jb_json_tree_view_t range = jh_json_item(ranges, i);
    int64_t first = 0, last = 0;
    if (jh_json_int64(jh_json_field(range, "first"), &first) &&
        jh_json_int64(jh_json_field(range, "last"), &last) && number >= first &&
        number <= last)
      return true;
  }
  return false;
}

static bool usable(jh_hw_t *h, jb_json_tree_view_t endpoint,
                   jh_hw_origin_t origin, const char *owner,
                   const char *owner_signal) {
  if (!jh_hw_is(jh_json_field(endpoint, "domain"), "soc-gpio"))
    return true;
  const jb_json_tree_view_t pin = jh_json_field(endpoint, "id");
  const jb_json_tree_view_t table = reservations(h);
  /* Hard reservations take precedence regardless of registry member order. */
  for (unsigned hard_pass = 0; hard_pass < 2; ++hard_pass) {
    for (size_t i = 0; i < jh_json_count(table); ++i) {
      const jb_json_tree_view_t item = jh_json_item(table, i);
      const bool hard = jh_hw_is(jh_json_field(item, "strength"), "hard");
      if (hard != (hard_pass == 0))
        continue;
      const char *key = jh_hw_key(h, item);
      if (!hard && jh_hw_list_has(
                       jh_json_field(h->assembly, "releaseReservations"), key))
        continue;
      const jb_json_tree_view_t pins = jh_json_field(item, "pins");
      bool includes = false;
      for (size_t j = 0; !includes && j < jh_json_count(pins); ++j)
        includes =
            jh_hw_equal(h, pin, jh_json_field(jh_json_item(pins, j), "id"));
      if (!includes)
        continue;
      const char *reserved_owner = jh_hw_text(h, jh_json_field(item, "owner"));
      const char *device = board_node(h, reserved_owner);
      if (owner == NULL || device == NULL || strcmp(owner, device) != 0) {
        const char *detail =
            hard
                ? jh_hw_join(h, JH_PARTS(jh_span(" is hard-reserved by "),
                                         jh_span(reserved_owner), jh_span(" ("),
                                         jh_span(key), jh_span(")")))
                : jh_hw_join(h, JH_PARTS(jh_span(" is soft-reserved by "),
                                         jh_span(reserved_owner),
                                         jh_span("; release "), jh_span(key),
                                         jh_span(" first")));
        jh_hw_error(
            h, "JH-HW-RESERVATION", origin, NULL,
            JH_PARTS(jh_span(jh_hw_value_text(h, pin)), jh_span(detail)));
        return false;
      }
      if (hard)
        return true;
    }
  }
  if (exposed(h, pin))
    return true;
  const jh_hw_claim_t *earlier = previous_claim(h, endpoint, NULL);
  if (earlier != NULL && earlier->board) {
    const char *signal = strrchr(earlier->owner, '.');
    const char *device =
        signal == NULL
            ? ""
            : jh_hw_join(
                  h, JH_PARTS(((jb_json_span_t){
                         earlier->owner, (size_t)(signal - earlier->owner)})));
    signal = signal == NULL ? "" : signal + 1;
    if ((owner != NULL && strcmp(owner, device) == 0) ||
        (owner == NULL && owner_signal != NULL &&
         strcmp(owner_signal, signal) == 0))
      return true;
    jh_hw_error(
        h, "JH-HW-PIN-RANGE", origin, NULL,
        JH_PARTS(
            jh_string_span(&h->board->id), jh_span(" does not expose "),
            jh_span(jh_hw_value_text(h, pin)), jh_span("; only "),
            jh_span(earlier->owner),
            jh_span(" reaches it, so a net there needs an owner with signal "),
            jh_span(signal)));
  } else {
    jh_hw_error(h, "JH-HW-PIN-RANGE", origin, NULL,
                JH_PARTS(jh_string_span(&h->board->id),
                         jh_span(" does not expose "),
                         jh_span(jh_hw_value_text(h, pin))));
  }
  return false;
}

static jb_json_tree_view_t domains(jh_hw_t *h, bool device) {
  jb_json_tree_t *list = jh_hw_array(h);
  jh_hw_append(h, list, jh_hw_string(h, jh_span("soc-gpio")));
  if (device)
    jh_hw_append(h, list, jh_hw_string(h, jh_span("device-pin")));
  return jh_hw_root(list);
}

static void buses(jh_hw_t *h) {
  const jb_json_tree_view_t inputs = jh_json_field(h->assembly, "buses");
  jb_vector_t names = {0};
  jh_node_storage(&h->memory, jh_list_init(&names));
  jh_hw_keys(h, inputs, &names);
  jb_json_tree_t *output = jh_hw_object(h);
  const jb_json_tree_view_t allowed = domains(h, false);
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&names);
       ++i) {
    const char *name = jb_string_data(jh_list_at(&names, i));
    const jb_json_tree_view_t bus = jh_json_field(inputs, name);
    const jb_json_tree_view_t pins = jh_json_field(bus, "pins");
    const char *kind = jh_hw_text(h, jh_json_field(bus, "kind"));
    const jh_hw_bus_pins_t *spec = jh_hw_bus_pins(kind);
    const char *at =
        jh_hw_pointer(h, jh_hw_assembly_pointer(h, "buses", name), "pins");
    jb_vector_t pin_names = {0}, all = {0};
    jh_node_storage(&h->memory, jh_list_init(&pin_names));
    jh_node_storage(&h->memory, jh_list_init(&all));
    jh_hw_keys(h, pins, &pin_names);
    if (spec == NULL) {
      jh_node_storage(&h->memory, HAL_ESTATE);
    } else {
      for (size_t j = 0; j < spec->required_count; ++j) {
        const char *pin = spec->required[j];
        jh_node_storage(&h->memory, jh_list_push(&all, jh_span(pin)));
        if (jh_json_field(pins, pin).tree == NULL)
          jh_hw_error(
              h, "JH-HW-TRANSPORT",
              (jh_hw_origin_t){h->path, jh_hw_pointer(h, at, pin)}, NULL,
              JH_PARTS(jh_span("a "), jh_span(kind), jh_span(" bus states "),
                       jh_span(pin), jh_span(", null when unused")));
      }
      for (size_t j = 0; j < spec->optional_count; ++j)
        jh_node_storage(&h->memory,
                        jh_list_push(&all, jh_span(spec->optional[j])));
      jh_list_sort(&all);
    }
    jb_json_tree_t *endpoints = jh_hw_object(h);
    for (size_t j = 0;
         h->memory.status == HAL_OK && j < jb_vector_size(&pin_names); ++j) {
      const char *pin = jb_string_data(jh_list_at(&pin_names, j));
      const jb_json_tree_view_t value = jh_json_field(pins, pin);
      const jh_hw_origin_t origin = {h->path, jh_hw_pointer(h, at, pin)};
      if (!jh_list_contains(&all, jh_span(pin))) {
        jb_string_t repr = {0};
        (void)jb_string_init(&repr, jb_heap_allocator());
        jh_node_storage(&h->memory, jh_repr_list(&repr, &all));
        jh_hw_error(h, "JH-HW-TRANSPORT", origin, NULL,
                    JH_PARTS(jh_span("a "), jh_span(kind),
                             jh_span(" bus has the pins "),
                             jh_string_span(&repr)));
        jb_string_destroy(&repr);
      }
      if (jh_json_field(value, "net").tree != NULL) {
        jh_hw_error(
            h, "JH-HW-NET-OWNER", origin, NULL,
            JH_PARTS(jh_span(
                "a bus owns its lines; reference the bus instead of a net")));
        continue;
      }
      const jb_json_tree_view_t endpoint = normalize(h, value, allowed, origin);
      jh_hw_set(h, endpoints, pin, endpoint);
      if (jh_json_type(endpoint) != JB_JSON_NULL &&
          usable(h, endpoint, origin, NULL, NULL))
        claim(h, endpoint,
              jh_hw_join(h, JH_PARTS(jh_span("bus."), jh_span(name),
                                     jh_span("."), jh_span(pin))),
              origin, false, NULL);
    }
    jb_json_tree_t *resolved = jh_hw_copy(h, bus);
    jh_hw_set(h, resolved, "pins", jh_hw_root(endpoints));
    jh_hw_set(h, output, name, jh_hw_root(resolved));
    jh_list_destroy(&all);
    jh_list_destroy(&pin_names);
  }
  h->buses = jh_hw_root(output);
  jh_list_destroy(&names);
}

static jh_hw_net_t *net_named(jh_hw_t *h, const char *name) {
  for (size_t i = 0; i < jb_vector_size(&h->net_states); ++i) {
    jh_hw_net_t *net = jb_vector_at(&h->net_states, i);
    if (strcmp(net->name, name) == 0)
      return net;
  }
  return NULL;
}

static void net_endpoints(jh_hw_t *h) {
  const jb_json_tree_view_t inputs = jh_json_field(h->assembly, "nets");
  jb_vector_t names = {0};
  jh_node_storage(&h->memory, jh_list_init(&names));
  jh_hw_keys(h, inputs, &names);
  const jb_json_tree_view_t allowed = domains(h, true);
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&names);
       ++i) {
    const char *name =
        jh_hw_join(h, JH_PARTS(jh_string_span(jh_list_at(&names, i))));
    const jb_json_tree_view_t input = jh_json_field(inputs, name);
    const jh_hw_origin_t origin = {
        h->path,
        jh_hw_pointer(h, jh_hw_assembly_pointer(h, "nets", name), "endpoint")};
    const jb_json_tree_view_t endpoint =
        normalize(h, jh_json_field(input, "endpoint"), allowed, origin);
    if (jh_json_type(endpoint) == JB_JSON_NULL)
      continue;
    const char *owner = jh_hw_text(h, jh_json_field(input, "owner"));
    const char *dot = strrchr(owner, '.');
    const char *owner_node =
        dot == NULL ? ""
                    : jh_hw_join(h, JH_PARTS(((jb_json_span_t){
                                        owner, (size_t)(dot - owner)})));
    const char *board_owner =
        strncmp(owner, "board.", 6) == 0 ? owner_node : NULL;
    if (!usable(h, endpoint, origin, board_owner, dot == NULL ? "" : dot + 1))
      continue;
    const jh_hw_claim_t *previous = previous_claim(h, endpoint, NULL);
    const jh_hw_net_t net = {
        name, owner,
        previous != NULL && previous->board ? previous->owner : NULL, endpoint};
    jh_node_storage(&h->memory, jb_vector_append(&h->net_states, &net, 1));
    claim(h, endpoint, jh_hw_join(h, JH_PARTS(jh_span("net."), jh_span(name))),
          origin, false, name);
  }
  jh_list_destroy(&names);
}

static void node_endpoints(jh_hw_t *h) {
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&h->nodes); ++i) {
    jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    if (!node->present || node->board)
      continue;
    for (size_t j = 0; j < jb_vector_size(&node->pins); ++j) {
      jh_hw_pin_t *pin = jb_vector_at(&node->pins, j);
      const jb_json_tree_view_t name = jh_json_field(pin->value, "net");
      if (name.tree != NULL) {
        const jh_hw_net_t *net = net_named(h, jh_hw_text(h, name));
        if (net != NULL) {
          pin->endpoint = net->endpoint;
          pin->net = net->name;
        }
        continue;
      }
      const char *key = jh_hw_join(
          h, JH_PARTS(jh_span(node->path), jh_span("."), jh_span(pin->name)));
      if (pin->site == NULL || strcmp(pin->site, key) != 0)
        continue;
      const jb_json_tree_view_t spec =
          jh_json_field(jh_json_field(node->type, "signals"), pin->name);
      pin->endpoint =
          normalize(h, pin->value, jh_json_field(spec, "domains"), pin->origin);
      if (jh_json_type(pin->endpoint) != JB_JSON_NULL &&
          usable(h, pin->endpoint, pin->origin, NULL, NULL))
        claim(h, pin->endpoint, key, pin->origin, false, NULL);
    }
  }
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&h->nodes); ++i) {
    jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    for (size_t j = 0; j < jb_vector_size(&node->pins); ++j) {
      jh_hw_pin_t *pin = jb_vector_at(&node->pins, j);
      if (pin->site == NULL || pin->endpoint.tree != NULL)
        continue;
      const char *signal = NULL;
      jh_hw_node_t *source = jh_hw_parent(h, pin->site, &signal);
      jh_hw_pin_t *original = source == NULL ? NULL : jh_hw_pin(source, signal);
      if (original != NULL && original != pin) {
        pin->endpoint = original->endpoint;
        pin->net = original->net;
      }
    }
  }
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&h->nodes); ++i) {
    jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    if (node->board)
      continue;
    const char *compatible = jb_string_data(&node->binding->compatible);
    for (size_t j = 0; j < jb_vector_size(&node->pins); ++j) {
      const jh_hw_pin_t *pin = jb_vector_const_at(&node->pins, j);
      if (jh_json_type(pin->endpoint) == JB_JSON_NULL)
        continue;
      const jb_json_tree_view_t spec =
          jh_json_field(jh_json_field(node->type, "signals"), pin->name);
      const jb_json_tree_view_t allowed = jh_json_field(spec, "domains");
      const char *domain =
          jh_hw_text(h, jh_json_field(pin->endpoint, "domain"));
      if (!jh_hw_list_has(allowed, domain)) {
        jh_hw_error(h, "JH-HW-PIN-RANGE", pin->origin, NULL,
                    JH_PARTS(jh_span(node->path), jh_span("."),
                             jh_span(pin->name), jh_span(" takes "),
                             jh_span(jh_hw_repr(h, allowed)),
                             jh_span(", not a "), jh_span(domain),
                             jh_span(" endpoint")));
        continue;
      }
      int64_t encoded = 0;
      if ((strcmp(compatible, "jaszczurhal,can-transceiver") == 0 ||
           strcmp(compatible, "microchip,mcp2562fd") == 0) &&
          (strcmp(pin->name, "rx") == 0 || strcmp(pin->name, "tx") == 0) &&
          jh_board_encode_pin(pin->endpoint, &encoded) && encoded == 0)
        jh_hw_error(
            h, "JH-HW-PIN-RANGE", pin->origin, NULL,
            JH_PARTS(jh_span("the HAL reads "), jh_span(pin->name),
                     jh_span(" pin 0 as the default pin of the controller")));
    }
  }
}

static jh_hw_pin_t *signal_pin(jh_hw_t *h, const char *member,
                               jh_hw_node_t **node) {
  const char *name = NULL;
  *node = jh_hw_parent(h, member, &name);
  return *node == NULL ? NULL : jh_hw_pin(*node, name);
}

static void net_owners(jh_hw_t *h) {
  typedef struct {
    jh_hw_node_t *node;
    jh_hw_pin_t *pin;
  } output_t;
  jb_json_tree_t *output = jh_hw_object(h);
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&h->net_states); ++i) {
    const jh_hw_net_t *net = jb_vector_const_at(&h->net_states, i);
    jh_hw_node_t *owner_node = NULL;
    const jh_hw_pin_t *owner = signal_pin(h, net->owner, &owner_node);
    if (owner == NULL ||
        !((owner->net != NULL && strcmp(owner->net, net->name) == 0) ||
          (net->board_signal != NULL &&
           strcmp(net->owner, net->board_signal) == 0))) {
      jh_hw_error(
          h, "JH-HW-NET-OWNER",
          (jh_hw_origin_t){
              h->path,
              jh_hw_pointer(h, jh_hw_assembly_pointer(h, "nets", net->name),
                            "owner")},
          NULL,
          JH_PARTS(jh_span(net->owner), jh_span(" is not a signal wired to "),
                   jh_span(net->name)));
      continue;
    }
    jb_vector_t outputs = {0};
    jh_node_storage(&h->memory, jb_vector_init(&outputs, sizeof(output_t),
                                               jb_heap_allocator()));
    output_t first = {0};
    const char *first_source = NULL;
    for (size_t n = 0;
         h->memory.status == HAL_OK && n < jb_vector_size(&h->nodes); ++n) {
      jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, n);
      for (size_t j = 0; j < jb_vector_size(&node->pins); ++j) {
        jh_hw_pin_t *pin = jb_vector_at(&node->pins, j);
        const char *key = jh_hw_join(
            h, JH_PARTS(jh_span(node->path), jh_span("."), jh_span(pin->name)));
        if (!((pin->net != NULL && strcmp(pin->net, net->name) == 0) ||
              (net->board_signal != NULL &&
               strcmp(key, net->board_signal) == 0)))
          continue;
        pin->owner = net->owner;
        pin->net = net->name;
        pin->endpoint = net->endpoint;
        const jb_json_tree_view_t spec =
            jh_json_field(jh_json_field(node->type, "signals"), pin->name);
        if (jh_hw_is(jh_json_field(spec, "direction"), "input") ||
            jh_hw_property(node, "activeLow") == NULL)
          continue;
        const char *source = jh_hw_source_key(h, pin->origin.source);
        int order = first.pin == NULL ? -1 : strcmp(source, first_source);
        if (order == 0)
          order = jh_json_pointer_compare(jh_span(pin->origin.pointer),
                                          jh_span(first.pin->origin.pointer));
        const output_t member = {node, pin};
        if (order < 0) {
          first = member;
          first_source = source;
        }
        jh_node_storage(&h->memory, jb_vector_append(&outputs, &member, 1));
      }
    }
    for (size_t j = 0; h->memory.status == HAL_OK && first.node != NULL &&
                       j < jb_vector_size(&outputs);
         ++j) {
      const output_t *member = jb_vector_const_at(&outputs, j);
      jh_hw_pin_t *pin = member->pin;
      jh_hw_node_t *node = member->node;
      if (!jh_hw_equal(h, jh_hw_property(node, "activeLow")->value,
                       jh_hw_property(first.node, "activeLow")->value))
        jh_hw_error(
            h, "JH-HW-NET-OWNER", pin->origin, first.pin->origin.pointer,
            JH_PARTS(jh_span(node->path), jh_span("."), jh_span(pin->name),
                     jh_span(" and "), jh_span(first.node->path), jh_span("."),
                     jh_span(first.pin->name), jh_span(" share "),
                     jh_span(net->name), jh_span(" with opposite activeLow")));
    }
    jb_vector_destroy(&outputs);
    jb_json_tree_t *entry = jh_hw_object(h);
    jh_hw_set(h, entry, "endpoint", net->endpoint);
    jh_hw_set(h, entry, "owner", jh_hw_string(h, jh_span(net->owner)));
    jh_hw_set(h, output, net->name, jh_hw_root(entry));
  }
  h->nets = jh_hw_root(output);
}

void jh_hw_wiring(jh_hw_t *h) {
  releases(h);
  if (jh_hw_phase(h) != HAL_OK)
    return;
  board_claims(h);
  buses(h);
  net_endpoints(h);
  node_endpoints(h);
  net_owners(h);
  jh_hw_transport(h);
}
