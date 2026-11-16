#include "jh_hardware_internal.h"
#include <string.h>

const jh_hw_renderer_t *jh_hw_renderer_info(const char *name) {
  static const jh_hw_renderer_t renderers[] = {
      {"mcp2515", "hal_can_mcp2515_config_t", NULL, 10000000, 3},
      {"mcp251xfd", "hal_can_mcp251xfd_config_t", "spi_clock_hz", 0, 3},
      {"sx126x", "hal_lora_radio_config_t", "spi_clock_hz", 0, 3},
      {"ili9341", "hal_display_ili9341_config_t", "clock_hz", 0, 2},
      {"fdcan", "hal_can_stm32g474_fdcan_config_t", NULL, 0, 0}};
  for (size_t i = 0; i < COUNTOF(renderers); ++i)
    if (name != NULL && strcmp(name, renderers[i].name) == 0)
      return &renderers[i];
  return NULL;
}

const char *jh_hw_renderer(const char *compatible) {
  static const struct {
    const char *compatible, *renderer;
  } table[] = {{"microchip,mcp2515", "mcp2515"},
               {"semtech,sx1262", "sx126x"},
               {"ilitek,ili9341", "ili9341"},
               {"jaszczurhal,can-transceiver", "fdcan"},
               {"microchip,mcp2562fd", "fdcan"}};
  for (size_t i = 0; i < COUNTOF(table); ++i)
    if (strcmp(compatible, table[i].compatible) == 0)
      return table[i].renderer;
  return NULL;
}

typedef struct {
  jh_hw_t *h;
  jb_string_t c, cpp, assignments;
  size_t count;
} fields_t;

static void fields_init(fields_t *f, jh_hw_t *h) {
  memset(f, 0, sizeof *f);
  f->h = h;
  jh_node_storage(&h->memory, jb_string_init(&f->c, jb_heap_allocator()));
  jh_node_storage(&h->memory, jb_string_init(&f->cpp, jb_heap_allocator()));
  jh_node_storage(&h->memory,
                  jb_string_init(&f->assignments, jb_heap_allocator()));
  jh_node_storage(&h->memory, jh_append(&f->c, "{"));
  jh_node_storage(&h->memory, jh_append(&f->cpp, "{"));
}

static void field(fields_t *f, const char *name, const char *c,
                  const char *cpp) {
  hal_status_t *status = &f->h->memory.status;
  if (cpp == NULL)
    cpp = c;
  if (f->count != 0) {
    jh_put(&f->c, status, ", ");
    jh_put(&f->cpp, status, ", ");
    jh_put(&f->assignments, status, " ");
  }
  ++f->count;
  jh_put(&f->c, status, ".");
  jh_put(&f->c, status, name);
  jh_put(&f->c, status, " = ");
  jh_put(&f->c, status, c);
  jh_put(&f->cpp, status, cpp);
  jh_put(&f->assignments, status, "config.stm32g474_fdcan.");
  jh_put(&f->assignments, status, name);
  jh_put(&f->assignments, status, " = ");
  jh_put(&f->assignments, status, cpp);
  jh_put(&f->assignments, status, ";");
}

static void bool_field(fields_t *f, const char *name, bool value) {
  field(f, name, value ? "1" : "0", value ? "true" : "false");
}

static void value_field(fields_t *f, const char *name, const char *type,
                        jb_json_tree_view_t value) {
  field(f, name, jh_hw_literal(f->h, type, value), NULL);
}

static void integer_field(fields_t *f, const char *name, const char *type,
                          int64_t value) {
  value_field(f, name, type, jh_hw_integer(f->h, value));
}

static jh_hw_initializer_t fields_finish(fields_t *f, const char *comment) {
  jh_put(&f->c, &f->h->memory.status, "}");
  jh_put(&f->cpp, &f->h->memory.status, "}");
  const jh_hw_initializer_t result = {
      jh_hw_join(f->h, JH_PARTS(jh_string_span(&f->c))),
      jh_hw_join(f->h, JH_PARTS(jh_string_span(&f->cpp))), comment};
  jb_string_destroy(&f->assignments);
  jb_string_destroy(&f->cpp);
  jb_string_destroy(&f->c);
  return result;
}

static jb_json_tree_view_t property(jb_json_tree_view_t node, const char *key) {
  return jh_json_field(jh_json_field(jh_json_field(node, "properties"), key),
                       "value");
}

static bool node_pin(jb_json_tree_view_t node, const char *name, int64_t *out) {
  return jh_board_encode_pin(
      jh_json_field(jh_json_field(jh_json_field(node, "signals"), name),
                    "endpoint"),
      out);
}

