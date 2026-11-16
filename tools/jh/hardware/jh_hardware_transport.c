#include "jh_hardware_internal.h"
#include <stdio.h>
#include <string.h>

typedef struct {
  const char *kind;
  int64_t index;
  jh_hw_origin_t origin;
} controller_t;

static void take_controller(jh_hw_t *h, jb_vector_t *used, const char *kind,
                            jb_json_tree_view_t value, jh_hw_origin_t origin) {
  int64_t index = 0;
  if (!jh_hw_i64(h, value, &index)) {
    jh_node_storage(&h->memory, HAL_ESTATE);
    return;
  }
  const int64_t count =
      strcmp(kind, "fdcan") == 0
          ? jh_board_can_controller_count(jh_string_span(&h->target->id))
          : (jh_hw_bus_pins(kind) != NULL ? 2 : 0);
  if (index >= count) {
    jh_hw_error(h, "JH-HW-CONTROLLER", origin, NULL,
                JH_PARTS(jh_string_span(&h->target->id), jh_span(" has "),
                         jh_span(jh_hw_repr(h, jh_hw_integer(h, count))),
                         jh_span(" "), jh_span(kind), jh_span(" controllers")));
    return;
  }
  for (size_t i = 0; i < jb_vector_size(used); ++i) {
    const controller_t *earlier = jb_vector_const_at(used, i);
    if (index == earlier->index && strcmp(kind, earlier->kind) == 0) {
      jh_hw_error(h, "JH-HW-CONTROLLER", origin, earlier->origin.pointer,
                  JH_PARTS(jh_span(kind), jh_span(" "),
                           jh_span(jh_hw_repr(h, value)),
                           jh_span(" is already used by "),
                           jh_span(earlier->origin.pointer)));
      return;
    }
  }
  const controller_t controller = {kind, index, origin};
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jb_vector_append(used, &controller, 1));
}

static void controllers(jh_hw_t *h) {
  jb_vector_t used = {0};
  jh_node_storage(&h->memory, jb_vector_init(&used, sizeof(controller_t),
                                             jb_heap_allocator()));
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&h->nodes); ++i) {
    const jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    if (node->board &&
        jh_hw_is(jh_json_field(node->board_facts, "kind"), "bus-device")) {
      const jb_json_tree_view_t bus = jh_json_field(node->board_facts, "bus");
      take_controller(
          h, &used, jh_hw_text(h, jh_json_field(bus, "kind")),
          jh_json_field(bus, "index"),
          (jh_hw_origin_t){node->origin.source,
                           jh_hw_pointer(h, node->origin.pointer, "bus")});
    }
  }
  jb_vector_t names = {0};
  jh_node_storage(&h->memory, jh_list_init(&names));
  const jb_json_tree_view_t buses = jh_json_field(h->assembly, "buses");
  jh_hw_keys(h, buses, &names);
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&names);
       ++i) {
    const char *name = jb_string_data(jh_list_at(&names, i));
    const jb_json_tree_view_t bus = jh_json_field(buses, name);
    take_controller(
        h, &used, jh_hw_text(h, jh_json_field(bus, "kind")),
        jh_json_field(bus, "index"),
        (jh_hw_origin_t){
            h->path, jh_hw_pointer(h, jh_hw_assembly_pointer(h, "buses", name),
                                   "index")});
  }
  jh_list_destroy(&names);
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&h->nodes); ++i) {
    jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    if (!node->present || node->board)
      continue;
    const char *at = jh_hw_node_pointer(h, node->path);
    const jb_json_tree_view_t controller =
        jh_json_field(node->entry, "controller");
    const jb_json_tree_view_t wanted = jh_json_field(node->type, "controller");
    if (jh_json_type(controller) != JB_JSON_NULL) {
      const char *kind = jh_hw_text(h, jh_json_field(controller, "kind"));
      const jh_hw_origin_t origin = {h->path,
                                     jh_hw_pointer(h, at, "controller")};
      if (!jh_hw_is(jh_json_field(wanted, "kind"), kind))
        jh_hw_error(h, "JH-HW-CONTROLLER", origin, NULL,
                    JH_PARTS(jh_string_span(&node->binding->compatible),
                             jh_span(" takes no "), jh_span(kind),
                             jh_span(" controller")));
      else
        take_controller(h, &used, kind, jh_json_field(controller, "index"),
                        origin);
    }
    const jb_json_tree_view_t address = jh_json_field(node->entry, "address");
    if (jh_json_type(address) != JB_JSON_NULL) {
      const jh_hw_origin_t origin = {h->path, jh_hw_pointer(h, at, "address")};
      if (!jh_hw_is(jh_json_field(jh_json_field(node->type, "bus"), "kind"),
                    "i2c"))
        jh_hw_error(h, "JH-HW-ADDRESS", origin, NULL,
                    JH_PARTS(jh_string_span(&node->binding->compatible),
                             jh_span(" is not an I2C device")));
      uint32_t number = 0;
      int64_t integral = 0;
      bool valid = jh_json_hardware_id(address, 0x7f, &number);
      if (!valid && jh_hw_i64(h, address, &integral) && integral >= 0 &&
          integral <= 0x7f) {
        number = (uint32_t)integral;
        valid = true;
      }
      if (!valid) {
        jh_hw_error(h, "JH-HW-ADDRESS", origin, NULL,
                    JH_PARTS(jh_span("an I2C address from 0x00 through 0x7f")));
      } else {
        jb_json_tree_t *entry = jh_hw_copy(h, node->entry);
        jh_hw_set(h, entry, "address", jh_hw_integer(h, number));
        node->entry = jh_hw_root(entry);
      }
    }
  }
  jb_vector_destroy(&used);
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&h->nodes); ++i) {
    const jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    const jb_json_tree_view_t bus = jh_json_field(node->entry, "bus");
    int64_t address = 0;
    if (!node->present || node->board || jh_json_type(bus) == JB_JSON_NULL ||
        !jh_hw_i64(h, jh_json_field(node->entry, "address"), &address))
      continue;
    for (size_t j = 0; j < i; ++j) {
      const jh_hw_node_t *earlier =
          *(jh_hw_node_t **)jb_vector_at(&h->nodes, j);
      int64_t prior = 0;
      if (!earlier->present || earlier->board ||
          !jh_hw_i64(h, jh_json_field(earlier->entry, "address"), &prior) ||
          prior != address ||
          !jh_hw_equal(h, bus, jh_json_field(earlier->entry, "bus")))
        continue;
      char hex[16];
      (void)snprintf(hex, sizeof hex, "0x%02x", (unsigned)address);
      jh_hw_error(
          h, "JH-HW-ADDRESS",
          (jh_hw_origin_t){
              h->path,
              jh_hw_pointer(h, jh_hw_node_pointer(h, node->path), "address")},
          jh_hw_pointer(h, jh_hw_node_pointer(h, earlier->path), "address"),
          JH_PARTS(jh_span("address "), jh_span(hex), jh_span(" on "),
                   jh_span(jh_hw_text(h, bus)), jh_span(" is already used")));
      break;
    }
  }
}

