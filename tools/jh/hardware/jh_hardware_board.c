#include "jh_hardware_internal.h"
#include <string.h>

bool jh_hw_owned(const char *name) {
  static const char *const prefixes[] = {
      "JH_HW_", "HAL_BOARD_", "HAL_STM32G474_CLOCK_", "HAL_CYW43_PIN_"};
  if (strcmp(name, "HAL_LED_BUILTIN") == 0)
    return true;
  for (size_t i = 0; i < COUNTOF(prefixes); ++i) {
    const size_t length = strlen(prefixes[i]);
    if (strncmp(name, prefixes[i], length) != 0 || name[length] == '\0')
      continue;
    const char *p = name + length;
    for (; *p; ++p)
      if (!jh_ascii_is_lower(*p) && !jh_ascii_is_upper(*p) &&
          !jh_ascii_is_digit(*p) && *p != '_')
        break;
    if (*p == '\0')
      return true;
  }
  return false;
}

static const char *component_slot(jh_hw_t *h, const char *id) {
  for (size_t i = 0; i < jb_vector_size(&h->registry->boards.components); ++i) {
    const jh_board_component_t *component =
        jb_vector_const_at(&h->registry->boards.components, i);
    if (strcmp(id, jb_string_data(&component->id)) == 0)
      return jb_string_data(&component->slot);
  }
  return "";
}

static const char *pin_macro(jh_hw_t *h, const char *path, const char *signal) {
  return jh_hw_join(h, JH_PARTS(jh_span("JH_HW_NODE_"),
                                jh_span(jh_hw_token(h, path)), jh_span("_PIN_"),
                                jh_span(jh_hw_token(h, signal))));
}

static jb_json_tree_view_t macro_endpoint(jh_hw_t *h,
                                          jb_json_tree_view_t endpoint,
                                          const char *path,
                                          const char *signal) {
  jb_json_tree_t *result = jh_hw_copy(h, endpoint);
  jh_hw_set(h, result, "macro",
            jh_hw_string(h, jh_span(pin_macro(h, path, signal))));
  return jh_hw_root(result);
}

hal_status_t jh_hardware_board_json(jh_hardware_t *hardware, jb_string_t *out,
                                    jh_failure_t *failure) {
  if (hardware == NULL || hardware->state == NULL || out == NULL ||
      failure == NULL)
    return jh_fail_arguments(failure);
  jh_hw_t *h = hardware->state;
  h->failure = h->memory.failure = failure;
  if (h->model.tree == NULL)
    return jh_fail_text(failure, HAL_ESTATE, 3,
                        "hardware has not been resolved");
  jb_vector_t definitions = {0}, components = {0};
  jh_node_storage(&h->memory, jh_list_init(&definitions));
  jh_node_storage(&h->memory, jh_list_init(&components));
  jb_json_tree_t *result = jh_hw_object(h);
  const jb_json_tree_view_t board =
      jh_hw_assembly_board(h, h->model, &definitions, &components);
  jh_hw_set(h, result, "board", board);
  jb_json_tree_t *header = jh_hw_array(h), *compiler = jh_hw_array(h),
                 *names = jh_hw_array(h);
  for (size_t i = 0; i < jb_vector_size(&definitions); ++i) {
    const jb_json_span_t definition =
        jh_string_span(jh_list_at(&definitions, i));
    const jb_json_tree_view_t text = jh_hw_string(h, definition);
    jh_hw_append(h, header, text);
    const char *name = jh_hw_definition_name(h, definition);
    if (!jh_hw_owned(name))
      jh_hw_append(h, compiler, text);
  }
  for (size_t i = 0; i < jb_vector_size(&components); ++i)
    jh_hw_append(h, names,
                 jh_hw_string(h, jh_string_span(jh_list_at(&components, i))));
  jh_hw_set(h, result, "headerDefinitions", jh_hw_root(header));
  jh_hw_set(h, result, "compilerDefinitions", jh_hw_root(compiler));
  jh_hw_set(h, result, "components", jh_hw_root(names));
  jb_json_tree_t *pins = jh_hw_object(h);
  const jb_json_tree_view_t nodes =
      jh_json_field(jh_json_field(h->model, "hardware"), "nodes");
  for (size_t i = 0; i < jh_json_count(nodes); ++i) {
    const jb_json_tree_view_t node = jh_json_item(nodes, i);
    const jb_json_tree_view_t signals = jh_json_field(node, "signals");
    for (size_t j = 0; j < jh_json_count(signals); ++j) {
      const jb_json_tree_view_t signal = jh_json_item(signals, j);
      int64_t encoded = 0;
      if (!jh_board_encode_pin(jh_json_field(signal, "endpoint"), &encoded))
        continue;
      const char *key =
          jh_hw_join(h, JH_PARTS(jh_span(jh_hw_key(h, node)), jh_span("."),
                                 jh_span(jh_hw_key(h, signal))));
      jh_hw_set(h, pins, key, jh_hw_integer(h, encoded));
    }
  }
  jh_hw_set(h, result, "pinNumbers", jh_hw_root(pins));
  if (jh_hw_phase(h) == HAL_OK)
    jh_node_storage(&h->memory, jh_hw_format(jh_hw_root(result), 2, out));
  jh_list_destroy(&components);
  jh_list_destroy(&definitions);
  return h->memory.status;
}

