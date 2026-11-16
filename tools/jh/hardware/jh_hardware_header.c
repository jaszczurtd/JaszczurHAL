#include "jh_hardware_internal.h"
#include <stdio.h>
#include <string.h>

static void define(jh_hw_t *h, jb_string_t *out, const char *prefix,
                   const char *suffix, const char *value, const char *comment) {
  hal_status_t *status = &h->memory.status;
  jh_put(out, status, "#define ");
  jh_put(out, status, prefix);
  jh_put(out, status, suffix);
  jh_put(out, status, " ");
  jh_put(out, status, value);
  if (comment != NULL && *comment != '\0') {
    jh_put(out, status, " /* ");
    jh_put(out, status, comment);
    jh_put(out, status, " */");
  }
  jh_put(out, status, "\n");
}

static void blank(jh_hw_t *h, jb_string_t *out, const char *comment) {
  jh_put(out, &h->memory.status, "\n/* ");
  jh_put(out, &h->memory.status, comment);
  jh_put(out, &h->memory.status, " */\n");
}

static void deferred(jh_hw_t *h, jb_string_t *out, const char *prefix,
                     const char *suffix, jh_hw_initializer_t initializer) {
  jh_put(out, &h->memory.status, "/* ");
  jh_put(out, &h->memory.status, initializer.comment);
  jh_put(out, &h->memory.status, " */\n#ifdef __cplusplus\n");
  define(h, out, prefix, suffix, initializer.cpp, NULL);
  jh_put(out, &h->memory.status, "#else\n");
  define(h, out, prefix, suffix, initializer.c, NULL);
  jh_put(out, &h->memory.status, "#endif\n");
}

static void pin_macros(jh_hw_t *h, jb_string_t *out, const char *prefix,
                       jb_json_tree_view_t endpoint, bool ownership,
                       bool owner) {
  const bool connected = jh_json_type(endpoint) != JB_JSON_NULL;
  const jb_json_tree_view_t domain = jh_json_field(endpoint, "domain");
  define(h, out, prefix, "_CONNECTED", connected ? "1" : "0", NULL);
  define(h, out, prefix, "_DOMAIN",
         connected ? (jh_hw_is(domain, "soc-gpio") ? "1" : "2") : "0",
         "0 none, 1 SoC GPIO, 2 device pin");
  if (ownership)
    define(h, out, prefix, "_OWNER", connected && owner ? "1" : "0", NULL);
  if (!connected)
    return;
  int64_t encoded = 0;
  if (jh_board_encode_pin(endpoint, &encoded))
    define(h, out, prefix, "",
           jh_hw_literal(h, "uint8", jh_hw_integer(h, encoded)),
           "HAL pin encoding");
  if (jh_hw_is(domain, "device-pin")) {
    define(h, out, prefix, "_PROVIDER",
           jh_hw_literal(h, "string", jh_json_field(endpoint, "node")), NULL);
    define(h, out, prefix, "_INDEX",
           jh_hw_literal(h, "uint32", jh_json_field(endpoint, "index")), NULL);
  } else if (!jh_hw_is(domain, "soc-gpio")) {
    const char *provider = jh_hw_join(
        h,
        JH_PARTS(jh_span("board."),
                 jh_span(jh_hw_text(h, jh_json_field(endpoint, "component")))));
    define(h, out, prefix, "_PROVIDER",
           jh_hw_literal(h, "string", jh_hw_string(h, jh_span(provider))),
           NULL);
    define(h, out, prefix, "_INDEX",
           jh_hw_literal(h, "uint32", jh_json_field(endpoint, "id")), NULL);
  }
}