const char *jh_hw_uart_problem(jh_hw_t *h, const char *target,
                               jb_json_tree_view_t frame) {
  if (strcmp(target, "stm32g474") != 0)
    return NULL;
  int64_t bits = 0;
  (void)jh_hw_i64(h, jh_json_field(frame, "dataBits"), &bits);
  return bits == 5
             ? "the STM32G474 USART has no 5-bit frames"
             : (bits == 6 && jh_hw_is(jh_json_field(frame, "parity"), "none")
                    ? "the STM32G474 USART needs parity with 6 data bits"
                    : NULL);
}

static void uart_frames(jh_hw_t *h) {
  for (size_t i = 0; i < jh_json_count(h->buses); ++i) {
    const jb_json_tree_view_t bus = jh_json_item(h->buses, i);
    if (!jh_hw_is(jh_json_field(bus, "kind"), "uart"))
      continue;
    const jb_json_tree_view_t frame = jh_json_field(bus, "frame");
    const char *problem =
        jh_hw_uart_problem(h, jb_string_data(&h->target->id), frame);
    if (problem != NULL)
      jh_hw_error(
          h, "JH-HW-TRANSPORT",
          (jh_hw_origin_t){
              h->path,
              jh_hw_pointer(
                  h,
                  jh_hw_pointer(
                      h, jh_hw_assembly_pointer(h, "buses", jh_hw_key(h, bus)),
                      "frame"),
                  "dataBits")},
          NULL, JH_PARTS(jh_span(problem)));
  }
}

typedef struct {
  const char *name, *pointer;
  jb_json_tree_view_t value;
} setting_t;