static jh_hw_initializer_t mcp2515(jh_hw_t *h, jb_json_tree_view_t node,
                                   jb_json_tree_view_t bus) {
  int64_t cs = 0;
  if (!node_pin(node, "cs", &cs))
    return (jh_hw_initializer_t){0};
  fields_t f;
  fields_init(&f, h);
  value_field(&f, "spi_bus", "uint8", jh_json_field(bus, "index"));
  integer_field(&f, "cs_pin", "uint8", cs);
  value_field(&f, "bitrate_hz", "uint32", property(node, "bitrateHz"));
  value_field(&f, "oscillator_hz", "uint32", property(node, "oscillatorHz"));
  bool_field(&f, "one_shot_tx", true);
  bool_field(&f, "sleep_wakeup", true);
  const jh_hw_initializer_t inner = fields_finish(&f, NULL);
  const jh_hw_initializer_t result = {
      jh_hw_join(
          h,
          JH_PARTS(jh_span("{.backend = HAL_CAN_BACKEND_MCP2515, .mcp2515 = "),
                   jh_span(inner.c), jh_span("}"))),
      jh_hw_join(h, JH_PARTS(jh_span("{HAL_CAN_BACKEND_MCP2515, {"),
                             jh_span(inner.cpp), jh_span("}}"))),
      "hal_can_config_t; needs hal_can.h, HAL_ENABLE_CAN"};
  return result;
}

static jh_hw_initializer_t fdcan(jh_hw_t *h, jb_json_tree_view_t node) {
  const jb_json_tree_view_t controller = jh_json_field(node, "controller");
  int64_t rx = 0, tx = 0, standby = 0, instance = 0, maximum = 0;
  if (jh_json_type(controller) == JB_JSON_NULL || !node_pin(node, "rx", &rx) ||
      !node_pin(node, "tx", &tx))
    return (jh_hw_initializer_t){0};
  const bool has_standby = node_pin(node, "standby", &standby);
  (void)jh_hw_i64(h, jh_json_field(controller, "index"), &instance);
  (void)jh_hw_i64(h, property(node, "maxBitrateHz"), &maximum);
  fields_t f;
  fields_init(&f, h);
  integer_field(&f, "instance", "uint8", instance + 1);
  integer_field(&f, "rx_pin", "uint8", rx);
  integer_field(&f, "tx_pin", "uint8", tx);
  bool_field(&f, "has_standby", has_standby);
  integer_field(&f, "standby_pin", "uint8", has_standby ? standby : 0);
  bool_field(&f, "standby_high",
             jh_hw_bool(property(node, "standbyActiveHigh")));
  integer_field(&f, "arbitration_bitrate_hz", "uint32", 500000);
  integer_field(&f, "data_bitrate_hz", "uint32",
                maximum < 2000000 ? maximum : 2000000);
  integer_field(&f, "transceiver_max_bitrate_hz", "uint32", maximum);
  bool_field(&f, "enable_fd", true);
  bool_field(&f, "one_shot_tx", false);
  const char *cpp = jh_hw_join(
      h, JH_PARTS(jh_span("([]() { hal_can_config_t config{}; config.backend = "
                          "HAL_CAN_BACKEND_STM32G474_FDCAN; "),
                  jh_string_span(&f.assignments),
                  jh_span(" return config; }())")));
  const jh_hw_initializer_t inner = fields_finish(&f, NULL);
  const jh_hw_initializer_t result = {
      jh_hw_join(
          h, JH_PARTS(jh_span("{.backend = HAL_CAN_BACKEND_STM32G474_FDCAN, "
                              ".stm32g474_fdcan = "),
                      jh_span(inner.c), jh_span("}"))),
      cpp, "hal_can_config_t; needs hal_can.h, HAL_ENABLE_CAN"};
  return result;
}

static const char *lora_pin(jh_hw_t *h, int64_t pin, bool present) {
  return present ? jh_hw_literal(h, "uint8", jh_hw_integer(h, pin))
                 : "HAL_LORA_PIN_NONE";
}