static jb_json_tree_view_t can_channels(jh_hw_t *h,
                                        jb_json_tree_view_t hardware,
                                        jb_json_tree_view_t board) {
  jb_json_tree_t *channels = jh_hw_array(h);
  const jb_json_tree_view_t old =
      jh_json_field(jh_json_field(board, "can"), "channels");
  for (size_t i = 0; i < jh_json_count(old); ++i)
    jh_hw_append(h, channels, jh_json_item(old, i));
  const jb_json_tree_view_t nodes = jh_json_field(hardware, "nodes");
  jb_vector_t keys = {0};
  jh_node_storage(&h->memory, jh_list_init(&keys));
  jh_hw_keys(h, nodes, &keys);
  const int64_t controllers =
      jh_board_can_controller_count(jh_string_span(&h->target->id));
  for (int64_t index = 0; h->memory.status == HAL_OK && index < controllers;
       ++index) {
    for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&keys);
         ++i) {
      const char *path = jb_string_data(jh_list_at(&keys, i));
      const jb_json_tree_view_t node = jh_json_field(nodes, path);
      const char *compatible = jh_hw_text(h, jh_json_field(node, "compatible"));
      const char *renderer = jh_hw_renderer(compatible);
      int64_t controller = -1;
      (void)jh_hw_i64(h,
                      jh_json_field(jh_json_field(node, "controller"), "index"),
                      &controller);
      if (renderer == NULL || strcmp(renderer, "fdcan") != 0 ||
          !jh_hw_bool(jh_json_field(node, "present")) || controller != index ||
          jh_hw_initializer(h, path, node, hardware).c == NULL)
        continue;
      jb_json_tree_t *channel = jh_hw_object(h);
      jh_hw_set(h, channel, "controller",
                jh_hw_string(h, jh_span("stm32g474-fdcan")));
      jh_hw_set(h, channel, "instance", jh_hw_integer(h, controller + 1));
      static const char *const signals[] = {"rx", "tx", "standby"};
      for (size_t j = 0; j < COUNTOF(signals); ++j) {
        const jb_json_tree_view_t endpoint = jh_json_field(
            jh_json_field(jh_json_field(node, "signals"), signals[j]),
            "endpoint");
        if (jh_json_type(endpoint) == JB_JSON_OBJECT)
          jh_hw_set(h, channel, signals[j],
                    macro_endpoint(h, endpoint, path, signals[j]));
      }
      static const char *const properties[] = {"standbyActiveHigh",
                                               "maxBitrateHz"};
      for (size_t j = 0; j < COUNTOF(properties); ++j)
        jh_hw_set(h, channel, properties[j],
                  jh_json_field(jh_json_field(jh_json_field(node, "properties"),
                                              properties[j]),
                                "value"));
      jh_hw_set(
          h, channel, "transceiver",
          jh_hw_string(h, jh_span(strcmp(compatible, "microchip,mcp2562fd") == 0
                                      ? "mcp2562fd"
                                      : "generic")));
      jh_hw_append(h, channels, jh_hw_root(channel));
    }
  }
  jh_list_destroy(&keys);
  jb_json_tree_t *can = jh_hw_object(h);
  jh_hw_set(h, can, "channels", jh_hw_root(channels));
  return jh_hw_root(can);
}