static bool settings(jh_hw_t *h, const jh_hw_node_t *node,
                     jb_json_tree_view_t bus, const char *bus_name,
                     setting_t *out, size_t *count) {
  const char *at =
      jh_hw_pointer(h, jh_hw_node_pointer(h, node->path), "transport");
  const char *bus_at = jh_hw_assembly_pointer(h, "buses", bus_name);
  const jb_json_tree_view_t transport = jh_json_field(node->entry, "transport");
  *count = 0;
  if (jh_hw_is(jh_json_field(bus, "kind"), "uart")) {
    if (jh_json_count(transport) == 0)
      return true;
    jh_hw_error(
        h, "JH-HW-TRANSPORT", (jh_hw_origin_t){h->path, at}, NULL,
        JH_PARTS(jh_span("UART consumers use the bus baud rate and frame")));
    return false;
  }
  out[0] = (setting_t){"frequencyHz", jh_hw_pointer(h, bus_at, "frequencyHz"),
                       jh_json_field(bus, "frequencyHz")};
  *count = 1;
  if (jh_hw_is(jh_json_field(bus, "kind"), "i2c")) {
    static const char *const invalid[] = {"mode", "bitOrder"};
    for (size_t i = 0; i < COUNTOF(invalid); ++i)
      if (jh_json_field(transport, invalid[i]).tree != NULL) {
        jh_hw_error(h, "JH-HW-TRANSPORT",
                    (jh_hw_origin_t){h->path, jh_hw_pointer(h, at, invalid[i])},
                    NULL, JH_PARTS(jh_span("I2C has no SPI settings")));
        return false;
      }
    const jb_json_tree_view_t rate = jh_json_field(transport, "frequencyHz");
    if (rate.tree != NULL && !jh_hw_equal(h, rate, out[0].value)) {
      jh_hw_error(
          h, "JH-HW-TRANSPORT",
          (jh_hw_origin_t){h->path, jh_hw_pointer(h, at, "frequencyHz")}, NULL,
          JH_PARTS(jh_span("every I2C consumer runs at the bus rate "),
                   jh_span(jh_hw_repr(h, out[0].value)), jh_span(" Hz")));
      return false;
    }
    return true;
  }
  out[1] = (setting_t){"mode", jh_hw_pointer(h, bus_at, "mode"),
                       jh_json_field(bus, "mode")};
  out[2] = (setting_t){"bitOrder", jh_hw_pointer(h, bus_at, "bitOrder"),
                       jh_json_field(bus, "bitOrder")};
  if (out[1].value.tree == NULL)
    out[1].value = jh_hw_integer(h, 0);
  if (out[2].value.tree == NULL)
    out[2].value = jh_hw_string(h, jh_span("msb"));
  *count = 3;
  for (size_t i = 0; i < *count; ++i) {
    const jb_json_tree_view_t override = jh_json_field(transport, out[i].name);
    if (override.tree != NULL) {
      out[i].value = override;
      out[i].pointer = jh_hw_pointer(h, at, out[i].name);
    }
  }
  return true;
}

const char *jh_hw_transport_problem(jh_hw_t *h, const char *renderer,
                                    int64_t rate, int64_t mode,
                                    jb_json_tree_view_t order, size_t *field) {
  const jh_hw_renderer_t *info = jh_hw_renderer_info(renderer);
  if (info == NULL || info->bus_lines == 0)
    return NULL;
  *field = 0;
  if (info->fixed_frequency != 0 && rate != info->fixed_frequency)
    return jh_hw_join(
        h,
        JH_PARTS(
            jh_span("the "), jh_span(renderer), jh_span(" driver runs SPI at "),
            jh_span(jh_hw_repr(h, jh_hw_integer(h, info->fixed_frequency))),
            jh_span(" Hz only")));
  if (mode != 0) {
    *field = 1;
    return jh_hw_join(h, JH_PARTS(jh_span("the "), jh_span(renderer),
                                  jh_span(" driver uses SPI mode 0 only")));
  }
  if (order.tree != NULL && !jh_hw_is(order, "msb")) {
    *field = 2;
    return jh_hw_join(h, JH_PARTS(jh_span("the "), jh_span(renderer),
                                  jh_span(" driver sends MSB first only")));
  }
  return NULL;
}