static jh_hw_initializer_t sx126x(jh_hw_t *h, jb_json_tree_view_t node,
                                  jb_json_tree_view_t bus) {
  static const char *const names[] = {"cs",   "reset",     "busy",
                                      "dio1", "rfSwitchA", "rfSwitchB"};
  int64_t pins[COUNTOF(names)] = {0};
  bool present[COUNTOF(names)] = {false};
  for (size_t i = 0; i < COUNTOF(names); ++i)
    present[i] = node_pin(node, names[i], &pins[i]);
  const char *mode = jh_hw_text(h, property(node, "rfSwitchMode"));
  const bool dual = strcmp(mode, "dual-gpio") == 0;
  const bool single =
      strcmp(mode, "single-gpio") == 0 || strcmp(mode, "dio2-single-gpio") == 0;
  int64_t startup = 0;
  (void)jh_hw_i64(h, property(node, "tcxoStartupUs"), &startup);
  if (!present[0] || !present[1] || !present[2] || !present[3] ||
      present[4] != (single || dual) || present[5] != dual ||
      jh_json_type(property(node, "maxSpiClockHz")) == JB_JSON_NULL ||
      (jh_hw_is(property(node, "tcxoControl"), "dio3") && startup == 0))
    return (jh_hw_initializer_t){0};
  fields_t f;
  fields_init(&f, h);
  field(&f, "reset_pin", lora_pin(h, pins[1], present[1]), NULL);
  field(&f, "dio1_pin", lora_pin(h, pins[3], present[3]), NULL);
  field(&f, "busy_pin", lora_pin(h, pins[2], present[2]), NULL);
  field(&f, "rf_switch_mode",
        jh_hw_join(h, JH_PARTS(jh_span("HAL_LORA_RF_SWITCH_"),
                               jh_span(jh_hw_token(h, mode)))),
        NULL);
  field(&f, "rf_switch_pin_a", lora_pin(h, pins[4], present[4]), NULL);
  field(&f, "rf_switch_pin_b", lora_pin(h, pins[5], present[5]), NULL);
  static const struct {
    const char *field, *property;
  } levels[] = {{"rf_switch_idle_level_a", "rfSwitchIdleLevelA"},
                {"rf_switch_idle_level_b", "rfSwitchIdleLevelB"},
                {"rf_switch_rx_level_a", "rfSwitchRxLevelA"},
                {"rf_switch_rx_level_b", "rfSwitchRxLevelB"},
                {"rf_switch_tx_level_a", "rfSwitchTxLevelA"},
                {"rf_switch_tx_level_b", "rfSwitchTxLevelB"}};
  for (size_t i = 0; i < COUNTOF(levels); ++i)
    bool_field(&f, levels[i].field,
               jh_hw_bool(property(node, levels[i].property)));
  static const struct {
    const char *field, *property, *prefix, *fallback;
  } enums[] = {{"regulator_mode", "regulator", "HAL_LORA_REGULATOR_", ""},
               {"tcxo_control", "tcxoControl", "HAL_LORA_TCXO_CONTROL_", ""},
               {"tcxo_voltage", "tcxoVoltage", "HAL_LORA_TCXO_", "1v6"}};
  for (size_t i = 0; i < COUNTOF(enums); ++i) {
    const jb_json_tree_view_t value = property(node, enums[i].property);
    const char *name =
        value.tree == NULL ? enums[i].fallback : jh_hw_text(h, value);
    field(&f, enums[i].field,
          jh_hw_join(h, JH_PARTS(jh_span(enums[i].prefix),
                                 jh_span(jh_hw_token(h, name)))),
          NULL);
  }
  static const struct {
    const char *field, *property, *type;
  } numbers[] = {{"tcxo_startup_us", "tcxoStartupUs", "uint32"},
                 {"min_frequency_hz", "minFrequencyHz", "uint32"},
                 {"max_frequency_hz", "maxFrequencyHz", "uint32"},
                 {"max_spi_clock_hz", "maxSpiClockHz", "uint32"},
                 {"min_tx_power_dbm", "minTxPowerDbm", "int8"},
                 {"max_tx_power_dbm", "maxTxPowerDbm", "int8"}};
  for (size_t i = 0; i < COUNTOF(numbers); ++i) {
    jb_json_tree_view_t value = property(node, numbers[i].property);
    if (value.tree == NULL)
      value = jh_hw_integer(h, 0);
    value_field(&f, numbers[i].field, numbers[i].type, value);
  }
  const jh_hw_initializer_t hardware = fields_finish(&f, NULL);
  fields_init(&f, h);
  field(&f, "model", "HAL_LORA_RADIO_SX1262", NULL);
  value_field(&f, "spi_bus", "uint8", jh_json_field(bus, "index"));
  static const char *const bus_pins[] = {"miso", "mosi", "sck"};
  static const char *const bus_fields[] = {"spi_miso_pin", "spi_mosi_pin",
                                           "spi_sck_pin"};
  for (size_t i = 0; i < COUNTOF(bus_pins); ++i) {
    int64_t pin = 0;
    const bool wired = jh_board_encode_pin(
        jh_json_field(jh_json_field(bus, "pins"), bus_pins[i]), &pin);
    field(&f, bus_fields[i], lora_pin(h, pin, wired), NULL);
  }
  field(&f, "cs_pin", lora_pin(h, pins[0], present[0]), NULL);
  value_field(&f, jh_hw_renderer_info("sx126x")->clock_field, "uint32",
              jh_json_field(jh_json_field(node, "transport"), "frequencyHz"));
  field(&f, "hardware",
        jh_hw_join(h, JH_PARTS(jh_span("{.sx126x = "), jh_span(hardware.c),
                               jh_span("}"))),
        jh_hw_join(
            h, JH_PARTS(jh_span("{"), jh_span(hardware.cpp), jh_span("}"))));
  return fields_finish(
      &f, "hal_lora_radio_config_t; needs hal_lora_radio.h, HAL_ENABLE_LORA");
}