static void bus_header(jh_hw_t *h, jb_string_t *out, const char *name,
                       jb_json_tree_view_t bus) {
  const char *prefix = jh_hw_join(
      h, JH_PARTS(jh_span("JH_HW_BUS_"), jh_span(jh_hw_token(h, name))));
  const char *kind = jh_hw_text(h, jh_json_field(bus, "kind"));
  blank(
      h, out,
      jh_hw_join(h, JH_PARTS(jh_span(kind), jh_span(" bus "), jh_span(name))));
  define(h, out, prefix, "_INDEX",
         jh_hw_literal(h, "uint8", jh_json_field(bus, "index")), NULL);
  const jb_json_tree_view_t pins = jh_json_field(bus, "pins");
  jb_vector_t names = {0};
  jh_node_storage(&h->memory, jh_list_init(&names));
  jh_hw_keys(h, pins, &names);
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&names);
       ++i) {
    const char *pin = jb_string_data(jh_list_at(&names, i));
    pin_macros(h, out,
               jh_hw_join(h, JH_PARTS(jh_span(prefix), jh_span("_PIN_"),
                                      jh_span(jh_hw_token(h, pin)))),
               jh_json_field(pins, pin), false, false);
  }
  jh_list_destroy(&names);
  if (strcmp(kind, "spi") == 0 || strcmp(kind, "i2c") == 0) {
    define(h, out, prefix, "_FREQUENCY_HZ",
           jh_hw_literal(h, "uint32", jh_json_field(bus, "frequencyHz")), "Hz");
    return;
  }
  int64_t index = 0;
  (void)jh_hw_i64(h, jh_json_field(bus, "index"), &index);
  define(h, out, prefix, "_UART_PORT",
         jh_hw_literal(h, "uint8", jh_hw_integer(h, index + 1)),
         "HAL_UART_PORT_<n>");
  define(h, out, prefix, "_BAUD_RATE",
         jh_hw_literal(h, "uint32", jh_json_field(bus, "baudRate")), "baud");
  const jb_json_tree_view_t frame = jh_json_field(bus, "frame");
  define(h, out, prefix, "_DATA_BITS",
         jh_hw_literal(h, "uint8", jh_json_field(frame, "dataBits")), NULL);
  define(h, out, prefix, "_PARITY",
         jh_hw_literal(
             h, "uint8",
             jh_hw_integer(h, jh_hw_parity(jh_json_field(frame, "parity")))),
         "0 none, 1 even, 2 odd");
  define(h, out, prefix, "_STOP_BITS",
         jh_hw_literal(h, "uint8", jh_json_field(frame, "stopBits")), NULL);
  define(h, out, prefix, "_FRAME_CONFIG", jh_hw_uart_frame(h, frame),
         "needs hal_uart_config.h, HAL_ENABLE_UART");
}