jb_json_tree_view_t jh_hw_assembly_board(jh_hw_t *h, jb_json_tree_view_t model,
                                         jb_vector_t *definitions,
                                         jb_vector_t *components) {
  const jb_json_tree_view_t hardware = jh_json_field(model, "hardware");
  const jb_json_tree_view_t capabilities =
      jh_json_field(hardware, "capabilities");
  const jb_json_tree_view_t selection = jh_json_field(model, "selection");
  h->target = jh_board_registry_find(
      &h->registry->boards.targets,
      jh_span(jh_hw_text(h, jh_json_field(selection, "target"))));
  h->board = jh_board_registry_find(
      &h->registry->boards.boards,
      jh_span(jh_hw_text(h, jh_json_field(selection, "board"))));
  if (h->target == NULL || h->board == NULL) {
    jh_hw_error(
        h, "JH-HW-GENERATION", (jh_hw_origin_t){h->path, "/selection"}, NULL,
        JH_PARTS(jh_span("the model selects an unknown target or board")));
    return (jb_json_tree_view_t){0};
  }
  const jb_json_tree_view_t board = h->board->root;
  const char *radio =
      jh_hw_bool(jh_json_field(capabilities, "cyw43"))
          ? jh_hw_text(
                h, jh_json_field(jh_json_field(hardware, "providers"), "cyw43"))
          : "";
  const bool module_radio = *radio != '\0' && strcmp(radio, "board") != 0;
  jb_json_tree_t *result = jh_hw_copy(h, board),
                 *board_components = jh_hw_object(h);
  const jb_json_tree_view_t original = jh_json_field(board, "components");
  for (size_t i = 0; i < jh_json_count(original); ++i) {
    const jb_json_tree_view_t item = jh_json_item(original, i);
    const char *key = jh_hw_key(h, item), *slot = component_slot(h, key);
    const bool clock = strcmp(slot, "system-clock") == 0;
    const bool owned = clock || strcmp(slot, "network-radio-transport") == 0 ||
                       strcmp(slot, "network-stack") == 0 ||
                       strcmp(slot, "bluetooth-host-stack") == 0;
    if (!owned || (strcmp(radio, "board") == 0 && !clock))
      jh_hw_set(h, board_components, key, item);
  }
  if (strcmp(jb_string_data(&h->target->id), "stm32g474") == 0) {
    const jb_json_tree_view_t tree =
        jh_json_field(jh_json_field(hardware, "clock"), "tree");
    for (size_t i = 0; i < jh_clock_stm32g474_tree_count; ++i) {
      const jh_clock_tree_component_t *entry = &jh_clock_stm32g474_trees[i];
      if (entry->component != NULL && jh_hw_is(tree, entry->tree)) {
        jb_json_tree_t *mode = jh_hw_object(h);
        jh_hw_set(h, mode, "mode", jh_hw_string(h, jh_span("board-owned")));
        jh_hw_set(h, board_components, entry->component, jh_hw_root(mode));
      }
    }
  }
  jh_hw_set(h, result, "components", jh_hw_root(board_components));
  jb_json_tree_t *caps = jh_hw_object(h);
  for (size_t i = 0; i < jb_vector_size(&h->registry->capabilities); ++i) {
    const char *cap = jb_string_data(jh_list_at(&h->registry->capabilities, i));
    jb_json_tree_t *item = jh_hw_object(h);
    jh_hw_set(h, item, "present",
              jh_hw_boolean(h, jh_hw_bool(jh_json_field(capabilities, cap))));
    jh_hw_set(h, item, "macro",
              jh_hw_string(h, jh_span(jh_hw_join(
                                  h, JH_PARTS(jh_span("JH_HW_HAS_"),
                                              jh_span(jh_hw_token(h, cap)))))));
    jh_hw_set(h, caps, cap, jh_hw_root(item));
  }
  jh_hw_set(h, result, "capabilities", jh_hw_root(caps));
  jb_json_tree_t *devices = jh_hw_object(h);
  const jb_json_tree_view_t originals = jh_json_field(board, "devices");
  for (size_t i = 0; i < jh_json_count(originals); ++i) {
    const jb_json_tree_view_t item = jh_json_item(originals, i);
    const char *key = jh_hw_key(h, item);
    if (strcmp(key, "statusLed") != 0)
      jh_hw_set(h, devices, key, item);
  }
  const jb_json_tree_view_t status = jh_json_field(hardware, "statusLed");
  if (jh_json_type(status) == JB_JSON_OBJECT) {
    const jb_json_tree_view_t endpoint = jh_json_field(status, "endpoint");
    const char *domain = jh_hw_text(h, jh_json_field(endpoint, "domain"));
    const bool addressable =
        jh_hw_is(jh_json_field(status, "kind"), "addressable");
    const char *kind = addressable                       ? "addressable"
                       : strcmp(domain, "soc-gpio") == 0 ? "gpio"
                       : strcmp(domain, "component-gpio") == 0
                           ? "component-gpio"
                           : "device-pin";
    jb_json_tree_t *led = jh_hw_object(h);
    jh_hw_set(h, led, "kind", jh_hw_string(h, jh_span(kind)));
    jh_hw_set(h, led, "endpoint",
              macro_endpoint(h, endpoint,
                             jh_hw_text(h, jh_json_field(status, "node")),
                             jh_hw_text(h, jh_json_field(status, "signal"))));
    if (addressable)
      jh_hw_set(h, led, "pixelOrder", jh_json_field(status, "pixelOrder"));
    jh_hw_set(h, devices, "statusLed", jh_hw_root(led));
  }
  jh_hw_set(h, result, "devices", jh_hw_root(devices));
  const jb_json_tree_view_t gpio = jh_json_field(board, "gpio"),
                            reservations = jh_json_field(gpio, "reservations");
  jb_json_tree_t *new_gpio = jh_hw_copy(h, gpio), *kept = jh_hw_object(h);
  for (size_t i = 0; i < jh_json_count(reservations); ++i) {
    const jb_json_tree_view_t item = jh_json_item(reservations, i);
    const char *key = jh_hw_key(h, item);
    if (!jh_hw_bool(jh_json_field(
            jh_json_field(jh_json_field(hardware, "reservations"), key),
            "released")))
      jh_hw_set(h, kept, key, item);
  }
  jh_hw_set(h, new_gpio, "reservations", jh_hw_root(kept));
  jh_hw_set(h, result, "gpio", jh_hw_root(new_gpio));
  jh_hw_set(h, result, "can", can_channels(h, hardware, board));
  jh_hw_keys(h, jh_json_field(h->target->root, "components"), components);
  jh_hw_keys(h, jh_hw_root(board_components), components);
  const char *provider = jh_hw_text(
      h, jh_json_field(jh_json_field(h->target->root, "build"), "provider"));
  if (module_radio) {
    jh_node_storage(
        &h->memory,
        jh_list_push(components, jh_span(strcmp(provider, "pico-sdk") == 0
                                             ? "cyw43-pico-pio"
                                             : "cyw43-stm32-gspi")));
    jh_node_storage(&h->memory,
                    jh_list_push(components, jh_span("cyw43-lwip")));
    jh_node_storage(&h->memory,
                    jh_list_push(components, jh_span("btstack-host")));
  }
  jb_json_tree_t *all = jh_hw_object(h), *component_board = jh_hw_object(h);
  for (size_t i = 0; i < jb_vector_size(components); ++i)
    jh_hw_set(h, all, jb_string_data(jh_list_at(components, i)),
              jh_hw_root(jh_hw_object(h)));
  jh_list_destroy(components);
  jh_node_storage(&h->memory, jh_list_init(components));
  jh_hw_keys(h, jh_hw_root(all), components);
  jh_hw_set(h, component_board, "components", jh_hw_root(all));
  if (h->memory.status == HAL_OK)
    jh_node_storage(&h->memory, jh_board_compile_definitions(
                                    h->target->root,
                                    jh_hw_root(component_board), definitions));
  if (module_radio) {
    static const char *const pins[][2] = {{"WL_ON", "wlOn"},
                                          {"CHIP_SELECT", "cs"},
                                          {"DATA", "data"},
                                          {"CLOCK", "clock"}};
    for (size_t i = 0; i < COUNTOF(pins); ++i)
      jh_node_storage(
          &h->memory,
          jh_list_push_parts(
              definitions, JH_PARTS(jh_span("HAL_CYW43_PIN_"),
                                    jh_span(pins[i][0]), jh_span("="),
                                    jh_span(pin_macro(h, radio, pins[i][1])))));
    if (strcmp(provider, "pico-sdk") == 0)
      jh_node_storage(
          &h->memory,
          jh_list_push(definitions,
                       jh_span("HAL_CYW43_GSPI_TARGET_HZ=31250000u")));
  }
  return jh_hw_root(result);
}