static jh_hw_initializer_t ili9341(jh_hw_t *h, jb_json_tree_view_t node,
                                   jb_json_tree_view_t bus) {
  int64_t dc = 0;
  if (!node_pin(node, "dc", &dc))
    return (jh_hw_initializer_t){0};
  fields_t f;
  fields_init(&f, h);
  value_field(&f, "bus", "uint8", jh_json_field(bus, "index"));
  static const char *const pins[] = {"cs", "dc", "reset"};
  static const char *const fields[] = {"cs_pin", "dc_pin", "rst_pin"};
  for (size_t i = 0; i < COUNTOF(pins); ++i) {
    int64_t pin = -1;
    (void)node_pin(node, pins[i], &pin);
    integer_field(&f, fields[i], "int16", pin);
  }
  value_field(&f, jh_hw_renderer_info("ili9341")->clock_field, "uint32",
              jh_json_field(jh_json_field(node, "transport"), "frequencyHz"));
  return fields_finish(
      &f, "hal_display_ili9341_config_t; needs hal_display.h, HAL_ENABLE_TFT");
}

jh_hw_initializer_t jh_hw_initializer(jh_hw_t *h, const char *path,
                                      jb_json_tree_view_t node,
                                      jb_json_tree_view_t hardware) {
  const jh_hw_initializer_t none = {0};
  const char *renderer =
      jh_hw_renderer(jh_hw_text(h, jh_json_field(node, "compatible")));
  if (renderer == NULL || !jh_hw_bool(jh_json_field(node, "present")))
    return none;
  const jb_json_tree_view_t signals = jh_json_field(node, "signals");
  for (size_t i = 0; i < jh_json_count(signals); ++i) {
    const jb_json_tree_view_t pin = jh_json_item(signals, i);
    const char *owner = jh_hw_join(
        h, JH_PARTS(jh_span(path), jh_span("."), jh_span(jh_hw_key(h, pin))));
    if (jh_json_type(jh_json_field(pin, "endpoint")) != JB_JSON_NULL &&
        !jh_hw_is(jh_json_field(pin, "owner"), owner))
      return none;
  }
  jb_json_tree_view_t bus = jh_json_field(node, "bus");
  if (jh_json_type(bus) == JB_JSON_STRING)
    bus = jh_json_field(jh_json_field(hardware, "buses"), jh_hw_text(h, bus));
  if (strcmp(renderer, "fdcan") == 0)
    return fdcan(h, node);
  static const char *const lines[] = {"sck", "mosi", "miso"};
  const size_t line_count = jh_hw_renderer_info(renderer)->bus_lines;
  for (size_t i = 0; i < line_count; ++i) {
    int64_t pin = 0;
    if (!jh_board_encode_pin(
            jh_json_field(jh_json_field(bus, "pins"), lines[i]), &pin))
      return none;
  }
  if (strcmp(renderer, "mcp2515") == 0)
    return mcp2515(h, node, bus);
  if (strcmp(renderer, "sx126x") == 0)
    return sx126x(h, node, bus);
  return ili9341(h, node, bus);
}

jh_hw_initializer_t jh_hw_spi_initializer(jh_hw_t *h,
                                          jb_json_tree_view_t settings) {
  fields_t f;
  fields_init(&f, h);
  value_field(&f, "clock_hz", "uint32", jh_json_field(settings, "frequencyHz"));
  field(&f, "bit_order",
        jh_hw_is(jh_json_field(settings, "bitOrder"), "msb") ? "1U" : "0U",
        NULL);
  value_field(&f, "data_mode", "uint8", jh_json_field(settings, "mode"));
  return fields_finish(&f, "hal_spi_settings_t; needs hal_spi.h");
}

unsigned jh_hw_parity(jb_json_tree_view_t parity) {
  return jh_hw_is(parity, "even") ? 1u : (jh_hw_is(parity, "odd") ? 2u : 0u);
}

const char *jh_hw_uart_frame(jh_hw_t *h, jb_json_tree_view_t frame) {
  static const char *const flags[] = {"N", "E", "O"};
  return jh_hw_join(
      h,
      JH_PARTS(jh_span("HAL_UART_CFG_"),
               jh_span(jh_hw_value_text(h, jh_json_field(frame, "dataBits"))),
               jh_span(flags[jh_hw_parity(jh_json_field(frame, "parity"))]),
               jh_span(jh_hw_value_text(h, jh_json_field(frame, "stopBits")))));
}