static void node_header(jh_hw_t *h, jb_string_t *out, const char *path,
                        jb_json_tree_view_t node,
                        jb_json_tree_view_t hardware) {
  const char *prefix = jh_hw_join(
      h, JH_PARTS(jh_span("JH_HW_NODE_"), jh_span(jh_hw_token(h, path))));
  blank(h, out,
        jh_hw_join(h, JH_PARTS(jh_span(path), jh_span(": "),
                               jh_span(jh_hw_text(
                                   h, jh_json_field(node, "compatible"))))));
  const bool present = jh_hw_bool(jh_json_field(node, "present"));
  define(h, out, prefix, "_PRESENT", present ? "1" : "0", NULL);
  const jh_hw_initializer_t initializer =
      jh_hw_initializer(h, path, node, hardware);
  define(h, out, prefix, "_CONFIG_AVAILABLE", initializer.c != NULL ? "1" : "0",
         NULL);
  if (!present)
    return;
  jb_vector_t names = {0};
  jh_node_storage(&h->memory, jh_list_init(&names));
  const jb_json_tree_view_t properties = jh_json_field(node, "properties");
  jh_hw_keys(h, properties, &names);
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&names);
       ++i) {
    const char *name = jb_string_data(jh_list_at(&names, i));
    const jb_json_tree_view_t item = jh_json_field(properties, name);
    define(h, out, prefix,
           jh_hw_join(
               h, JH_PARTS(jh_span("_PROP_"), jh_span(jh_hw_token(h, name)))),
           jh_hw_literal(h, jh_hw_text(h, jh_json_field(item, "cType")),
                         jh_json_field(item, "value")),
           jh_hw_text(h, jh_json_field(item, "unit")));
  }
  jh_list_destroy(&names);
  if (jh_hw_is(jh_json_field(node, "compatible"), "jaszczurhal,gpio-input")) {
    const jb_json_tree_view_t pull =
        jh_json_field(jh_json_field(properties, "pull"), "value");
    jh_put(out, &h->memory.status,
           "/** GPIO input mode from pull; requires hal_gpio.h. */\n");
    define(h, out, prefix, "_INPUT_MODE",
           jh_hw_is(pull, "up")
               ? "HAL_GPIO_INPUT_PULLUP"
               : (jh_hw_is(pull, "down") ? "HAL_GPIO_INPUT_PULLDOWN"
                                         : "HAL_GPIO_INPUT"),
           NULL);
  }
  jh_node_storage(&h->memory, jh_list_init(&names));
  const jb_json_tree_view_t signals = jh_json_field(node, "signals");
  jh_hw_keys(h, signals, &names);
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&names);
       ++i) {
    const char *name = jb_string_data(jh_list_at(&names, i));
    const jb_json_tree_view_t signal = jh_json_field(signals, name);
    const char *key =
        jh_hw_join(h, JH_PARTS(jh_span(path), jh_span("."), jh_span(name)));
    pin_macros(h, out,
               jh_hw_join(h, JH_PARTS(jh_span(prefix), jh_span("_PIN_"),
                                      jh_span(jh_hw_token(h, name)))),
               jh_json_field(signal, "endpoint"), true,
               jh_hw_is(jh_json_field(signal, "owner"), key));
  }
  jh_list_destroy(&names);
  jb_json_tree_view_t bus = jh_json_field(node, "bus");
  if (jh_json_type(bus) == JB_JSON_STRING)
    bus = jh_json_field(jh_json_field(hardware, "buses"), jh_hw_text(h, bus));
  if (jh_json_type(bus) != JB_JSON_NULL)
    define(h, out, prefix, "_BUS_INDEX",
           jh_hw_literal(h, "uint8", jh_json_field(bus, "index")), NULL);
  const jb_json_tree_view_t controller = jh_json_field(node, "controller");
  if (jh_json_type(controller) != JB_JSON_NULL)
    define(h, out, prefix, "_CONTROLLER_INDEX",
           jh_hw_literal(h, "uint8", jh_json_field(controller, "index")), NULL);
  const jb_json_tree_view_t address = jh_json_field(node, "address");
  if (jh_json_type(address) != JB_JSON_NULL) {
    int64_t number = 0;
    (void)jh_hw_i64(h, address, &number);
    char hex[16];
    (void)snprintf(hex, sizeof hex, "0x%02xU", (unsigned)number);
    define(h, out, prefix, "_ADDRESS", hex, "7-bit I2C address");
  }
  const jb_json_tree_view_t transport = jh_json_field(node, "transport");
  if (jh_json_count(transport) != 0) {
    define(h, out, prefix, "_FREQUENCY_HZ",
           jh_hw_literal(h, "uint32", jh_json_field(transport, "frequencyHz")),
           "Hz");
    const jb_json_tree_view_t mode = jh_json_field(transport, "mode");
    if (mode.tree != NULL) {
      define(h, out, prefix, "_SPI_MODE", jh_hw_literal(h, "uint8", mode),
             NULL);
      define(h, out, prefix, "_SPI_BIT_ORDER",
             jh_hw_is(jh_json_field(transport, "bitOrder"), "msb") ? "1U"
                                                                   : "0U",
             "0 LSB first, 1 MSB first");
      deferred(h, out, prefix, "_SPI_SETTINGS_INIT",
               jh_hw_spi_initializer(h, transport));
    }
  }
  if (initializer.c != NULL)
    deferred(h, out, prefix, "_CONFIG_INIT", initializer);
}