static bool supported(jh_hw_t *h, jh_hw_node_t *node, const setting_t *values,
                      size_t count) {
  if (count == 0)
    return true;
  int64_t rate = 0, maximum = 0;
  (void)jh_hw_i64(h, values[0].value, &rate);
  const jb_json_tree_view_t limit =
      jh_json_field(jh_json_field(node->type, "bus"), "maximumFrequencyHz");
  if (jh_hw_i64(h, limit, &maximum) && rate > maximum) {
    jh_hw_error(h, "JH-HW-TRANSPORT",
                (jh_hw_origin_t){h->path, values[0].pointer}, NULL,
                JH_PARTS(jh_string_span(&node->binding->compatible),
                         jh_span(" takes at most "),
                         jh_span(jh_hw_repr(h, limit)), jh_span(" Hz")));
    return false;
  }
  const char *driver =
      jh_hw_renderer(jb_string_data(&node->binding->compatible));
  const char *problem = NULL;
  size_t field = 0;
  if (driver != NULL && strcmp(driver, "fdcan") != 0 && count == 3) {
    int64_t mode = 0;
    (void)jh_hw_i64(h, values[1].value, &mode);
    problem =
        jh_hw_transport_problem(h, driver, rate, mode, values[2].value, &field);
    if (problem == NULL && strcmp(driver, "sx126x") == 0) {
      const jh_hw_property_t *property = jh_hw_property(node, "maxSpiClockHz");
      if (property != NULL && jh_hw_i64(h, property->value, &maximum) &&
          rate > maximum)
        problem = jh_hw_join(
            h,
            JH_PARTS(jh_span("the radio takes at most maxSpiClockHz = "),
                     jh_span(jh_hw_repr(h, property->value)), jh_span(" Hz")));
    }
  }
  if (problem != NULL)
    jh_hw_error(h, "JH-HW-TRANSPORT",
                (jh_hw_origin_t){h->path, values[field].pointer}, NULL,
                JH_PARTS(jh_span(problem)));
  return problem == NULL;
}

void jh_hw_transport(jh_hw_t *h) {
  controllers(h);
  uart_frames(h);
  for (size_t i = 0;
       h->memory.status == HAL_OK && i < jb_vector_size(&h->nodes); ++i) {
    jh_hw_node_t *node = *(jh_hw_node_t **)jb_vector_at(&h->nodes, i);
    if (!node->present || node->board)
      continue;
    const jb_json_tree_view_t bus_value = jh_json_field(node->entry, "bus");
    const char *at = jh_hw_node_pointer(h, node->path);
    if (jh_json_type(bus_value) == JB_JSON_NULL) {
      if (jh_json_type(jh_json_field(node->entry, "transport")) != JB_JSON_NULL)
        jh_hw_error(
            h, "JH-HW-TRANSPORT",
            (jh_hw_origin_t){h->path, jh_hw_pointer(h, at, "transport")}, NULL,
            JH_PARTS(jh_span("a node without a bus has no transport")));
      continue;
    }
    const char *bus_name = jh_hw_text(h, bus_value);
    const jb_json_tree_view_t bus = jh_json_field(h->buses, bus_name);
    const char *kind = jh_hw_text(h, jh_json_field(bus, "kind"));
    if (!jh_hw_is(jh_json_field(jh_json_field(node->type, "bus"), "kind"),
                  kind)) {
      jh_hw_error(h, "JH-HW-TRANSPORT",
                  (jh_hw_origin_t){h->path, jh_hw_pointer(h, at, "bus")}, NULL,
                  JH_PARTS(jh_string_span(&node->binding->compatible),
                           jh_span(" cannot use the "), jh_span(kind),
                           jh_span(" bus "), jh_span(bus_name)));
      continue;
    }
    setting_t values[3];
    size_t count = 0;
    if (!settings(h, node, bus, bus_name, values, &count) ||
        !supported(h, node, values, count))
      continue;
    jb_json_tree_t *effective = jh_hw_object(h);
    for (size_t j = 0; j < count; ++j)
      jh_hw_set(h, effective, values[j].name, values[j].value);
    node->transport = jh_hw_root(effective);
    const jh_hw_pin_t *cs = jh_hw_pin(node, "cs");
    if (strcmp(kind, "spi") != 0 || cs == NULL ||
        jh_json_type(cs->endpoint) == JB_JSON_NULL)
      continue;
    /* The old checker retains the most recent chip select claim. */
    for (size_t j = i; j > 0; --j) {
      jh_hw_node_t *previous = *(jh_hw_node_t **)jb_vector_at(&h->nodes, j - 1);
      const jh_hw_pin_t *other = jh_hw_pin(previous, "cs");
      if (!previous->present || previous->board ||
          previous->transport.tree == NULL || other == NULL ||
          !jh_hw_equal(h, bus_value, jh_json_field(previous->entry, "bus")) ||
          !jh_hw_endpoint_equal(h, cs->endpoint, other->endpoint))
        continue;
      jh_hw_error(h, "JH-HW-PIN-CONFLICT", cs->origin, other->origin.pointer,
                  JH_PARTS(jh_span(node->path),
                           jh_span(" shares its chip select on "),
                           jh_span(bus_name)));
      break;
    }
  }
}