void jh_hw_render_header(jh_hw_t *h, jb_json_tree_view_t model,
                         const char *digest, jb_string_t *out) {
  jh_put(out, &h->memory.status,
         "/* Generated by jh hardware resolve; do not edit. */\n"
         "/* Early hardware facts of one assembly. Initializer macros expand "
         "only\n"
         " * after the HAL header named in their comment is included. "
         "*/\n#pragma once\n\n");
  define(h, out, "JH_HW_SCHEMA_VERSION", "", "1", NULL);
  const jb_json_tree_view_t selection = jh_json_field(model, "selection");
  static const char *const fields[] = {"target", "board", "assembly"};
  static const char *const macros[] = {"JH_HW_TARGET_NAME", "JH_HW_BOARD_NAME",
                                       "JH_HW_ASSEMBLY_NAME"};
  for (size_t i = 0; i < COUNTOF(fields); ++i)
    define(h, out, macros[i], "",
           jh_hw_literal(h, "string", jh_json_field(selection, fields[i])),
           NULL);
  define(h, out, "JH_HW_HARDWARE_SHA256", "",
         jh_hw_literal(h, "string", jh_hw_string(h, jh_span(digest))), NULL);
  const jb_json_span_t first = {digest,
                                strlen(digest) < 16 ? strlen(digest) : 16};
  define(h, out, "JH_HW_HARDWARE_ID", "",
         jh_hw_join(h, JH_PARTS(jh_span("0x"), first, jh_span("ULL"))),
         "first 64 bits of the digest, for #if checks");
  blank(h, out, "effective capabilities");
  const jb_json_tree_view_t hardware = jh_json_field(model, "hardware");
  const jb_json_tree_view_t capabilities =
      jh_json_field(hardware, "capabilities");
  jb_vector_t names = {0};
  jh_node_storage(&h->memory, jh_list_init(&names));
  jh_hw_keys(h, capabilities, &names);
  for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&names);
       ++i) {
    const char *name = jb_string_data(jh_list_at(&names, i));
    define(h, out, "JH_HW_HAS_", jh_hw_token(h, name),
           jh_hw_bool(jh_json_field(capabilities, name)) ? "1" : "0", NULL);
  }
  jh_list_destroy(&names);
  static const char *const tables[] = {"buses", "nodes"};
  for (size_t t = 0; t < COUNTOF(tables); ++t) {
    const jb_json_tree_view_t table = jh_json_field(hardware, tables[t]);
    jh_node_storage(&h->memory, jh_list_init(&names));
    jh_hw_keys(h, table, &names);
    for (size_t i = 0; h->memory.status == HAL_OK && i < jb_vector_size(&names);
         ++i) {
      const char *name = jb_string_data(jh_list_at(&names, i));
      if (t == 0)
        bus_header(h, out, name, jh_json_field(table, name));
      else
        node_header(h, out, name, jh_json_field(table, name), hardware);
    }
    jh_list_destroy(&names);
  }
}

hal_status_t jh_hardware_header(jh_hardware_t *hardware, jb_string_t *out,
                                jh_failure_t *failure) {
  if (hardware == NULL || hardware->state == NULL || out == NULL ||
      failure == NULL)
    return jh_fail_arguments(failure);
  jh_hw_t *h = hardware->state;
  h->failure = h->memory.failure = failure;
  if (h->model.tree == NULL || h->memory.status != HAL_OK)
    return jh_fail_text(failure, HAL_ESTATE, 3,
                        "hardware has no resolved model");
  char digest[65] = {0};
  jh_hw_digest(h, h->model, digest);
  if (h->memory.status == HAL_OK)
    jh_hw_render_header(h, h->model, digest, out);
  return jh_hw_phase(h);
}
