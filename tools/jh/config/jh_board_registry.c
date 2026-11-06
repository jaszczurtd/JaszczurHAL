#include "jh_board_registry.h"
#include "jh_feature_registry.h"
#include "jh_json_check.h"
#include "jh_json_file.h"
#include "jh_python_repr.h"
#include <jb/array.h>
#include <jb/filesystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pins of one set at most: a range wider than this is refused instead of
 * expanded. */
#define JH_BOARD_MAX_PINS 65536u

/* ---- Messages ---- */

/* The descriptor a check reports and the failure it records. */
typedef struct {
  jb_json_span_t path;
  jh_failure_t *failure;
} check_t;

static hal_status_t storage(const check_t *c, hal_status_t status) {
  return status == HAL_OK ? HAL_OK : jh_fail_storage(c->failure, status);
}

/* A string owned by the caller, empty on the host heap. */
static hal_status_t text_init(jb_string_t *text) {
  return jb_string_init(text, jb_heap_allocator());
}

static hal_status_t fail_value(const check_t *c, jb_json_span_t at,
                               const jb_json_tree_view_t *value,
                               const char *expected) {
  return jh_check_value(c->failure, c->path, at, value, expected);
}

/* fail_value with expected joined from parts. */
static hal_status_t fail_value_with(const check_t *c, jb_json_span_t at,
                                    const jb_json_tree_view_t *value,
                                    const jb_json_span_t *parts, size_t count) {
  jb_string_t expected = {0};
  hal_status_t status = text_init(&expected);
  if (status == HAL_OK)
    status = jh_join(&expected, parts, count);
  status = status == HAL_OK
               ? fail_value(c, at, value, jb_string_data(&expected))
               : storage(c, status);
  jb_string_destroy(&expected);
  return status;
}

/* "got <got>" where got is ready repr text, expected joined from parts. */
static hal_status_t fail_got_with(const check_t *c, jb_json_span_t at,
                                  jb_json_span_t got,
                                  const jb_json_span_t *parts, size_t count) {
  jb_string_t expected = {0};
  hal_status_t status = text_init(&expected);
  if (status == HAL_OK)
    status = jh_join(&expected, parts, count);
  status = status == HAL_OK ? jh_check_got(c->failure, c->path, at, got,
                                           jb_string_data(&expected))
                            : storage(c, status);
  jb_string_destroy(&expected);
  return status;
}

/* fail_got_with the repr of a string. */
static hal_status_t fail_text_with(const check_t *c, jb_json_span_t at,
                                   jb_json_span_t text,
                                   const jb_json_span_t *parts, size_t count) {
  jb_string_t got = {0};
  hal_status_t status = text_init(&got);
  if (status == HAL_OK)
    status = jh_python_repr_append(&got, text);
  status = status == HAL_OK
               ? fail_got_with(c, at, jh_string_span(&got), parts, count)
               : storage(c, status);
  jb_string_destroy(&got);
  return status;
}

static hal_status_t fail_list(const check_t *c, jb_json_span_t at,
                              const jb_vector_t *list, const char *expected) {
  return jh_check_list(c->failure, c->path, at, list, expected);
}

/* A DescriptorError without a place: the concatenation of parts. */
static hal_status_t fail_plain(jh_failure_t *failure,
                               const jb_json_span_t *parts, size_t count) {
  return jh_check_fail(failure, parts, count);
}

static hal_status_t exact(const check_t *c, jb_json_span_t at,
                          jb_json_tree_view_t value,
                          const char *const *required, size_t required_count,
                          const char *const *allowed, size_t allowed_count) {
  return jh_check_exact_fields(c->failure, c->path, at, value, required,
                               required_count, allowed, allowed_count);
}

/* ---- JSON values as Python sees them ---- */

static bool member(jb_json_tree_view_t object, const char *key,
                   jb_json_tree_view_t *out) {
  return jh_json_member(object, key, out);
}

/* The member key must exist: exact checked it. */
static jb_json_tree_view_t field(jb_json_tree_view_t object, const char *key) {
  jb_json_tree_view_t out = {0};
  (void)jh_json_member(object, key, &out);
  return out;
}

/* dict.get(key) is None: an absent member or a null. */
static bool absent(jb_json_tree_view_t object, const char *key,
                   jb_json_tree_view_t *out) {
  return !member(object, key, out) || jh_json_type(*out) == JB_JSON_NULL;
}

static bool is_object(jb_json_tree_view_t value) {
  return jh_json_type(value) == JB_JSON_OBJECT;
}

static bool is_array(jb_json_tree_view_t value) {
  return jh_json_type(value) == JB_JSON_ARRAY;
}

static bool is_bool(jb_json_tree_view_t value) {
  return jh_json_type(value) == JB_JSON_BOOL;
}

static bool truthy_bool(jb_json_tree_view_t value) {
  bool flag = false;
  return is_bool(value) && jb_json_tree_get_bool(value, &flag) == HAL_OK &&
         flag;
}

/* The value is the string text. */
static bool string_is(jb_json_tree_view_t value, const char *text) {
  jb_json_span_t got = {NULL, 0};
  return jh_json_string(value, &got) &&
         jh_span_compare(got, jh_span(text)) == 0;
}

/* The value is a string among count names. */
static bool string_in(jb_json_tree_view_t value, const char *const *names,
                      size_t count) {
  for (size_t i = 0; i < count; ++i)
    if (string_is(value, names[i]))
      return true;
  return false;
}

static bool span_in(jb_json_span_t text, const char *const *names,
                    size_t count) {
  for (size_t i = 0; i < count; ++i)
    if (jh_span_compare(text, jh_span(names[i])) == 0)
      return true;
  return false;
}

/* ---- Patterns ---- */

static bool lower_or_digit(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

static bool upper(char c) { return c >= 'A' && c <= 'Z'; }

/* ^[a-z0-9]+(?:-[a-z0-9]+)*$ */
static bool id_pattern(jb_json_span_t text) {
  if (text.length == 0)
    return false;
  bool after_dash = true;
  for (size_t i = 0; i < text.length; ++i) {
    const char c = text.data[i];
    if (c == '-') {
      if (after_dash)
        return false;
      after_dash = true;
    } else if (lower_or_digit(c)) {
      after_dash = false;
    } else {
      return false;
    }
  }
  return !after_dash;
}

/* ^[A-Z][A-Z0-9_]*$ */
static bool macro_pattern(jb_json_span_t text) {
  if (text.length == 0 || !upper(text.data[0]))
    return false;
  for (size_t i = 1; i < text.length; ++i) {
    const char c = text.data[i];
    if (!upper(c) && !(c >= '0' && c <= '9') && c != '_')
      return false;
  }
  return true;
}

/* ^[a-z][a-zA-Z0-9]*$ */
static bool camel_pattern(jb_json_span_t text) {
  if (text.length == 0 || !(text.data[0] >= 'a' && text.data[0] <= 'z'))
    return false;
  for (size_t i = 1; i < text.length; ++i)
    if (!lower_or_digit(text.data[i]) && !upper(text.data[i]))
      return false;
  return true;
}

/* ^P([A-Z])([0-9]|1[0-5])$ */
static bool stm32_pin_pattern(jb_json_span_t text) {
  if (text.length < 3 || text.length > 4 || text.data[0] != 'P' ||
      !upper(text.data[1]))
    return false;
  if (text.length == 3)
    return text.data[2] >= '0' && text.data[2] <= '9';
  return text.data[2] == '1' && text.data[3] >= '0' && text.data[3] <= '5';
}

/* A string value matching one of the patterns above. */
static bool string_matches(jb_json_tree_view_t value,
                           bool (*pattern)(jb_json_span_t)) {
  jb_json_span_t text = {NULL, 0};
  return jh_json_string(value, &text) && pattern(text);
}

/* ---- Pins ---- */

/* A pin ID without ownership. */
typedef struct {
  bool text;
  int64_t number;
  jb_json_span_t name;
} pin_key_t;

/* The pin a JSON value names: an integer token in int64_t or a string. */
static bool pin_key_of(jb_json_tree_view_t value, pin_key_t *out) {
  out->text = false;
  out->number = 0;
  out->name.data = NULL;
  out->name.length = 0;
  if (jh_json_int64(value, &out->number))
    return true;
  out->text = jh_json_string(value, &out->name);
  return out->text;
}

static pin_key_t key_of_pin(const jh_board_pin_t *pin) {
  pin_key_t key = {pin->text, pin->number, {NULL, 0}};
  if (pin->text)
    key.name = jh_string_span(&pin->name);
  return key;
}

/* Integers by value, then strings in code point order. */
static int key_compare(pin_key_t a, pin_key_t b) {
  if (a.text != b.text)
    return a.text ? 1 : -1;
  if (!a.text)
    return a.number < b.number ? -1 : (a.number > b.number ? 1 : 0);
  return jh_span_compare(a.name, b.name);
}

static int pin_order(const void *left, const void *right) {
  return key_compare(key_of_pin(left), key_of_pin(right));
}

static hal_status_t pins_init(jb_vector_t *pins) {
  return jb_vector_init(pins, sizeof(jh_board_pin_t), jb_heap_allocator());
}

static void pins_destroy(jb_vector_t *pins) {
  for (size_t i = 0; i < jb_vector_size(pins); ++i)
    jb_string_destroy(&((jh_board_pin_t *)jb_vector_at(pins, i))->name);
  jb_vector_destroy(pins);
}

static hal_status_t pins_add(jb_vector_t *pins, pin_key_t key) {
  jh_board_pin_t pin = {key.text, key.number, {0}};
  hal_status_t status = HAL_OK;
  if (key.text) {
    status = text_init(&pin.name);
    if (status == HAL_OK)
      status = jh_append_span(&pin.name, key.name);
  }
  if (status == HAL_OK)
    status = jb_vector_append(pins, &pin, 1);
  if (status != HAL_OK)
    jb_string_destroy(&pin.name);
  return status;
}

/* Sort the pins and drop repeated ones, as a set keeps them. */
static void pins_finish(jb_vector_t *pins) {
  const size_t count = jb_vector_size(pins);
  if (count < 2)
    return;
  qsort(jb_vector_data(pins), count, sizeof(jh_board_pin_t), pin_order);
  size_t kept = 1;
  for (size_t i = 1; i < count; ++i) {
    jh_board_pin_t *pin = jb_vector_at(pins, i);
    jh_board_pin_t *last = jb_vector_at(pins, kept - 1);
    if (key_compare(key_of_pin(pin), key_of_pin(last)) == 0) {
      jb_string_destroy(&pin->name);
      continue;
    }
    if (kept != i)
      *(jh_board_pin_t *)jb_vector_at(pins, kept) = *pin;
    ++kept;
  }
  (void)jb_vector_resize(pins, kept);
}

static bool pins_contain(const jb_vector_t *pins, pin_key_t key) {
  size_t low = 0, high = jb_vector_size(pins);
  while (low < high) {
    const size_t middle = low + (high - low) / 2u;
    const int order =
        key_compare(key_of_pin(jb_vector_const_at(pins, middle)), key);
    if (order == 0)
      return true;
    if (order < 0)
      low = middle + 1u;
    else
      high = middle;
  }
  return false;
}

/* The JSON value names a pin of the set. */
static bool value_in_pins(const jb_vector_t *pins, jb_json_tree_view_t value) {
  pin_key_t key;
  return pin_key_of(value, &key) && pins_contain(pins, key);
}

/* Add every pin of source to target and finish it. */
static hal_status_t pins_union(jb_vector_t *target, const jb_vector_t *source) {
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(source); ++i)
    status = pins_add(target, key_of_pin(jb_vector_const_at(source, i)));
  if (status == HAL_OK)
    pins_finish(target);
  return status;
}

/* The pins of set outside within, in pin order. */
static hal_status_t pins_outside(const jb_vector_t *set,
                                 const jb_vector_t *within, jb_vector_t *out) {
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(set); ++i) {
    const pin_key_t key = key_of_pin(jb_vector_const_at(set, i));
    if (!pins_contain(within, key))
      status = pins_add(out, key);
  }
  return status;
}

static hal_status_t key_repr(jb_string_t *out, pin_key_t key) {
  hal_status_t status = HAL_OK;
  if (key.text)
    return jh_python_repr_append(out, key.name);
  jh_put_int64(out, &status, key.number);
  return status;
}

/* repr() of the sorted list of pins. */
static hal_status_t pins_repr(jb_string_t *out, const jb_vector_t *pins) {
  hal_status_t status = jh_append(out, "[");
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(pins); ++i) {
    if (i != 0)
      status = jh_append(out, ", ");
    if (status == HAL_OK)
      status = key_repr(out, key_of_pin(jb_vector_const_at(pins, i)));
  }
  return status == HAL_OK ? jh_append(out, "]") : status;
}

/* fail with the repr of a list of pins. */
static hal_status_t fail_pins(const check_t *c, jb_json_span_t at,
                              const jb_vector_t *pins, const char *expected) {
  jb_string_t got = {0};
  hal_status_t status = text_init(&got);
  if (status == HAL_OK)
    status = pins_repr(&got, pins);
  status = status == HAL_OK ? jh_check_got(c->failure, c->path, at,
                                           jh_string_span(&got), expected)
                            : storage(c, status);
  jb_string_destroy(&got);
  return status;
}

/* ---- Fixed tables ---- */

typedef struct {
  const char *name, *mode;
} role_signal_t;

typedef struct {
  const char *name, *type;
  bool has_min;
  int64_t min;
  const char *const *values;
  size_t value_count;
  bool conditional;
} role_attribute_t;

typedef struct {
  const char *lower, *upper;
} role_order_t;

typedef struct {
  const char *gate;
  const char *const *allowed; /* in code point order */
  size_t allowed_count;
  const char *const *signals;
  size_t signal_count;
  const char *const *attributes;
  size_t attribute_count;
} role_rule_t;

typedef struct {
  const char *role;
  const char *const *bus_kinds; /* in code point order */
  size_t bus_kind_count;
  const char *macro_prefix;
  const role_signal_t *signals;
  size_t signal_count;
  const role_attribute_t *attributes;
  size_t attribute_count;
  const role_order_t *ordered;
  size_t ordered_count;
  const role_rule_t *rules;
  size_t rule_count;
} role_spec_t;

/* The declarative schema of multi-pin bus devices: the checks below are role
 * agnostic, every entry is data describing one device role. */
static const char *const sx1262_bus_kinds[] = {"spi"};
static const role_signal_t sx1262_signals[] = {
    {"sck", "required"},         {"mosi", "required"},
    {"miso", "required"},        {"cs", "required"},
    {"reset", "required"},       {"busy", "required"},
    {"dio1", "required"},        {"rfSwitchA", "conditional"},
    {"rfSwitchB", "conditional"}};
static const char *const sx1262_regulators[] = {"ldo", "dcdc"};
static const char *const sx1262_switch_modes[] = {
    "none", "dio2", "single-gpio", "dio2-single-gpio", "dual-gpio"};
static const char *const sx1262_tcxo_controls[] = {"none", "dio3"};
static const char *const sx1262_tcxo_voltages[] = {"1v6", "1v7", "1v8", "2v2",
                                                   "2v4", "2v7", "3v0", "3v3"};
static const role_attribute_t sx1262_attributes[] = {
    {"minFrequencyHz", "uint32", true, 1, NULL, 0, false},
    {"maxFrequencyHz", "uint32", true, 1, NULL, 0, false},
    {"maxSpiClockHz", "uint32", true, 1, NULL, 0, false},
    {"defaultSpiClockHz", "uint32", true, 1, NULL, 0, false},
    {"minTxPowerDbm", "int8", false, 0, NULL, 0, false},
    {"maxTxPowerDbm", "int8", false, 0, NULL, 0, false},
    {"regulator", "enum", false, 0, sx1262_regulators,
     COUNTOF(sx1262_regulators), false},
    {"rfSwitchMode", "enum", false, 0, sx1262_switch_modes,
     COUNTOF(sx1262_switch_modes), false},
    {"tcxoControl", "enum", false, 0, sx1262_tcxo_controls,
     COUNTOF(sx1262_tcxo_controls), false},
    {"tcxoVoltage", "enum", false, 0, sx1262_tcxo_voltages,
     COUNTOF(sx1262_tcxo_voltages), true},
    {"tcxoStartupUs", "uint32", true, 1, NULL, 0, true},
    {"rfSwitchIdleLevelA", "bool", false, 0, NULL, 0, true},
    {"rfSwitchRxLevelA", "bool", false, 0, NULL, 0, true},
    {"rfSwitchTxLevelA", "bool", false, 0, NULL, 0, true},
    {"rfSwitchIdleLevelB", "bool", false, 0, NULL, 0, true},
    {"rfSwitchRxLevelB", "bool", false, 0, NULL, 0, true},
    {"rfSwitchTxLevelB", "bool", false, 0, NULL, 0, true}};
static const role_order_t sx1262_ordered[] = {
    {"minFrequencyHz", "maxFrequencyHz"},
    {"minTxPowerDbm", "maxTxPowerDbm"},
    {"defaultSpiClockHz", "maxSpiClockHz"}};
static const char *const sx1262_switch_a_modes[] = {"dio2-single-gpio",
                                                    "dual-gpio", "single-gpio"};
static const char *const sx1262_switch_a_signals[] = {"rfSwitchA"};
static const char *const sx1262_switch_a_attributes[] = {
    "rfSwitchIdleLevelA", "rfSwitchRxLevelA", "rfSwitchTxLevelA"};
static const char *const sx1262_switch_b_modes[] = {"dual-gpio"};
static const char *const sx1262_switch_b_signals[] = {"rfSwitchB"};
static const char *const sx1262_switch_b_attributes[] = {
    "rfSwitchIdleLevelB", "rfSwitchRxLevelB", "rfSwitchTxLevelB"};
static const char *const sx1262_tcxo_modes[] = {"dio3"};
static const char *const sx1262_tcxo_attributes[] = {"tcxoVoltage",
                                                     "tcxoStartupUs"};
static const role_rule_t sx1262_rules[] = {
    {"rfSwitchMode", sx1262_switch_a_modes, COUNTOF(sx1262_switch_a_modes),
     sx1262_switch_a_signals, COUNTOF(sx1262_switch_a_signals),
     sx1262_switch_a_attributes, COUNTOF(sx1262_switch_a_attributes)},
    {"rfSwitchMode", sx1262_switch_b_modes, COUNTOF(sx1262_switch_b_modes),
     sx1262_switch_b_signals, COUNTOF(sx1262_switch_b_signals),
     sx1262_switch_b_attributes, COUNTOF(sx1262_switch_b_attributes)},
    {"tcxoControl", sx1262_tcxo_modes, COUNTOF(sx1262_tcxo_modes), NULL, 0,
     sx1262_tcxo_attributes, COUNTOF(sx1262_tcxo_attributes)}};
static const role_spec_t device_roles[] = {
    {"sx1262-radio", sx1262_bus_kinds, COUNTOF(sx1262_bus_kinds),
     "HAL_BOARD_LORA_RADIO", sx1262_signals, COUNTOF(sx1262_signals),
     sx1262_attributes, COUNTOF(sx1262_attributes), sx1262_ordered,
     COUNTOF(sx1262_ordered), sx1262_rules, COUNTOF(sx1262_rules)}};

/* CAN controllers a board can wire channels to: the targets that have them,
 * their instance count and the HAL_CAN_BACKEND_* suffix of the generated
 * table. */
typedef struct {
  const char *id;
  const char *const *targets; /* in code point order */
  size_t target_count;
  int64_t instances;
  const char *backend;
} can_controller_t;

static const char *const fdcan_targets[] = {"stm32g474"};
static const can_controller_t can_controllers[] = {
    {"stm32g474-fdcan", fdcan_targets, COUNTOF(fdcan_targets), 3,
     "STM32G474_FDCAN"}};
static const char *const can_transceivers[] = {"generic", "mcp2562fd"};
static const char *const can_channel_signals[] = {"rx", "tx", "standby"};

/* Devices of one endpoint and the fields each kind adds. */
typedef struct {
  const char *kind;
  const char *const *fields;
  size_t field_count;
} endpoint_kind_t;

static const char *const active_level_fields[] = {"activeLevel"};
static const char *const addressable_fields[] = {"pixelOrder", "protocol"};
static const endpoint_kind_t endpoint_kinds[] = {
    {"addressable", addressable_fields, COUNTOF(addressable_fields)},
    {"component-gpio", active_level_fields, COUNTOF(active_level_fields)},
    {"gpio", active_level_fields, COUNTOF(active_level_fields)}};
static const char bus_device_kind[] = "bus-device";

static const role_attribute_t *role_attribute(const role_spec_t *spec,
                                              jb_json_span_t name) {
  for (size_t i = 0; i < spec->attribute_count; ++i)
    if (jh_span_compare(name, jh_span(spec->attributes[i].name)) == 0)
      return &spec->attributes[i];
  return NULL;
}

static bool role_rule_gates(const role_spec_t *spec, const char *name,
                            bool signal) {
  for (size_t r = 0; r < spec->rule_count; ++r) {
    const role_rule_t *rule = &spec->rules[r];
    const char *const *names = signal ? rule->signals : rule->attributes;
    const size_t count = signal ? rule->signal_count : rule->attribute_count;
    if (span_in(jh_span(name), names, count))
      return true;
  }
  return false;
}

/* "device role '<role>': <detail>" with optional quoted names. */
static hal_status_t fail_role(jh_failure_t *failure, const role_spec_t *spec,
                              const jb_json_span_t *parts, size_t count) {
  jb_string_t message = {0};
  hal_status_t status = text_init(&message);
  jh_put(&message, &status, "device role ");
  if (status == HAL_OK)
    status = jh_python_repr_append(&message, jh_span(spec->role));
  jh_put(&message, &status, ": ");
  for (size_t i = 0; i < count; ++i)
    jh_put_span(&message, &status, parts[i]);
  status = status == HAL_OK
               ? fail_plain(failure, JH_PARTS(jh_string_span(&message)))
               : jh_fail_storage(failure, status);
  jb_string_destroy(&message);
  return status;
}

/* repr() of name, for a role message. */
static hal_status_t quoted(jb_string_t *out, const char *name) {
  jb_string_clear(out);
  return jh_python_repr_append(out, jh_span(name));
}

/* validate_role_registry: the role specs cannot hold unreachable
 * conditionals. */
static hal_status_t check_role(const role_spec_t *spec, jh_failure_t *failure) {
  jb_string_t name = {0}, mode = {0}, list = {0};
  hal_status_t status = text_init(&name);
  if (status == HAL_OK)
    status = text_init(&mode);
  if (status == HAL_OK)
    status = text_init(&list);
  if (status != HAL_OK) {
    status = jh_fail_storage(failure, status);
  } else if (!id_pattern(jh_span(spec->role))) {
    status =
        fail_role(failure, spec, JH_PARTS(jh_span("expected a kebab-case ID")));
  } else if (!macro_pattern(jh_span(spec->macro_prefix))) {
    status = fail_role(failure, spec,
                       JH_PARTS(jh_span("expected a C macro prefix")));
  }
  for (size_t r = 0; status == HAL_OK && r < spec->rule_count; ++r) {
    const role_rule_t *rule = &spec->rules[r];
    const role_attribute_t *gate = role_attribute(spec, jh_span(rule->gate));
    status = quoted(&name, rule->gate);
    if (status != HAL_OK) {
      status = jh_fail_storage(failure, status);
    } else if (gate == NULL || strcmp(gate->type, "enum") != 0) {
      status = fail_role(failure, spec,
                         JH_PARTS(jh_span("gate "), jh_string_span(&name),
                                  jh_span(" is not an enum attribute")));
    } else {
      jb_string_clear(&list);
      jh_put(&list, &status, "[");
      bool any = false;
      for (size_t a = 0; status == HAL_OK && a < rule->allowed_count; ++a) {
        if (span_in(jh_span(rule->allowed[a]), gate->values, gate->value_count))
          continue;
        jh_put(&list, &status, any ? ", " : "");
        if (status == HAL_OK)
          status = jh_python_repr_append(&list, jh_span(rule->allowed[a]));
        any = true;
      }
      jh_put(&list, &status, "]");
      if (status != HAL_OK)
        status = jh_fail_storage(failure, status);
      else if (any)
        status =
            fail_role(failure, spec,
                      JH_PARTS(jh_span("gate "), jh_string_span(&name),
                               jh_span(" values "), jh_string_span(&list),
                               jh_span(" are not declared by the attribute")));
      else if (gate->conditional)
        status = fail_role(failure, spec,
                           JH_PARTS(jh_span("gate "), jh_string_span(&name),
                                    jh_span(" must be required")));
    }
  }
  for (size_t s = 0; status == HAL_OK && s < spec->signal_count; ++s) {
    const role_signal_t *signal = &spec->signals[s];
    const bool conditional = strcmp(signal->mode, "conditional") == 0;
    status = quoted(&name, signal->name);
    if (status == HAL_OK)
      status = quoted(&mode, signal->mode);
    if (status != HAL_OK)
      status = jh_fail_storage(failure, status);
    else if (!camel_pattern(jh_span(signal->name)))
      status = fail_role(failure, spec,
                         JH_PARTS(jh_span("signal "), jh_string_span(&name),
                                  jh_span(" is not camelCase")));
    else if (!conditional && strcmp(signal->mode, "required") != 0)
      status =
          fail_role(failure, spec,
                    JH_PARTS(jh_span("signal "), jh_string_span(&name),
                             jh_span(" has mode "), jh_string_span(&mode)));
    else if (conditional != role_rule_gates(spec, signal->name, true))
      status = fail_role(
          failure, spec,
          JH_PARTS(jh_span("signal "), jh_string_span(&name),
                   jh_span(" must be conditional exactly when a requires rule "
                           "gates it")));
  }
  for (size_t a = 0; status == HAL_OK && a < spec->attribute_count; ++a) {
    const role_attribute_t *attribute = &spec->attributes[a];
    static const char *const types[] = {"uint32", "int8", "bool", "enum"};
    status = quoted(&name, attribute->name);
    if (status != HAL_OK)
      status = jh_fail_storage(failure, status);
    else if (!camel_pattern(jh_span(attribute->name)))
      status = fail_role(failure, spec,
                         JH_PARTS(jh_span("attribute "), jh_string_span(&name),
                                  jh_span(" is not camelCase")));
    else if (!span_in(jh_span(attribute->type), types, COUNTOF(types)))
      status = fail_role(failure, spec,
                         JH_PARTS(jh_span("attribute "), jh_string_span(&name),
                                  jh_span(" has an unknown type")));
    else if (strcmp(attribute->type, "enum") == 0 &&
             attribute->value_count == 0)
      status = fail_role(failure, spec,
                         JH_PARTS(jh_span("attribute "), jh_string_span(&name),
                                  jh_span(" declares no values")));
    else if (attribute->conditional !=
             role_rule_gates(spec, attribute->name, false))
      status = fail_role(
          failure, spec,
          JH_PARTS(jh_span("attribute "), jh_string_span(&name),
                   jh_span(" must be conditional exactly when a requires rule "
                           "gates it")));
  }
  for (size_t o = 0; status == HAL_OK && o < spec->ordered_count; ++o) {
    const char *const pair[] = {spec->ordered[o].lower, spec->ordered[o].upper};
    for (size_t p = 0; status == HAL_OK && p < COUNTOF(pair); ++p) {
      const role_attribute_t *attribute =
          role_attribute(spec, jh_span(pair[p]));
      if (attribute != NULL && (strcmp(attribute->type, "uint32") == 0 ||
                                strcmp(attribute->type, "int8") == 0))
        continue;
      status = quoted(&name, pair[p]);
      status = status == HAL_OK
                   ? fail_role(failure, spec,
                               JH_PARTS(jh_span("ordered pair uses non-numeric "
                                                "attribute "),
                                        jh_string_span(&name)))
                   : jh_fail_storage(failure, status);
    }
  }
  jb_string_destroy(&list);
  jb_string_destroy(&mode);
  jb_string_destroy(&name);
  return status;
}

static hal_status_t check_roles(jh_failure_t *failure) {
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(device_roles); ++i)
    status = check_role(&device_roles[i], failure);
  return status;
}

static const role_spec_t *find_role(jb_json_tree_view_t value) {
  for (size_t i = 0; i < COUNTOF(device_roles); ++i)
    if (string_is(value, device_roles[i].role))
      return &device_roles[i];
  return NULL;
}

static const can_controller_t *find_controller(jb_json_tree_view_t value) {
  for (size_t i = 0; i < COUNTOF(can_controllers); ++i)
    if (string_is(value, can_controllers[i].id))
      return &can_controllers[i];
  return NULL;
}

/* ---- Component registry: config/tooling/board_components.json ---- */

static void component_destroy(jh_board_component_t *component) {
  jb_string_destroy(&component->id);
  jb_string_destroy(&component->slot);
  jh_list_destroy(&component->providers);
}

static const jh_board_component_t *find_component(const jb_vector_t *list,
                                                  jb_json_span_t id) {
  for (size_t i = 0; i < jb_vector_size(list); ++i) {
    const jh_board_component_t *component = jb_vector_const_at(list, i);
    if (jh_span_compare(jh_string_span(&component->id), id) == 0)
      return component;
  }
  return NULL;
}

/* A ToolingContractError: "<source>: <field> <rule>". */
static hal_status_t fail_contract(jh_failure_t *failure, jb_json_span_t source,
                                  const char *field, const char *rule) {
  return fail_plain(failure, JH_PARTS(source, jh_span(": "), jh_span(field),
                                      jh_span(" "), jh_span(rule)));
}

/* require_string: a non-empty string. */
static bool contract_string(jb_json_tree_view_t record, const char *key,
                            jb_json_span_t *text) {
  jb_json_tree_view_t value = {0};
  return member(record, key, &value) && jh_json_string(value, text) &&
         text->length != 0;
}

/* require_string_list: unique non-empty strings, kept in file order. */
static hal_status_t contract_strings(jb_json_tree_view_t record,
                                     const char *key, jb_vector_t *out,
                                     bool *valid) {
  jb_json_tree_view_t value = {0};
  *valid = member(record, key, &value) && is_array(value);
  hal_status_t status = HAL_OK;
  for (size_t i = 0; *valid && status == HAL_OK && i < jh_json_count(value);
       ++i) {
    jb_json_span_t text = {NULL, 0};
    *valid = jh_json_string(jh_json_item(value, i), &text) &&
             text.length != 0 && !jh_list_contains(out, text);
    if (*valid)
      status = jh_list_push(out, text);
  }
  return status;
}

/* load_component_registry. */
static hal_status_t load_components(jh_board_registry_t *registry,
                                    const char *checkout_root, size_t limit,
                                    jh_failure_t *failure) {
  static const char source_name[] = "board_components.json";
  jb_string_t path = {0}, source = {0};
  jb_json_tree_t *tree = NULL;
  jb_json_tree_view_t root = {0}, records = {0}, version = {0};
  jh_json_location_t where;
  jh_failure_t reason;
  jh_json_location_init(&where);
  jh_failure_init(&reason);
  hal_status_t status = jh_fail_storage(failure, text_init(&path));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, text_init(&source));
  if (status == HAL_OK)
    status = jh_absolute_path("config/tooling/board_components.json",
                              checkout_root, &path, failure);
  if (status == HAL_OK && jh_json_read_value(&where, jb_string_data(&path),
                                             limit, &tree, &reason) != HAL_OK)
    status =
        reason.status == HAL_ENOMEM
            ? jh_fail_storage(failure, HAL_ENOMEM)
            : fail_plain(failure, JH_PARTS(jh_span("cannot load "),
                                           jh_string_span(&path), jh_span(": "),
                                           jh_string_span(&reason.message)));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, jb_json_tree_root(tree, &root));
  if (status == HAL_OK && !is_object(root))
    status = fail_plain(failure, JH_PARTS(jh_string_span(&path),
                                          jh_span(": root must be an object")));
  int64_t number = 0;
  if (status == HAL_OK && !(member(root, "schemaVersion", &version) &&
                            jh_json_int64(version, &number) && number == 1))
    status =
        fail_plain(failure, JH_PARTS(jh_string_span(&path),
                                     jh_span(": schemaVersion must be 1")));
  if (status == HAL_OK &&
      !(member(root, "components", &records) && is_array(records)))
    status = fail_contract(failure, jh_span(source_name), "components",
                           "must be an array");
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(records); ++i) {
    const jb_json_tree_view_t record = jh_json_item(records, i);
    char index[32];
    (void)snprintf(index, sizeof index, "[%zu]", i);
    status = jh_fail_storage(
        failure,
        jh_join(&source, JH_PARTS(jh_span(source_name), jh_span(": components"),
                                  jh_span(index))));
    jb_json_span_t id = {NULL, 0}, slot = {NULL, 0};
    jh_board_component_t component = {0};
    bool valid = false;
    if (status != HAL_OK)
      break;
    if (!is_object(record)) {
      status = fail_plain(failure, JH_PARTS(jh_string_span(&source),
                                            jh_span(" must be an object")));
      break;
    }
    if (!contract_string(record, "id", &id)) {
      status = fail_contract(failure, jh_string_span(&source), "id",
                             "must be a non-empty string");
      break;
    }
    if (find_component(&registry->components, id) != NULL) {
      jb_string_t repr = {0};
      status = text_init(&repr);
      if (status == HAL_OK)
        status = jh_python_repr_append(&repr, id);
      status = status == HAL_OK
                   ? fail_plain(failure, JH_PARTS(jh_string_span(&source),
                                                  jh_span(": duplicate id "),
                                                  jh_string_span(&repr)))
                   : jh_fail_storage(failure, status);
      jb_string_destroy(&repr);
      break;
    }
    status = jh_fail_storage(failure, text_init(&component.id));
    if (status == HAL_OK)
      status = jh_fail_storage(failure, text_init(&component.slot));
    if (status == HAL_OK)
      status = jh_fail_storage(failure, jh_list_init(&component.providers));
    if (status == HAL_OK)
      status = jh_fail_storage(
          failure,
          contract_strings(record, "providers", &component.providers, &valid));
    if (status == HAL_OK && !valid)
      status = fail_contract(failure, jh_string_span(&source), "providers",
                             "must contain unique non-empty strings");
    if (status == HAL_OK && !contract_string(record, "slot", &slot))
      status = fail_contract(failure, jh_string_span(&source), "slot",
                             "must be a non-empty string");
    if (status == HAL_OK)
      status = jh_fail_storage(failure, jh_append_span(&component.id, id));
    if (status == HAL_OK)
      status = jh_fail_storage(failure, jh_append_span(&component.slot, slot));
    if (status == HAL_OK)
      status = jh_fail_storage(
          failure, jb_vector_append(&registry->components, &component, 1));
    if (status != HAL_OK)
      component_destroy(&component);
  }
  if (tree != NULL)
    jb_json_tree_destroy(tree);
  jh_failure_destroy(&reason);
  jh_json_location_destroy(&where);
  jb_string_destroy(&source);
  jb_string_destroy(&path);
  return status;
}

/* ---- Descriptors ---- */

static const char *const common_required[] = {
    "schemaVersion", "kind", "id", "displayName", "description", "status"};
static const char *const target_fields[] = {"$schema",
                                            "schemaVersion",
                                            "kind",
                                            "id",
                                            "displayName",
                                            "description",
                                            "status",
                                            "usbDevice",
                                            "architecture",
                                            "hal",
                                            "build",
                                            "gpio",
                                            "clockInputs",
                                            "memory",
                                            "defaultBoard",
                                            "sourceFallbackBoard",
                                            "components",
                                            "requiredFeatures",
                                            "supportedFeatures"};
static const char *const board_fields[] = {
    "$schema",      "schemaVersion", "kind",        "id",
    "displayName",  "description",   "status",      "compatibleTargets",
    "build",        "hal",           "memory",      "gpio",
    "capabilities", "devices",       "peripherals", "components",
    "constraints",  "programming",   "can",         "clockSources"};

/* validate_common. */
static hal_status_t validate_common(const check_t *c, jb_json_tree_view_t root,
                                    bool target) {
  hal_status_t status = target ? exact(c, jh_span("$"), root, common_required,
                                       COUNTOF(common_required), target_fields,
                                       COUNTOF(target_fields))
                               : exact(c, jh_span("$"), root, common_required,
                                       COUNTOF(common_required), board_fields,
                                       COUNTOF(board_fields));
  int64_t number = 0;
  jb_json_tree_view_t value = field(root, "schemaVersion");
  if (status == HAL_OK && !(jh_json_int64(value, &number) && number == 1))
    status = fail_value(c, jh_span("$.schemaVersion"), &value, "1");
  value = field(root, "kind");
  if (status == HAL_OK && !string_is(value, target ? "target" : "board"))
    status = fail_value(c, jh_span("$.kind"), &value,
                        target ? "'target'" : "'board'");
  value = field(root, "id");
  jb_json_span_t id = {NULL, 0};
  if (status == HAL_OK && !(jh_json_string(value, &id) && id_pattern(id)))
    status = fail_value(c, jh_span("$.id"), &value, "a kebab-case identifier");
  const jb_json_span_t stem = jh_span_path_stem(c->path);
  if (status == HAL_OK && jh_span_compare(stem, id) != 0) {
    jb_string_t repr = {0};
    status = storage(c, text_init(&repr));
    if (status == HAL_OK)
      status = storage(c, jh_python_repr_append(&repr, stem));
    if (status == HAL_OK)
      status = fail_value_with(
          c, jh_span("$.id"), &value,
          JH_PARTS(jh_span("the filename stem "), jh_string_span(&repr)));
    jb_string_destroy(&repr);
  }
  static const char *const texts[] = {"$.displayName", "$.description"};
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(texts); ++i) {
    jb_json_span_t text = {NULL, 0}, stripped = {NULL, 0};
    value = field(root, texts[i] + 2);
    if (!(jh_json_string(value, &text) && jh_python_strip(text, &stripped) &&
          stripped.length != 0))
      status = fail_value(c, jh_span(texts[i]), &value, "a non-empty string");
  }
  static const char *const statuses[] = {"supported", "experimental",
                                         "skeleton", "deprecated"};
  value = field(root, "status");
  if (status == HAL_OK && !string_in(value, statuses, COUNTOF(statuses)))
    status = fail_value(c, jh_span("$.status"), &value, "a supported status");
  return status;
}

/* expand_pin_set: a {ranges} or {values} pin set as a set of pins. */
static hal_status_t expand_pin_set(const check_t *c, jb_json_span_t at,
                                   jb_json_tree_view_t value,
                                   jb_vector_t *pins) {
  static const char *const fields[] = {"ranges", "values"};
  hal_status_t status = exact(c, at, value, NULL, 0, fields, COUNTOF(fields));
  jb_json_tree_view_t ranges = {0}, values = {0};
  const bool has_ranges = member(value, "ranges", &ranges);
  const bool has_values = member(value, "values", &values);
  if (status == HAL_OK && has_ranges == has_values)
    status = fail_value(c, at, &value, "exactly one of ranges or values");
  jb_string_t place = {0};
  if (status == HAL_OK)
    status = storage(c, text_init(&place));
  if (status == HAL_OK && has_ranges) {
    status = storage(c, jh_json_path_join(&place, at, ".ranges"));
    if (status == HAL_OK && !(is_array(ranges) && jh_json_count(ranges) != 0))
      status =
          fail_value(c, jh_string_span(&place), &ranges, "a non-empty array");
    size_t total = 0;
    jb_string_t item_place = {0};
    if (status == HAL_OK)
      status = storage(c, text_init(&item_place));
    for (size_t i = 0; status == HAL_OK && i < jh_json_count(ranges); ++i) {
      static const char *const bounds[] = {"first", "last"};
      const jb_json_tree_view_t item = jh_json_item(ranges, i);
      status = storage(
          c, jh_json_path_index(&item_place, jh_string_span(&place), i));
      if (status == HAL_OK)
        status = exact(c, jh_string_span(&item_place), item, bounds,
                       COUNTOF(bounds), bounds, COUNTOF(bounds));
      int64_t first = 0, last = 0;
      if (status == HAL_OK && !(jh_json_int64(field(item, "first"), &first) &&
                                jh_json_int64(field(item, "last"), &last) &&
                                first >= 0 && last >= first))
        status = fail_value(c, jh_string_span(&item_place), &item,
                            "a non-negative ordered integer range");
      if (status == HAL_OK &&
          ((uint64_t)(last - first) >= JH_BOARD_MAX_PINS ||
           total + (size_t)(last - first) + 1u > JH_BOARD_MAX_PINS))
        status = fail_value(c, jh_string_span(&item_place), &item,
                            "a range within 65536 pins");
      if (status == HAL_OK)
        total += (size_t)(last - first) + 1u;
      for (int64_t pin = first; status == HAL_OK && pin <= last; ++pin) {
        const pin_key_t key = {false, pin, {NULL, 0}};
        status = storage(c, pins_add(pins, key));
        if (pin == INT64_MAX)
          break;
      }
    }
    jb_string_destroy(&item_place);
  } else if (status == HAL_OK) {
    status = storage(c, jh_json_path_join(&place, at, ".values"));
    if (status == HAL_OK && !(is_array(values) && jh_json_count(values) != 0))
      status =
          fail_value(c, jh_string_span(&place), &values, "a non-empty array");
    for (size_t i = 0; status == HAL_OK && i < jh_json_count(values); ++i) {
      const jb_json_tree_view_t item = jh_json_item(values, i);
      pin_key_t key;
      if (!pin_key_of(item, &key))
        status = fail_value(c, jh_string_span(&place), &item,
                            "integer or string pin IDs");
      else
        status = storage(c, pins_add(pins, key));
    }
    if (status == HAL_OK)
      pins_finish(pins);
    if (status == HAL_OK && jb_vector_size(pins) != jh_json_count(values))
      status = fail_value(c, jh_string_span(&place), &values, "unique pin IDs");
  }
  if (status == HAL_OK)
    pins_finish(pins);
  jb_string_destroy(&place);
  return status;
}

/* The components a resolved profile selects: jb_string_t names. */
static bool component_selected(const jb_vector_t *components,
                               jb_json_span_t name) {
  return jh_list_contains(components, name);
}

/* validate_endpoint. */
static hal_status_t validate_endpoint(const check_t *c, jb_json_span_t at,
                                      jb_json_tree_view_t endpoint,
                                      const jb_vector_t *valid_pins,
                                      const jb_vector_t *components) {
  if (!is_object(endpoint))
    return fail_value(c, at, &endpoint, "an endpoint object");
  jb_json_tree_view_t domain = {0};
  const bool has_domain = member(endpoint, "domain", &domain);
  jb_string_t place = {0};
  hal_status_t status = storage(c, text_init(&place));
  if (status == HAL_OK && has_domain && string_is(domain, "soc-gpio")) {
    static const char *const fields[] = {"domain", "id"};
    status = exact(c, at, endpoint, fields, COUNTOF(fields), fields,
                   COUNTOF(fields));
    const jb_json_tree_view_t id = field(endpoint, "id");
    if (status == HAL_OK && !value_in_pins(valid_pins, id)) {
      status = storage(c, jh_json_path_join(&place, at, ".id"));
      if (status == HAL_OK)
        status =
            fail_value(c, jh_string_span(&place), &id, "a target valid pin");
    }
  } else if (status == HAL_OK && has_domain &&
             string_is(domain, "component-gpio")) {
    static const char *const fields[] = {"domain", "component", "id", "halPin"};
    status = exact(c, at, endpoint, fields, COUNTOF(fields), fields,
                   COUNTOF(fields));
    const jb_json_tree_view_t component = field(endpoint, "component");
    jb_json_span_t name = {NULL, 0};
    bool known = jh_json_string(component, &name) &&
                 component_selected(components, name);
    if (string_is(component, "cyw43") &&
        (component_selected(components, jh_span("cyw43-pico-pio")) ||
         component_selected(components, jh_span("cyw43-stm32-gspi"))))
      known = true;
    if (status == HAL_OK && !known) {
      status = storage(c, jh_json_path_join(&place, at, ".component"));
      if (status == HAL_OK)
        status = fail_value(c, jh_string_span(&place), &component,
                            "a component selected by the resolved profile");
    }
    int64_t number = 0;
    if (status == HAL_OK &&
        !(jh_json_int64(field(endpoint, "id"), &number) &&
          jh_json_int64(field(endpoint, "halPin"), &number)))
      status = fail_value(c, at, &endpoint, "integer component id and halPin");
  } else if (status == HAL_OK) {
    status = storage(c, jh_json_path_join(&place, at, ".domain"));
    if (status == HAL_OK)
      status =
          fail_value(c, jh_string_span(&place), has_domain ? &domain : NULL,
                     "soc-gpio or component-gpio");
  }
  jb_string_destroy(&place);
  return status;
}

/* A hard reservation: the pin and the owner its reservation names. */
typedef struct {
  jh_board_pin_t pin;
  jb_json_tree_view_t owner;
} reservation_t;

static void reservations_destroy(jb_vector_t *reserved) {
  for (size_t i = 0; i < jb_vector_size(reserved); ++i)
    jb_string_destroy(&((reservation_t *)jb_vector_at(reserved, i))->pin.name);
  jb_vector_destroy(reserved);
}

static reservation_t *find_reservation(const jb_vector_t *reserved,
                                       pin_key_t key) {
  for (size_t i = 0; i < jb_vector_size(reserved); ++i) {
    reservation_t *item = (reservation_t *)jb_vector_const_at(reserved, i);
    if (key_compare(key_of_pin(&item->pin), key) == 0)
      return item;
  }
  return NULL;
}

/* hard_reserved[pin] = owner, the last reservation winning. */
static hal_status_t reserve(jb_vector_t *reserved, jb_json_tree_view_t id,
                            jb_json_tree_view_t owner) {
  pin_key_t key;
  if (!pin_key_of(id, &key))
    return HAL_OK;
  reservation_t *found = find_reservation(reserved, key);
  if (found != NULL) {
    found->owner = owner;
    return HAL_OK;
  }
  reservation_t item = {{key.text, key.number, {0}}, owner};
  hal_status_t status = HAL_OK;
  if (key.text) {
    status = text_init(&item.pin.name);
    if (status == HAL_OK)
      status = jh_append_span(&item.pin.name, key.name);
  }
  if (status == HAL_OK)
    status = jb_vector_append(reserved, &item, 1);
  if (status != HAL_OK)
    jb_string_destroy(&item.pin.name);
  return status;
}

static bool reserved_pin(const jb_vector_t *reserved, jb_json_tree_view_t id) {
  pin_key_t key;
  return pin_key_of(id, &key) && find_reservation(reserved, key) != NULL;
}

/* require_hard_owner: a hard reservation of the pin owned by owner. */
static hal_status_t require_hard_owner(const check_t *c, jb_json_span_t at,
                                       jb_json_tree_view_t id,
                                       const jb_vector_t *reserved,
                                       jb_json_span_t owner) {
  pin_key_t key;
  const reservation_t *found =
      pin_key_of(id, &key) ? find_reservation(reserved, key) : NULL;
  jb_json_span_t text = {NULL, 0};
  if (found != NULL && jh_json_string(found->owner, &text) &&
      jh_span_compare(text, owner) == 0)
    return HAL_OK;
  return fail_value_with(
      c, at, &id,
      JH_PARTS(jh_span("a pin covered by a hard gpio reservation owned by "),
               owner));
}

/* board_device_owner: board.<device id in kebab case>. */
static hal_status_t device_owner(jb_json_span_t id, jb_string_t *out) {
  jb_string_clear(out);
  hal_status_t status = jh_append(out, "board.");
  for (size_t i = 0; status == HAL_OK && i < id.length; ++i) {
    const char c = id.data[i];
    if (upper(c) && i > 0 && lower_or_digit(id.data[i - 1]))
      status = jh_append(out, "-");
    const char lower = upper(c) ? (char)(c - 'A' + 'a') : c;
    if (status == HAL_OK)
      status = jb_string_append(out, &lower, 1);
  }
  return status;
}

/* ---- Devices ---- */

/* The member names of an object, sorted, that names does (in) or does not
 * hold. */
static hal_status_t keys_against(jb_json_tree_view_t object,
                                 const jb_vector_t *names, bool in,
                                 jb_vector_t *out) {
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(object); ++i) {
    const jb_json_span_t key = jh_json_key(jh_json_item(object, i));
    if (jh_list_contains(names, key) == in)
      status = jh_list_push(out, key);
  }
  jh_list_sort(out);
  return status;
}

/* The names of list the object does not have, sorted. */
static hal_status_t names_missing(const jb_vector_t *names,
                                  jb_json_tree_view_t object,
                                  jb_vector_t *out) {
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(names); ++i) {
    jb_json_tree_view_t found = {0};
    const jb_string_t *name = jh_list_at(names, i);
    if (!(is_object(object) &&
          jb_json_tree_find(object, jh_string_span(name), &found) == HAL_OK))
      status = jh_list_push(out, jh_string_span(name));
  }
  jh_list_sort(out);
  return status;
}

/* fail_list with expected "<prefix><role><suffix>" and, with with_list,
 * the repr of the list after it. */
static hal_status_t fail_role_list(const check_t *c, jb_json_span_t at,
                                   const jb_vector_t *list, const char *prefix,
                                   const char *role, const char *suffix,
                                   bool with_list) {
  jb_string_t expected = {0};
  hal_status_t status = text_init(&expected);
  jh_put(&expected, &status, prefix);
  jh_put(&expected, &status, role);
  jh_put(&expected, &status, suffix);
  if (status == HAL_OK && with_list)
    status = jh_repr_list(&expected, list);
  status = status == HAL_OK ? fail_list(c, at, list, jb_string_data(&expected))
                            : storage(c, status);
  jb_string_destroy(&expected);
  return status;
}

/* validate_device_attributes. */
static hal_status_t validate_device_attributes(const check_t *c,
                                               jb_json_span_t at,
                                               const role_spec_t *spec,
                                               jb_json_tree_view_t attributes,
                                               const jb_vector_t *active) {
  if (!is_object(attributes))
    return fail_value(c, at, &attributes, "an object");
  jb_vector_t expected = {0}, missing = {0}, unknown = {0};
  jb_string_t place = {0}, text = {0};
  hal_status_t status = jh_list_init(&expected);
  if (status == HAL_OK)
    status = jh_list_init(&missing);
  if (status == HAL_OK)
    status = jh_list_init(&unknown);
  if (status == HAL_OK)
    status = text_init(&place);
  if (status == HAL_OK)
    status = text_init(&text);
  for (size_t i = 0; status == HAL_OK && i < spec->attribute_count; ++i) {
    const role_attribute_t *attribute = &spec->attributes[i];
    if (!attribute->conditional ||
        jh_list_contains(active, jh_span(attribute->name)))
      status = jh_list_push(&expected, jh_span(attribute->name));
  }
  if (status == HAL_OK)
    status = names_missing(&expected, attributes, &missing);
  if (status == HAL_OK)
    status = keys_against(attributes, &expected, false, &unknown);
  status = storage(c, status);
  if (status == HAL_OK && jb_vector_size(&missing) != 0)
    status = fail_role_list(c, at, &missing, "required ", spec->role,
                            " attributes ", true);
  if (status == HAL_OK && jb_vector_size(&unknown) != 0)
    status = fail_role_list(c, at, &unknown,
                            "only attributes required by the active ",
                            spec->role, " configuration", false);
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(attributes); ++i) {
    const jb_json_tree_view_t value = jh_json_item(attributes, i);
    const jb_json_span_t name = jh_json_key(value);
    const role_attribute_t *attribute = role_attribute(spec, name);
    status = storage(c, jh_join(&place, JH_PARTS(at, jh_span("."), name)));
    if (status != HAL_OK || attribute == NULL)
      break;
    const jb_json_span_t item = jh_string_span(&place);
    if (strcmp(attribute->type, "bool") == 0) {
      if (!is_bool(value))
        status = fail_value(c, item, &value, "a boolean");
    } else if (strcmp(attribute->type, "enum") == 0) {
      if (!string_in(value, attribute->values, attribute->value_count)) {
        jb_string_clear(&text);
        jh_put(&text, &status, "one of [");
        for (size_t v = 0; status == HAL_OK && v < attribute->value_count;
             ++v) {
          jh_put(&text, &status, v == 0 ? "" : ", ");
          if (status == HAL_OK)
            status =
                jh_python_repr_append(&text, jh_span(attribute->values[v]));
        }
        jh_put(&text, &status, "]");
        status = status == HAL_OK
                     ? fail_value(c, item, &value, jb_string_data(&text))
                     : storage(c, status);
      }
    } else {
      const bool wide = strcmp(attribute->type, "uint32") == 0;
      int64_t low = wide ? 0 : -128;
      const int64_t high = wide ? 0xFFFFFFFF : 127;
      if (attribute->has_min && attribute->min > low)
        low = attribute->min;
      int64_t number = 0;
      if (!jh_json_int64(value, &number)) {
        status =
            fail_value_with(c, item, &value,
                            JH_PARTS(jh_span("an "), jh_span(attribute->type),
                                     jh_span(" integer")));
      } else if (number < low || number > high) {
        jb_string_clear(&text);
        jh_put(&text, &status, "an ");
        jh_put(&text, &status, attribute->type);
        jh_put(&text, &status, " integer in [");
        jh_put_int64(&text, &status, low);
        jh_put(&text, &status, ", ");
        jh_put_int64(&text, &status, high);
        jh_put(&text, &status, "]");
        status = status == HAL_OK
                     ? fail_value(c, item, &value, jb_string_data(&text))
                     : storage(c, status);
      }
    }
  }
  for (size_t o = 0; status == HAL_OK && o < spec->ordered_count; ++o) {
    jb_json_tree_view_t lower = {0}, upper_value = {0};
    int64_t low = 0, high = 0;
    if (!member(attributes, spec->ordered[o].lower, &lower) ||
        !member(attributes, spec->ordered[o].upper, &upper_value) ||
        !jh_json_int64(lower, &low) || !jh_json_int64(upper_value, &high) ||
        low <= high)
      continue;
    jb_string_clear(&text);
    jh_put(&text, &status, "a value not greater than ");
    jh_put(&text, &status, spec->ordered[o].upper);
    jh_put(&text, &status, " (");
    jh_put_int64(&text, &status, high);
    jh_put(&text, &status, ")");
    if (status == HAL_OK)
      status = jh_join(
          &place, JH_PARTS(at, jh_span("."), jh_span(spec->ordered[o].lower)));
    status = status == HAL_OK ? fail_value(c, jh_string_span(&place), &lower,
                                           jb_string_data(&text))
                              : storage(c, status);
  }
  jb_string_destroy(&text);
  jb_string_destroy(&place);
  jh_list_destroy(&unknown);
  jh_list_destroy(&missing);
  jh_list_destroy(&expected);
  return status;
}

/* A text key of an endpoint: domain, pin and component. */
static hal_status_t endpoint_key(jb_json_tree_view_t endpoint,
                                 jb_string_t *out) {
  jb_json_span_t text = {NULL, 0};
  pin_key_t pin;
  jb_string_clear(out);
  hal_status_t status = HAL_OK;
  if (jh_json_string(field(endpoint, "domain"), &text))
    jh_put_span(out, &status, text);
  jh_put(out, &status, "\x1f");
  if (pin_key_of(field(endpoint, "id"), &pin)) {
    jh_put(out, &status, pin.text ? "s" : "i");
    if (pin.text)
      jh_put_span(out, &status, pin.name);
    else
      jh_put_int64(out, &status, pin.number);
  }
  jh_put(out, &status, "\x1f");
  jb_json_tree_view_t component = {0};
  if (member(endpoint, "component", &component) &&
      jh_json_string(component, &text))
    jh_put_span(out, &status, text);
  return status;
}

/* Where a bus device validation looks pins up. */
typedef struct {
  const jb_vector_t *valid_pins, *components, *reserved;
} pins_context_t;

/* The signals and attributes the requires rules activate. */
static hal_status_t active_names(const role_spec_t *spec,
                                 jb_json_tree_view_t attributes,
                                 jb_vector_t *signals, jb_vector_t *activated) {
  hal_status_t status = HAL_OK;
  for (size_t r = 0; status == HAL_OK && r < spec->rule_count; ++r) {
    const role_rule_t *rule = &spec->rules[r];
    jb_json_tree_view_t gate = {0};
    if (!is_object(attributes) || !member(attributes, rule->gate, &gate) ||
        !string_in(gate, rule->allowed, rule->allowed_count))
      continue;
    for (size_t s = 0; status == HAL_OK && s < rule->signal_count; ++s)
      status = jh_list_push(signals, jh_span(rule->signals[s]));
    for (size_t a = 0; status == HAL_OK && a < rule->attribute_count; ++a)
      status = jh_list_push(activated, jh_span(rule->attributes[a]));
  }
  return status;
}

/* validate_bus_device. */
static hal_status_t validate_bus_device(const check_t *c, jb_json_span_t at,
                                        jb_json_tree_view_t device,
                                        const pins_context_t *pins,
                                        jb_json_span_t owner) {
  static const char *const fields[] = {"kind", "role", "bus", "signals",
                                       "attributes"};
  static const char *const bus_fields[] = {"kind", "index"};
  hal_status_t status =
      exact(c, at, device, fields, COUNTOF(fields), fields, COUNTOF(fields));
  jb_string_t place = {0}, key = {0}, text = {0};
  jb_vector_t names = {0}, unknown = {0}, active_signals = {0},
              active_attributes = {0}, expected = {0}, missing = {0},
              seen_keys = {0}, seen_names = {0};
  if (status == HAL_OK)
    status = storage(c, text_init(&place));
  if (status == HAL_OK)
    status = storage(c, text_init(&key));
  if (status == HAL_OK)
    status = storage(c, text_init(&text));
  jb_vector_t *lists[] = {
      &names,    &unknown, &active_signals, &active_attributes,
      &expected, &missing, &seen_keys,      &seen_names};
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(lists); ++i)
    status = storage(c, jh_list_init(lists[i]));
  const jb_json_tree_view_t role = field(device, "role");
  const role_spec_t *spec = status == HAL_OK ? find_role(role) : NULL;
  if (status == HAL_OK && spec == NULL) {
    status = storage(c, jh_json_path_join(&place, at, ".role"));
    if (status == HAL_OK)
      status =
          fail_value(c, jh_string_span(&place), &role, "a known device role");
  }
  const jb_json_tree_view_t bus = field(device, "bus");
  if (status == HAL_OK)
    status = storage(c, jh_json_path_join(&place, at, ".bus"));
  if (status == HAL_OK)
    status = exact(c, jh_string_span(&place), bus, bus_fields,
                   COUNTOF(bus_fields), bus_fields, COUNTOF(bus_fields));
  const jb_json_tree_view_t bus_kind = field(bus, "kind");
  if (status == HAL_OK &&
      !string_in(bus_kind, spec->bus_kinds, spec->bus_kind_count)) {
    jb_string_clear(&text);
    jh_put(&text, &status, "one of [");
    for (size_t i = 0; status == HAL_OK && i < spec->bus_kind_count; ++i) {
      jh_put(&text, &status, i == 0 ? "" : ", ");
      if (status == HAL_OK)
        status = jh_python_repr_append(&text, jh_span(spec->bus_kinds[i]));
    }
    jh_put(&text, &status, "]");
    if (status == HAL_OK)
      status = jh_json_path_join(&place, at, ".bus.kind");
    status = status == HAL_OK ? fail_value(c, jh_string_span(&place), &bus_kind,
                                           jb_string_data(&text))
                              : storage(c, status);
  }
  const jb_json_tree_view_t index = field(bus, "index");
  int64_t number = 0;
  if (status == HAL_OK && !jh_json_int64(index, &number)) {
    status = storage(c, jh_json_path_join(&place, at, ".bus.index"));
    if (status == HAL_OK)
      status =
          fail_value(c, jh_string_span(&place), &index, "an integer bus index");
  }
  if (status == HAL_OK && (number < 0 || number > 0xFE)) {
    status = storage(c, jh_json_path_join(&place, at, ".bus.index"));
    if (status == HAL_OK)
      status = fail_value(c, jh_string_span(&place), &index,
                          "a bus index in [0, 254]");
  }
  const jb_json_tree_view_t signals = field(device, "signals");
  if (status == HAL_OK)
    status = storage(c, jh_json_path_join(&place, at, ".signals"));
  if (status == HAL_OK && !is_object(signals))
    status = fail_value(c, jh_string_span(&place), &signals, "an object");
  for (size_t i = 0; status == HAL_OK && i < spec->signal_count; ++i)
    status = storage(c, jh_list_push(&names, jh_span(spec->signals[i].name)));
  if (status == HAL_OK)
    status = storage(c, keys_against(signals, &names, false, &unknown));
  if (status == HAL_OK && jb_vector_size(&unknown) != 0)
    status =
        fail_role_list(c, jh_string_span(&place), &unknown,
                       "only signals declared by role ", spec->role, "", false);
  const jb_json_tree_view_t attributes = field(device, "attributes");
  if (status == HAL_OK)
    status = storage(
        c, active_names(spec, attributes, &active_signals, &active_attributes));
  if (status == HAL_OK)
    status = storage(c, jh_json_path_join(&place, at, ".attributes"));
  if (status == HAL_OK)
    status = validate_device_attributes(c, jh_string_span(&place), spec,
                                        attributes, &active_attributes);
  for (size_t i = 0; status == HAL_OK && i < spec->signal_count; ++i)
    if (strcmp(spec->signals[i].mode, "required") == 0 ||
        jh_list_contains(&active_signals, jh_span(spec->signals[i].name)))
      status =
          storage(c, jh_list_push(&expected, jh_span(spec->signals[i].name)));
  if (status == HAL_OK)
    status = storage(c, names_missing(&expected, signals, &missing));
  if (status == HAL_OK)
    status = storage(c, jh_json_path_join(&place, at, ".signals"));
  if (status == HAL_OK && jb_vector_size(&missing) != 0)
    status = fail_role_list(c, jh_string_span(&place), &missing, "required ",
                            spec->role, " signals ", true);
  if (status == HAL_OK) {
    jh_list_destroy(&unknown);
    status = storage(c, jh_list_init(&unknown));
  }
  if (status == HAL_OK)
    status = storage(c, keys_against(signals, &expected, false, &unknown));
  if (status == HAL_OK && jb_vector_size(&unknown) != 0)
    status = fail_role_list(c, jh_string_span(&place), &unknown,
                            "only signals required by the active ", spec->role,
                            " configuration", false);
  /* The signals in name order. */
  jh_list_destroy(&names);
  if (status == HAL_OK)
    status = storage(c, jh_list_init(&names));
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(signals); ++i)
    status =
        storage(c, jh_list_push(&names, jh_json_key(jh_json_item(signals, i))));
  jh_list_sort(&names);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&names); ++i) {
    const jb_json_span_t name = jh_string_span(jh_list_at(&names, i));
    jb_json_tree_view_t endpoint = {0};
    (void)jb_json_tree_find(signals, name, &endpoint);
    status =
        storage(c, jh_join(&place, JH_PARTS(at, jh_span(".signals."), name)));
    const jb_json_span_t signal_at = jh_string_span(&place);
    if (status == HAL_OK)
      status = validate_endpoint(c, signal_at, endpoint, pins->valid_pins,
                                 pins->components);
    if (status == HAL_OK)
      status = storage(c, endpoint_key(endpoint, &key));
    for (size_t s = 0; status == HAL_OK && s < jb_vector_size(&seen_keys); ++s)
      if (jh_span_compare(jh_string_span(jh_list_at(&seen_keys, s)),
                          jh_string_span(&key)) == 0)
        status = fail_value_with(
            c, signal_at, &endpoint,
            JH_PARTS(jh_span("a pin not already used by "),
                     jh_string_span(jh_list_at(&seen_names, s))));
    if (status == HAL_OK)
      status = storage(c, jh_list_push(&seen_keys, jh_string_span(&key)));
    if (status == HAL_OK)
      status = storage(c, jh_list_push(&seen_names, name));
    if (status == HAL_OK && string_is(field(endpoint, "domain"), "soc-gpio"))
      status = require_hard_owner(c, signal_at, field(endpoint, "id"),
                                  pins->reserved, owner);
  }
  for (size_t i = 0; i < COUNTOF(lists); ++i)
    jh_list_destroy(lists[i]);
  jb_string_destroy(&text);
  jb_string_destroy(&key);
  jb_string_destroy(&place);
  return status;
}

/* validate_device. */
static hal_status_t validate_device(const check_t *c, jb_json_span_t at,
                                    jb_json_tree_view_t device,
                                    const pins_context_t *pins,
                                    jb_json_span_t owner) {
  jb_json_tree_view_t kind = {0};
  jb_json_span_t kind_text = {NULL, 0};
  if (!is_object(device) || !member(device, "kind", &kind) ||
      !jh_json_string(kind, &kind_text))
    return fail_value(c, at, &device, "a device object with a kind");
  if (jh_span_compare(kind_text, jh_span(bus_device_kind)) == 0)
    return validate_bus_device(c, at, device, pins, owner);
  const endpoint_kind_t *spec = NULL;
  for (size_t i = 0; i < COUNTOF(endpoint_kinds); ++i)
    if (jh_span_compare(kind_text, jh_span(endpoint_kinds[i].kind)) == 0)
      spec = &endpoint_kinds[i];
  jb_string_t place = {0};
  hal_status_t status = storage(c, text_init(&place));
  if (status == HAL_OK && spec == NULL) {
    status = storage(c, jh_json_path_join(&place, at, ".kind"));
    if (status == HAL_OK)
      status = fail_value(c, jh_string_span(&place), &kind,
                          "one of ['addressable', 'bus-device', "
                          "'component-gpio', 'gpio']");
  }
  static const char *const required[] = {"kind", "endpoint"};
  const char *allowed[4] = {"kind", "endpoint", NULL, NULL};
  size_t allowed_count = 2;
  for (size_t i = 0; spec != NULL && i < spec->field_count; ++i)
    allowed[allowed_count++] = spec->fields[i];
  if (status == HAL_OK)
    status = exact(c, at, device, required, COUNTOF(required), allowed,
                   allowed_count);
  const jb_json_tree_view_t endpoint = field(device, "endpoint");
  if (status == HAL_OK)
    status = storage(c, jh_json_path_join(&place, at, ".endpoint"));
  if (status == HAL_OK)
    status = validate_endpoint(c, jh_string_span(&place), endpoint,
                               pins->valid_pins, pins->components);
  const jb_json_tree_view_t id = field(endpoint, "id");
  if (status == HAL_OK && string_is(field(endpoint, "domain"), "soc-gpio") &&
      reserved_pin(pins->reserved, id))
    status = require_hard_owner(c, jh_string_span(&place), id, pins->reserved,
                                owner);
  jb_string_destroy(&place);
  return status;
}

/* ---- CAN ---- */

/* The compatibleTargets strings of a board are all among names. */
static bool targets_within(jb_json_tree_view_t board, const char *const *names,
                           size_t count) {
  const jb_json_tree_view_t targets = field(board, "compatibleTargets");
  for (size_t i = 0; i < jh_json_count(targets); ++i)
    if (!string_in(jh_json_item(targets, i), names, count))
      return false;
  return true;
}

/* validate_can_channel. */
static hal_status_t validate_can_channel(const check_t *c, jb_json_span_t at,
                                         jb_json_tree_view_t board,
                                         jb_json_tree_view_t channel,
                                         const can_controller_t **out) {
  static const char *const required[] = {
      "controller", "instance", "rx", "tx", "transceiver", "maxBitrateHz"};
  static const char *const allowed[] = {
      "controller", "instance",          "rx",          "tx",
      "standby",    "standbyActiveHigh", "transceiver", "maxBitrateHz"};
  hal_status_t status = exact(c, at, channel, required, COUNTOF(required),
                              allowed, COUNTOF(allowed));
  jb_string_t place = {0}, text = {0};
  if (status == HAL_OK)
    status = storage(c, text_init(&place));
  if (status == HAL_OK)
    status = storage(c, text_init(&text));
  const jb_json_tree_view_t controller_value = field(channel, "controller");
  const can_controller_t *controller =
      status == HAL_OK ? find_controller(controller_value) : NULL;
  if (status == HAL_OK)
    status = storage(c, jh_json_path_join(&place, at, ".controller"));
  if (status == HAL_OK && controller == NULL)
    status = fail_value(c, jh_string_span(&place), &controller_value,
                        "one of ['stm32g474-fdcan']");
  if (status == HAL_OK &&
      !targets_within(board, controller->targets, controller->target_count))
    status = fail_value(c, jh_string_span(&place), &controller_value,
                        "a controller present on every compatible target");
  const jb_json_tree_view_t instance = field(channel, "instance");
  int64_t number = 0;
  if (status == HAL_OK && !(jh_json_int64(instance, &number) && number >= 1 &&
                            number <= controller->instances)) {
    jh_put(&text, &status, "an instance in [1, ");
    jh_put_int64(&text, &status, controller->instances);
    jh_put(&text, &status, "]");
    if (status == HAL_OK)
      status = jh_json_path_join(&place, at, ".instance");
    status = status == HAL_OK ? fail_value(c, jh_string_span(&place), &instance,
                                           jb_string_data(&text))
                              : storage(c, status);
  }
  jb_json_tree_view_t standby = {0}, active_high = {0};
  const bool has_standby = member(channel, "standby", &standby);
  const bool has_active_high =
      member(channel, "standbyActiveHigh", &active_high);
  if (status == HAL_OK && has_standby != has_active_high) {
    jb_vector_t keys = {0}, none = {0};
    status = storage(c, jh_list_init(&keys));
    if (status == HAL_OK)
      status = storage(c, jh_list_init(&none));
    if (status == HAL_OK)
      status = storage(c, keys_against(channel, &none, false, &keys));
    if (status == HAL_OK)
      status =
          fail_list(c, at, &keys, "standby and standbyActiveHigh together");
    jh_list_destroy(&none);
    jh_list_destroy(&keys);
  }
  if (status == HAL_OK && has_active_high && !is_bool(active_high)) {
    status = storage(c, jh_json_path_join(&place, at, ".standbyActiveHigh"));
    if (status == HAL_OK)
      status = fail_value(c, jh_string_span(&place), &active_high, "a boolean");
  }
  const jb_json_tree_view_t transceiver = field(channel, "transceiver");
  if (status == HAL_OK &&
      !string_in(transceiver, can_transceivers, COUNTOF(can_transceivers))) {
    status = storage(c, jh_json_path_join(&place, at, ".transceiver"));
    if (status == HAL_OK)
      status = fail_value(c, jh_string_span(&place), &transceiver,
                          "one of ['generic', 'mcp2562fd']");
  }
  const jb_json_tree_view_t rate = field(channel, "maxBitrateHz");
  if (status == HAL_OK &&
      !(jh_json_int64(rate, &number) && number >= 1 && number <= 0xFFFFFFFF)) {
    status = storage(c, jh_json_path_join(&place, at, ".maxBitrateHz"));
    if (status == HAL_OK)
      status = fail_value(c, jh_string_span(&place), &rate,
                          "a uint32 integer of at least 1");
  }
  *out = controller;
  jb_string_destroy(&text);
  jb_string_destroy(&place);
  return status;
}

/* validate_can: every signal a target pin, used once and hard-reserved by
 * board.can; every instance wired at most once. Pin routing to an instance
 * is the controller backend's run time check. */
static hal_status_t validate_can(const check_t *c, jb_json_tree_view_t board,
                                 const pins_context_t *pins) {
  jb_json_tree_view_t can = {0};
  if (!member(board, "can", &can))
    return HAL_OK;
  static const char *const fields[] = {"channels"};
  hal_status_t status = exact(c, jh_span("$.can"), can, fields, COUNTOF(fields),
                              fields, COUNTOF(fields));
  const jb_json_tree_view_t channels = field(can, "channels");
  if (status == HAL_OK && !(is_array(channels) && jh_json_count(channels) != 0))
    status = fail_value(c, jh_span("$.can.channels"), &channels,
                        "a non-empty channel array");
  jb_string_t at = {0}, place = {0};
  jb_vector_t wired = {0}, wired_by = {0}, used = {0}, used_by = {0};
  if (status == HAL_OK)
    status = storage(c, text_init(&at));
  if (status == HAL_OK)
    status = storage(c, text_init(&place));
  if (status == HAL_OK)
    status = storage(c, jh_list_init(&wired));
  if (status == HAL_OK)
    status = storage(c, jh_list_init(&wired_by));
  if (status == HAL_OK)
    status = storage(c, pins_init(&used));
  if (status == HAL_OK)
    status = storage(c, jh_list_init(&used_by));
  for (size_t index = 0; status == HAL_OK && index < jh_json_count(channels);
       ++index) {
    const jb_json_tree_view_t channel = jh_json_item(channels, index);
    const can_controller_t *controller = NULL;
    status =
        storage(c, jh_json_path_index(&at, jh_span("$.can.channels"), index));
    if (status == HAL_OK)
      status = validate_can_channel(c, jh_string_span(&at), board, channel,
                                    &controller);
    int64_t instance = 0;
    (void)jh_json_int64(field(channel, "instance"), &instance);
    jb_string_clear(&place);
    if (status == HAL_OK) {
      jh_put(&place, &status, controller->id);
      jh_put(&place, &status, "\x1f");
      jh_put_int64(&place, &status, instance);
      status = storage(c, status);
    }
    for (size_t w = 0; status == HAL_OK && w < jb_vector_size(&wired); ++w)
      if (jh_span_compare(jh_string_span(jh_list_at(&wired, w)),
                          jh_string_span(&place)) == 0) {
        jb_string_t where = {0};
        const jb_json_tree_view_t value = field(channel, "instance");
        status = storage(c, text_init(&where));
        if (status == HAL_OK)
          status = storage(
              c, jh_json_path_join(&where, jh_string_span(&at), ".instance"));
        if (status == HAL_OK)
          status = fail_value_with(
              c, jh_string_span(&where), &value,
              JH_PARTS(jh_span("an instance not already wired by channel "),
                       jh_string_span(jh_list_at(&wired_by, w))));
        jb_string_destroy(&where);
      }
    char number[24];
    (void)snprintf(number, sizeof number, "%zu", index);
    if (status == HAL_OK)
      status = storage(c, jh_list_push(&wired, jh_string_span(&place)));
    if (status == HAL_OK)
      status = storage(c, jh_list_push(&wired_by, jh_span(number)));
    for (size_t s = 0; status == HAL_OK && s < COUNTOF(can_channel_signals);
         ++s) {
      jb_json_tree_view_t endpoint = {0};
      if (!member(channel, can_channel_signals[s], &endpoint))
        continue;
      status = storage(
          c, jh_join(&place, JH_PARTS(jh_string_span(&at), jh_span("."),
                                      jh_span(can_channel_signals[s]))));
      const jb_json_span_t signal_at = jh_string_span(&place);
      if (status == HAL_OK)
        status = validate_endpoint(c, signal_at, endpoint, pins->valid_pins,
                                   pins->components);
      if (status == HAL_OK && !string_is(field(endpoint, "domain"), "soc-gpio"))
        status = fail_value(c, signal_at, &endpoint, "a SoC GPIO endpoint");
      const jb_json_tree_view_t id = field(endpoint, "id");
      pin_key_t key;
      const bool keyed = pin_key_of(id, &key);
      for (size_t u = 0; status == HAL_OK && keyed && u < jb_vector_size(&used);
           ++u)
        if (key_compare(key_of_pin(jb_vector_const_at(&used, u)), key) == 0)
          status = fail_value_with(
              c, signal_at, &id,
              JH_PARTS(jh_span("a pin not already used by "),
                       jh_string_span(jh_list_at(&used_by, u))));
      if (status == HAL_OK && keyed)
        status = storage(c, pins_add(&used, key));
      if (status == HAL_OK && keyed)
        status = storage(c, jh_list_push(&used_by, signal_at));
      if (status == HAL_OK)
        status = require_hard_owner(c, signal_at, id, pins->reserved,
                                    jh_span("board.can"));
    }
  }
  jh_list_destroy(&used_by);
  pins_destroy(&used);
  jh_list_destroy(&wired_by);
  jh_list_destroy(&wired);
  jb_string_destroy(&place);
  jb_string_destroy(&at);
  return status;
}

/* ---- Components and USB ---- */

/* validate_components: the ids, appended to ids. */
static hal_status_t validate_components(const check_t *c, jb_json_span_t at,
                                        jb_json_tree_view_t components,
                                        jb_json_span_t provider,
                                        const jb_vector_t *registry,
                                        jb_vector_t *ids) {
  if (!is_object(components))
    return fail_value(c, at, &components, "an object");
  jb_string_t place = {0}, text = {0};
  jb_vector_t slots = {0}, owners = {0};
  hal_status_t status = storage(c, text_init(&place));
  if (status == HAL_OK)
    status = storage(c, text_init(&text));
  if (status == HAL_OK)
    status = storage(c, jh_list_init(&slots));
  if (status == HAL_OK)
    status = storage(c, jh_list_init(&owners));
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(components); ++i) {
    const jb_json_tree_view_t config = jh_json_item(components, i);
    const jb_json_span_t id = jh_json_key(config);
    const jh_board_component_t *entry = find_component(registry, id);
    status = storage(c, jh_join(&place, JH_PARTS(at, jh_span("."), id)));
    if (status == HAL_OK && entry == NULL)
      status = fail_value(c, jh_string_span(&place), &config,
                          "a known component ID");
    if (status == HAL_OK && !jh_list_contains(&entry->providers, provider)) {
      jb_vector_t sorted = {0};
      status = storage(c, jh_list_init(&sorted));
      for (size_t p = 0;
           status == HAL_OK && p < jb_vector_size(&entry->providers); ++p)
        status = storage(c, jh_list_push(&sorted, jh_string_span(jh_list_at(
                                                      &entry->providers, p))));
      jh_list_sort(&sorted);
      jb_string_clear(&text);
      jh_put(&text, &status, "one of ");
      if (status == HAL_OK)
        status = jh_repr_list(&text, &sorted);
      status = status == HAL_OK
                   ? fail_text_with(c, jh_string_span(&place), provider,
                                    JH_PARTS(jh_string_span(&text)))
                   : storage(c, status);
      jh_list_destroy(&sorted);
    }
    static const char *const fields[] = {"mode"};
    if (status == HAL_OK)
      status = exact(c, jh_string_span(&place), config, fields, COUNTOF(fields),
                     fields, COUNTOF(fields));
    static const char *const modes[] = {"required", "board-owned",
                                        "feature-gated"};
    const jb_json_tree_view_t mode = field(config, "mode");
    if (status == HAL_OK && !string_in(mode, modes, COUNTOF(modes))) {
      status = storage(
          c, jh_join(&place, JH_PARTS(at, jh_span("."), id, jh_span(".mode"))));
      if (status == HAL_OK)
        status = fail_value(c, jh_string_span(&place), &mode,
                            "required, board-owned, or feature-gated");
    }
    const jb_json_span_t slot =
        status == HAL_OK ? jh_string_span(&entry->slot) : jh_span("");
    for (size_t s = 0; status == HAL_OK && s < jb_vector_size(&slots); ++s) {
      const jb_json_span_t holder = jh_string_span(jh_list_at(&owners, s));
      if (jh_span_compare(jh_string_span(jh_list_at(&slots, s)), slot) != 0 ||
          jh_span_compare(holder, id) == 0)
        continue;
      jb_vector_t pair = {0};
      status = storage(c, jh_list_init(&pair));
      if (status == HAL_OK)
        status = storage(c, jh_list_push(&pair, holder));
      if (status == HAL_OK)
        status = storage(c, jh_list_push(&pair, id));
      jb_string_clear(&text);
      jh_put(&text, &status, "one component in exclusive slot ");
      jh_put_span(&text, &status, slot);
      status = status == HAL_OK ? fail_list(c, at, &pair, jb_string_data(&text))
                                : storage(c, status);
      jh_list_destroy(&pair);
    }
    if (status == HAL_OK)
      status = storage(c, jh_list_push(&slots, slot));
    if (status == HAL_OK)
      status = storage(c, jh_list_push(&owners, id));
    if (status == HAL_OK && !jh_list_contains(ids, id))
      status = storage(c, jh_list_push(ids, id));
  }
  jh_list_destroy(&owners);
  jh_list_destroy(&slots);
  jb_string_destroy(&text);
  jb_string_destroy(&place);
  return status;
}

/* validate_usb_ids. */
static hal_status_t validate_usb_ids(const check_t *c, jb_json_span_t at,
                                     jb_json_tree_view_t value) {
  static const char *const fields[] = {"vid", "pid"};
  hal_status_t status =
      exact(c, at, value, fields, COUNTOF(fields), fields, COUNTOF(fields));
  jb_string_t place = {0};
  if (status == HAL_OK)
    status = storage(c, text_init(&place));
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(fields); ++i) {
    const jb_json_tree_view_t id = field(value, fields[i]);
    int64_t number = 0;
    if (jh_json_int64(id, &number) && number >= 0 && number <= 0xFFFF)
      continue;
    status = storage(
        c, jh_join(&place, JH_PARTS(at, jh_span("."), jh_span(fields[i]))));
    if (status == HAL_OK)
      status =
          fail_value(c, jh_string_span(&place), &id, "a 16-bit USB identifier");
  }
  jb_string_destroy(&place);
  return status;
}

/* ---- What a load reads besides the descriptors ---- */

/* The first descriptor holding a value: the key list and paths in parallel. */
typedef struct {
  jb_vector_t keys, paths;
} seen_t;

static hal_status_t seen_init(seen_t *seen) {
  hal_status_t status = jh_list_init(&seen->keys);
  return status == HAL_OK ? jh_list_init(&seen->paths) : status;
}

static void seen_destroy(seen_t *seen) {
  jh_list_destroy(&seen->paths);
  jh_list_destroy(&seen->keys);
}

/* The path that first held key, or NULL. */
static const jb_string_t *seen_path(const seen_t *seen, jb_json_span_t key) {
  for (size_t i = 0; i < jb_vector_size(&seen->keys); ++i)
    if (jh_span_compare(jh_string_span(jh_list_at(&seen->keys, i)), key) == 0)
      return jh_list_at(&seen->paths, i);
  return NULL;
}

static hal_status_t seen_add(seen_t *seen, jb_json_span_t key,
                             jb_json_span_t path) {
  hal_status_t status = jh_list_push(&seen->keys, key);
  return status == HAL_OK ? jh_list_push(&seen->paths, path) : status;
}

typedef struct {
  jh_board_registry_t *registry;
  const char *checkout_root;
  size_t limit;
  jh_failure_t *failure;
  bool features_loaded;
  jh_feature_registry_t features;
  jh_json_location_t schema_where;
  jh_schema_files_t inputs_schema, sources_schema;
  bool inputs_loaded, sources_loaded;
  /* Values the boards so far hold, with the board that first did. */
  seen_t profiles, selectors, detected;
} loader_t;

static hal_status_t loaded_features(loader_t *l) {
  if (l->features_loaded)
    return HAL_OK;
  jb_string_t config = {0};
  hal_status_t status = jh_fail_storage(l->failure, text_init(&config));
  if (status == HAL_OK)
    status = jh_absolute_path("config", l->checkout_root, &config, l->failure);
  if (status == HAL_OK)
    status = jh_feature_registry_load(&l->features, jb_string_data(&config),
                                      l->limit, l->failure);
  l->features_loaded = status == HAL_OK;
  jb_string_destroy(&config);
  return status;
}

static const jh_feature_t *find_feature(const jh_feature_registry_t *registry,
                                        jb_json_span_t name) {
  size_t low = 0, high = jb_vector_size(&registry->features);
  while (low < high) {
    const size_t middle = low + (high - low) / 2u;
    const jh_feature_t *feature =
        jb_vector_const_at(&registry->features, middle);
    const int order = jh_span_compare(jh_string_span(&feature->name), name);
    if (order == 0)
      return feature;
    if (order < 0)
      low = middle + 1u;
    else
      high = middle;
  }
  return NULL;
}

/* ---- Feature requests ---- */

static bool feature_name_char(char c) {
  return upper(c) || (c >= '0' && c <= '9') || c == '_';
}

/* The repr of text appended after prefix, then suffix, as one failure. */
static hal_status_t fail_quoted(jh_failure_t *failure, const char *prefix,
                                jb_json_span_t text, const char *suffix) {
  jb_string_t repr = {0};
  hal_status_t status = text_init(&repr);
  if (status == HAL_OK)
    status = jh_python_repr_append(&repr, text);
  status =
      status == HAL_OK
          ? fail_plain(failure, JH_PARTS(jh_span(prefix), jh_string_span(&repr),
                                         jh_span(suffix)))
          : jh_fail_storage(failure, status);
  jb_string_destroy(&repr);
  return status;
}

/* normalize_features: the sorted unique SYMBOL=1 entries of the strings. */
static hal_status_t normalize_features(const jb_vector_t *features,
                                       jb_vector_t *out,
                                       jh_failure_t *failure) {
  hal_status_t status = HAL_OK;
  jb_string_t entry = {0};
  status = jh_fail_storage(failure, text_init(&entry));
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(features); ++i) {
    const jb_json_span_t feature = jh_string_span(jh_list_at(features, i));
    jb_json_span_t value = feature;
    (void)jh_python_strip(feature, &value);
    size_t at = 0;
    if (value.length >= 2 && value.data[0] == '-' && value.data[1] == 'D')
      at = 2;
    const size_t begin = at;
    bool matched = false;
    static const char *const prefixes[] = {"HAL_ENABLE_", "HAL_DISABLE_"};
    for (size_t p = 0; !matched && p < COUNTOF(prefixes); ++p) {
      const size_t length = strlen(prefixes[p]);
      matched = value.length - at > length &&
                memcmp(value.data + at, prefixes[p], length) == 0 &&
                feature_name_char(value.data[at + length]);
      if (matched)
        at += length;
    }
    while (matched && at < value.length && feature_name_char(value.data[at]))
      ++at;
    const jb_json_span_t symbol = {value.data + begin, at - begin};
    bool explicit_value = false, explicit_one = false;
    if (matched && at < value.length) {
      matched = value.data[at] == '=' && memchr(value.data + at + 1, '\n',
                                                value.length - at - 1) == NULL;
      explicit_value = true;
      explicit_one = value.length - at - 1 == 1 && value.data[at + 1] == '1';
    }
    if (!matched)
      status = fail_quoted(failure, "feature ", feature,
                           ": expected a HAL_ENABLE_* or HAL_DISABLE_* symbol, "
                           "optionally followed by =1");
    else if (explicit_value && !explicit_one)
      status = fail_quoted(failure, "[JH-CFG-VALUE] feature ", feature,
                           ": HAL feature symbols accept only a bare macro or "
                           "an explicit value of 1");
    if (status == HAL_OK)
      status = jh_fail_storage(
          failure, jh_join(&entry, JH_PARTS(symbol, jh_span("=1"))));
    if (status == HAL_OK && !jh_list_contains(out, jh_string_span(&entry)))
      status =
          jh_fail_storage(failure, jh_list_push(out, jh_string_span(&entry)));
  }
  jh_list_sort(out);
  jb_string_destroy(&entry);
  return status;
}

/* SYMBOL of a SYMBOL=1 entry. */
static jb_json_span_t symbol_of(const jb_string_t *entry) {
  jb_json_span_t text = jh_string_span(entry);
  text.length -= 2;
  return text;
}

/* The strings of an array that holds only strings. */
static hal_status_t strings_of(jb_json_tree_view_t array, jb_vector_t *out) {
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(array); ++i) {
    jb_json_span_t text = {NULL, 0};
    (void)jh_json_string(jh_json_item(array, i), &text);
    status = jh_list_push(out, text);
  }
  return status;
}

static bool only_strings(jb_json_tree_view_t array, const char *prefix) {
  for (size_t i = 0; i < jh_json_count(array); ++i) {
    jb_json_span_t text = {NULL, 0};
    if (!jh_json_string(jh_json_item(array, i), &text))
      return false;
    if (prefix != NULL && (text.length < strlen(prefix) ||
                           memcmp(text.data, prefix, strlen(prefix)) != 0))
      return false;
  }
  return true;
}

/* symbol.replace("HAL_ENABLE_", "HAL_DISABLE_", 1). */
static hal_status_t disabled_twin(jb_json_span_t symbol, jb_string_t *out) {
  static const char needle[] = "HAL_ENABLE_";
  const size_t length = COUNTOF(needle) - 1u;
  for (size_t at = 0; at + length <= symbol.length; ++at) {
    if (memcmp(symbol.data + at, needle, length) != 0)
      continue;
    const jb_json_span_t before = {symbol.data, at};
    const jb_json_span_t after = {symbol.data + at + length,
                                  symbol.length - at - length};
    return jh_join(out, JH_PARTS(before, jh_span("HAL_DISABLE_"), after));
  }
  return jh_join(out, JH_PARTS(symbol));
}

/* resolve_features: the resolved symbols of the normalized requests and the
 * target's required features, as the board configuration resolves them. */
static hal_status_t resolve_features(loader_t *l, jb_json_tree_view_t target,
                                     const jb_vector_t *normalized,
                                     jb_vector_t *resolved) {
  jh_failure_t *failure = l->failure;
  jb_vector_t raw = {0}, required = {0}, sources = {0}, requests = {0};
  jb_string_t text = {0};
  jh_feature_resolution_t resolution;
  jh_feature_resolution_init(&resolution);
  hal_status_t status = jh_fail_storage(failure, jh_list_init(&raw));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, jh_list_init(&required));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, jh_list_init(&sources));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, text_init(&text));
  if (status == HAL_OK)
    status = jh_fail_storage(
        failure, jb_vector_init(&requests, sizeof(jh_feature_request_t),
                                jb_heap_allocator()));
  jb_json_tree_view_t list = {0};
  if (status == HAL_OK && member(target, "requiredFeatures", &list))
    status = jh_fail_storage(failure, strings_of(list, &raw));
  if (status == HAL_OK)
    status = normalize_features(&raw, &required, failure);
  jb_json_span_t id = {NULL, 0};
  (void)jh_json_string(field(target, "id"), &id);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&required); ++i) {
    const jb_json_span_t symbol = symbol_of(jh_list_at(&required, i));
    status = jh_fail_storage(failure, disabled_twin(symbol, &text));
    bool twin = false;
    for (size_t r = 0; status == HAL_OK && r < jb_vector_size(normalized); ++r)
      twin = twin || jh_span_compare(symbol_of(jh_list_at(normalized, r)),
                                     jh_string_span(&text)) == 0;
    if (status == HAL_OK && twin)
      status =
          fail_plain(failure, JH_PARTS(jh_span("[JH-CFG-TARGET-REQUIRED] "), id,
                                       jh_span(" requires "), symbol,
                                       jh_span("; "), jh_string_span(&text),
                                       jh_span(" cannot be requested")));
  }
  char index[32];
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(normalized); ++i) {
    (void)snprintf(index, sizeof index, "[%zu]", i);
    status = jh_fail_storage(
        failure, jh_list_push_parts(&sources,
                                    JH_PARTS(jh_span("command-line:--requested-"
                                                     "feature"),
                                             jh_span(index))));
  }
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&required); ++i) {
    const jb_json_span_t symbol = symbol_of(jh_list_at(&required, i));
    bool requested = false;
    for (size_t r = 0; r < jb_vector_size(normalized); ++r)
      requested =
          requested ||
          jh_span_compare(symbol_of(jh_list_at(normalized, r)), symbol) == 0;
    (void)snprintf(index, sizeof index, "[%zu]", i);
    if (!requested)
      status = jh_fail_storage(
          failure,
          jh_list_push_parts(&sources, JH_PARTS(jh_span("target:"), id,
                                                jh_span(":requiredFeatures"),
                                                jh_span(index))));
  }
  /* The requests borrow the entries and sources, which no longer grow. */
  size_t source = 0;
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(normalized); ++i) {
    const jh_feature_request_t request = {
        symbol_of(jh_list_at(normalized, i)), jh_span("1"),
        jh_string_span(jh_list_at(&sources, source++)), true};
    status = jh_fail_storage(failure, jb_vector_append(&requests, &request, 1));
  }
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&required); ++i) {
    const jb_json_span_t symbol = symbol_of(jh_list_at(&required, i));
    bool requested = false;
    for (size_t r = 0; r < jb_vector_size(normalized); ++r)
      requested =
          requested ||
          jh_span_compare(symbol_of(jh_list_at(normalized, r)), symbol) == 0;
    if (requested)
      continue;
    const jh_feature_request_t request = {
        symbol, jh_span("1"), jh_string_span(jh_list_at(&sources, source++)),
        true};
    status = jh_fail_storage(failure, jb_vector_append(&requests, &request, 1));
  }
  if (status == HAL_OK)
    status = loaded_features(l);
  if (status == HAL_OK)
    status = jh_feature_resolve(
        &l->features, jb_vector_data(&requests), jb_vector_size(&requests),
        jh_span("board configuration"), NULL, &resolution, failure);
  if (status == HAL_OK && jb_vector_size(&resolution.findings) != 0) {
    jb_string_clear(&text);
    for (size_t i = 0;
         status == HAL_OK && i < jb_vector_size(&resolution.findings); ++i) {
      jh_put(&text, &status, i == 0 ? "" : "\n");
      jh_put_span(&text, &status,
                  jh_string_span(jh_list_at(&resolution.findings, i)));
    }
    status = status == HAL_OK
                 ? fail_plain(failure, JH_PARTS(jh_string_span(&text)))
                 : jh_fail_storage(failure, status);
  }
  for (size_t i = 0; status == HAL_OK && resolved != NULL &&
                     i < jb_vector_size(&resolution.resolved);
       ++i)
    status = jh_fail_storage(
        failure,
        jh_list_push(resolved,
                     jh_string_span(jh_list_at(&resolution.resolved, i))));
  jh_feature_resolution_destroy(&resolution);
  jb_vector_destroy(&requests);
  jb_string_destroy(&text);
  jh_list_destroy(&sources);
  jh_list_destroy(&required);
  jh_list_destroy(&raw);
  return status;
}

/* ---- Clock schemas ---- */

/* The first schema failure of value against the schema file name of
 * config/hardware: valid, or the pointer and message of the failure. */
static hal_status_t schema_first_error(loader_t *l, jh_schema_files_t *files,
                                       bool *loaded, const char *name,
                                       jb_json_tree_view_t value, bool *valid,
                                       jb_string_t *pointer,
                                       jb_string_t *message) {
  jh_failure_t *failure = l->failure;
  jb_string_t path = {0}, text = {0};
  jh_json_document_t document = {0};
  jh_json_location_t where;
  jh_json_location_init(&where);
  hal_status_t status = jh_fail_storage(failure, text_init(&path));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, text_init(&text));
  if (status == HAL_OK && !*loaded) {
    status = jh_fail_storage(
        failure,
        jh_join(&path, JH_PARTS(jh_span("config/hardware/"), jh_span(name))));
    jb_string_t absolute = {0};
    if (status == HAL_OK)
      status = jh_fail_storage(failure, text_init(&absolute));
    if (status == HAL_OK)
      status = jh_absolute_path(jb_string_data(&path), l->checkout_root,
                                &absolute, failure);
    if (status == HAL_OK)
      status =
          jh_schema_files_root(files, jb_string_data(&absolute), NULL, failure);
    *loaded = status == HAL_OK;
    jb_string_destroy(&absolute);
  }
  const jb_json_tree_format_t compact = {l->limit, 0, false, false};
  size_t written = 0;
  if (status == HAL_OK)
    status = jh_fail_storage(
        failure,
        jb_json_tree_format(value, &compact, jh_string_sink, &text, &written));
  if (status == HAL_OK)
    status = jh_json_parse_document(&where, name, jh_string_span(&text),
                                    l->limit, &document, failure);
  hal_status_t checked = HAL_OK;
  if (status == HAL_OK)
    checked = jh_schema_files_validate(files, &document, failure);
  if (status == HAL_OK && checked != HAL_OK && checked != HAL_ECONFIG)
    status = checked;
  *valid = status == HAL_OK && checked == HAL_OK;
  const jh_schema_report_t *first =
      status == HAL_OK && !*valid ? jh_schema_files_report(files, 0) : NULL;
  if (first != NULL) {
    status = jh_fail_storage(
        failure, jb_string_assign(pointer, jb_string_data(&first->pointer),
                                  jb_string_size(&first->pointer)));
    if (status == HAL_OK)
      status = jh_fail_storage(
          failure, jb_string_assign(message, jb_string_data(&first->message),
                                    jb_string_size(&first->message)));
  }
  jh_json_document_destroy(&document);
  jh_json_location_destroy(&where);
  jb_string_destroy(&text);
  jb_string_destroy(&path);
  return status;
}

/* A schema failure of the value at base: "<base><pointer with . for />". */
static hal_status_t fail_schema(const check_t *c, const char *base,
                                const jb_string_t *pointer,
                                jb_json_tree_view_t value,
                                const jb_string_t *message) {
  jb_string_t at = {0};
  hal_status_t status = storage(c, text_init(&at));
  jh_put(&at, &status, base);
  for (size_t i = 0; status == HAL_OK && i < jb_string_size(pointer); ++i) {
    const char ch =
        jb_string_data(pointer)[i] == '/' ? '.' : jb_string_data(pointer)[i];
    status = jb_string_append(&at, &ch, 1);
  }
  status = status == HAL_OK ? fail_value(c, jh_string_span(&at), &value,
                                         jb_string_data(message))
                            : storage(c, status);
  jb_string_destroy(&at);
  return status;
}

/* validate_clock_inputs. */
static hal_status_t validate_clock_inputs(loader_t *l, const check_t *c,
                                          jb_json_tree_view_t target,
                                          const jb_vector_t *valid_pins) {
  jb_json_tree_view_t inputs = {0};
  if (!member(target, "clockInputs", &inputs))
    return fail_value(c, jh_span("$.clockInputs"), NULL,
                      "the SoC clock inputs, {} when there are none");
  jb_string_t pointer = {0}, message = {0}, at = {0};
  bool valid = false;
  hal_status_t status = storage(c, text_init(&pointer));
  if (status == HAL_OK)
    status = storage(c, text_init(&message));
  if (status == HAL_OK)
    status = storage(c, text_init(&at));
  if (status == HAL_OK)
    status = schema_first_error(l, &l->inputs_schema, &l->inputs_loaded,
                                "clock_inputs.schema.json", inputs, &valid,
                                &pointer, &message);
  if (status == HAL_OK && !valid)
    status = fail_schema(c, "$.clockInputs", &pointer, inputs, &message);
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(inputs); ++i) {
    const jb_json_tree_view_t kinds = jh_json_item(inputs, i);
    for (size_t k = 0; status == HAL_OK && k < jh_json_count(kinds); ++k) {
      const jb_json_tree_view_t pins = jh_json_item(kinds, k);
      bool within = true;
      for (size_t p = 0; within && p < jh_json_count(pins); ++p)
        within = value_in_pins(valid_pins, jh_json_item(pins, p));
      if (within)
        continue;
      status =
          storage(c, jh_join(&at, JH_PARTS(jh_span("$.clockInputs."),
                                           jh_json_key(kinds), jh_span("."),
                                           jh_json_key(pins))));
      if (status == HAL_OK)
        status = fail_value(c, jh_string_span(&at), &pins, "target valid pins");
    }
  }
  jb_string_destroy(&at);
  jb_string_destroy(&message);
  jb_string_destroy(&pointer);
  return status;
}

/* ---- Targets ---- */

static hal_status_t validate_architecture(const check_t *c,
                                          jb_json_tree_view_t target) {
  static const char *const fields[] = {
      "vendor",  "family",     "soc",         "isa",    "cores",
      "mcuName", "mcuSubtype", "cpuArchName", "hasFpu", "backendName"};
  static const char *const texts[] = {"vendor",      "family",     "soc",
                                      "isa",         "mcuName",    "mcuSubtype",
                                      "cpuArchName", "backendName"};
  const jb_json_tree_view_t architecture = field(target, "architecture");
  hal_status_t status = exact(c, jh_span("$.architecture"), architecture,
                              fields, COUNTOF(fields), fields, COUNTOF(fields));
  const jb_json_tree_view_t cores = field(architecture, "cores");
  int64_t number = 0;
  if (status == HAL_OK && !(jh_json_int64(cores, &number) && number >= 1))
    status = fail_value(c, jh_span("$.architecture.cores"), &cores,
                        "a positive integer");
  jb_string_t at = {0};
  if (status == HAL_OK)
    status = storage(c, text_init(&at));
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(texts); ++i) {
    const jb_json_tree_view_t value = field(architecture, texts[i]);
    jb_json_span_t text = {NULL, 0};
    if (jh_json_string(value, &text) && text.length != 0)
      continue;
    status = storage(c, jh_join(&at, JH_PARTS(jh_span("$.architecture."),
                                              jh_span(texts[i]))));
    if (status == HAL_OK)
      status = fail_value(c, jh_string_span(&at), &value, "a non-empty string");
  }
  const jb_json_tree_view_t fpu = field(architecture, "hasFpu");
  if (status == HAL_OK && !is_bool(fpu))
    status = fail_value(c, jh_span("$.architecture.hasFpu"), &fpu, "a boolean");
  jb_string_destroy(&at);
  return status;
}

static hal_status_t validate_target_build(const check_t *c,
                                          jb_json_tree_view_t target,
                                          jb_json_span_t *provider) {
  static const char *const hal_fields[] = {"targetSelector"};
  const jb_json_tree_view_t hal = field(target, "hal");
  hal_status_t status =
      exact(c, jh_span("$.hal"), hal, hal_fields, COUNTOF(hal_fields),
            hal_fields, COUNTOF(hal_fields));
  const jb_json_tree_view_t selector = field(hal, "targetSelector");
  if (status == HAL_OK && !string_matches(selector, macro_pattern))
    status =
        fail_value(c, jh_span("$.hal.targetSelector"), &selector, "a C macro");
  static const char *const required[] = {"provider", "recipe"};
  static const char *const allowed[] = {"provider", "recipe", "platform",
                                        "idfTarget"};
  const jb_json_tree_view_t build = field(target, "build");
  if (status == HAL_OK)
    status = exact(c, jh_span("$.build"), build, required, COUNTOF(required),
                   allowed, COUNTOF(allowed));
  static const char *const providers[] = {"pico-sdk", "jh-stm32-baremetal",
                                          "esp-idf", "host"};
  const jb_json_tree_view_t value = field(build, "provider");
  if (status == HAL_OK && !string_in(value, providers, COUNTOF(providers)))
    status = fail_value(c, jh_span("$.build.provider"), &value,
                        "a supported provider");
  jb_json_tree_view_t platform = {0}, idf = {0};
  if (status == HAL_OK && string_is(value, "pico-sdk") &&
      !member(build, "platform", &platform))
    status =
        fail_value(c, jh_span("$.build.platform"), NULL, "a Pico SDK platform");
  const bool has_idf = !absent(build, "idfTarget", &idf);
  if (status == HAL_OK && string_is(value, "esp-idf") &&
      !(has_idf && string_matches(idf, id_pattern)))
    status = fail_value(c, jh_span("$.build.idfTarget"), has_idf ? &idf : NULL,
                        "an ESP-IDF target ID");
  (void)jh_json_string(value, provider);
  return status;
}

static hal_status_t validate_target_gpio(const check_t *c,
                                         jb_json_tree_view_t target,
                                         jb_vector_t *valid_pins) {
  static const char *const required[] = {"pinIdFormat", "validPins",
                                         "halEncoding"};
  static const char *const allowed[] = {"pinIdFormat", "validPins",
                                        "halEncoding", "traits"};
  const jb_json_tree_view_t gpio = field(target, "gpio");
  hal_status_t status = exact(c, jh_span("$.gpio"), gpio, required,
                              COUNTOF(required), allowed, COUNTOF(allowed));
  if (status == HAL_OK)
    status = expand_pin_set(c, jh_span("$.gpio.validPins"),
                            field(gpio, "validPins"), valid_pins);
  const jb_json_tree_view_t format = field(gpio, "pinIdFormat");
  const bool integer = string_is(format, "integer");
  const bool stm32 = string_is(format, "stm32-port-pin");
  bool fits = true;
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(valid_pins); ++i) {
    const jh_board_pin_t *pin = jb_vector_const_at(valid_pins, i);
    fits =
        fits &&
        (integer ? !pin->text
                 : pin->text && stm32_pin_pattern(jh_string_span(&pin->name)));
  }
  if (status == HAL_OK && !integer && !stm32)
    status = fail_value(c, jh_span("$.gpio.pinIdFormat"), &format,
                        "integer or stm32-port-pin");
  else if (status == HAL_OK && !fits)
    status = fail_pins(c, jh_span("$.gpio.validPins"), valid_pins,
                       integer ? "integer pin IDs" : "STM32 PxN pin IDs");
  static const char *const encoding[] = {"kind"};
  if (status == HAL_OK)
    status = exact(c, jh_span("$.gpio.halEncoding"), field(gpio, "halEncoding"),
                   encoding, COUNTOF(encoding), encoding, COUNTOF(encoding));
  jb_json_tree_view_t traits = {0};
  const bool has_traits = member(gpio, "traits", &traits);
  if (status == HAL_OK && has_traits && !is_object(traits))
    status = fail_value(c, jh_span("$.gpio.traits"), &traits, "an object");
  jb_string_t at = {0};
  if (status == HAL_OK)
    status = storage(c, text_init(&at));
  for (size_t i = 0;
       status == HAL_OK && has_traits && i < jh_json_count(traits); ++i) {
    const jb_json_tree_view_t set = jh_json_item(traits, i);
    const jb_json_span_t id = jh_json_key(set);
    status = storage(c, jh_join(&at, JH_PARTS(jh_span("$.gpio.traits."), id)));
    if (status == HAL_OK && !id_pattern(id))
      status = fail_text_with(c, jh_string_span(&at), id,
                              JH_PARTS(jh_span("a kebab-case ID")));
    jb_vector_t pins = {0}, outside = {0};
    if (status == HAL_OK)
      status = storage(c, pins_init(&pins));
    if (status == HAL_OK)
      status = storage(c, pins_init(&outside));
    if (status == HAL_OK)
      status = expand_pin_set(c, jh_string_span(&at), set, &pins);
    if (status == HAL_OK)
      status = storage(c, pins_outside(&pins, valid_pins, &outside));
    if (status == HAL_OK && jb_vector_size(&outside) != 0)
      status = fail_pins(c, jh_string_span(&at), &outside, "target valid pins");
    pins_destroy(&outside);
    pins_destroy(&pins);
  }
  jb_string_destroy(&at);
  return status;
}

static hal_status_t validate_target_memory(const check_t *c,
                                           jb_json_tree_view_t target) {
  static const char *const required[] = {"regions", "ramUsableBytes"};
  static const char *const allowed[] = {"regions", "ramUsableBytes",
                                        "externalRam"};
  static const char *const region_fields[] = {"kind", "sizeBytes",
                                              "dmaCapable"};
  const jb_json_tree_view_t memory = field(target, "memory");
  hal_status_t status = exact(c, jh_span("$.memory"), memory, required,
                              COUNTOF(required), allowed, COUNTOF(allowed));
  const jb_json_tree_view_t regions = field(memory, "regions");
  if (status == HAL_OK && !is_object(regions))
    status = fail_value(c, jh_span("$.memory.regions"), &regions, "an object");
  jb_string_t at = {0}, place = {0}, text = {0};
  if (status == HAL_OK)
    status = storage(c, text_init(&at));
  if (status == HAL_OK)
    status = storage(c, text_init(&place));
  if (status == HAL_OK)
    status = storage(c, text_init(&text));
  int64_t ram_total = 0;
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(regions); ++i) {
    const jb_json_tree_view_t region = jh_json_item(regions, i);
    status = storage(c, jh_join(&at, JH_PARTS(jh_span("$.memory.regions."),
                                              jh_json_key(region))));
    if (status == HAL_OK)
      status =
          exact(c, jh_string_span(&at), region, region_fields,
                COUNTOF(region_fields), region_fields, COUNTOF(region_fields));
    const jb_json_tree_view_t kind = field(region, "kind");
    if (status == HAL_OK && !string_is(kind, "ram") &&
        !string_is(kind, "flash")) {
      status =
          storage(c, jh_json_path_join(&place, jh_string_span(&at), ".kind"));
      if (status == HAL_OK)
        status = fail_value(c, jh_string_span(&place), &kind, "ram or flash");
    }
    const jb_json_tree_view_t size = field(region, "sizeBytes");
    int64_t bytes = 0;
    if (status == HAL_OK && !(jh_json_int64(size, &bytes) && bytes > 0)) {
      status = storage(
          c, jh_json_path_join(&place, jh_string_span(&at), ".sizeBytes"));
      if (status == HAL_OK)
        status =
            fail_value(c, jh_string_span(&place), &size, "a positive integer");
    }
    const jb_json_tree_view_t dma = field(region, "dmaCapable");
    if (status == HAL_OK && !is_bool(dma)) {
      status = storage(
          c, jh_json_path_join(&place, jh_string_span(&at), ".dmaCapable"));
      if (status == HAL_OK)
        status = fail_value(c, jh_string_span(&place), &dma, "a boolean");
    }
    if (status == HAL_OK && string_is(kind, "ram"))
      ram_total = bytes > INT64_MAX - ram_total ? INT64_MAX : ram_total + bytes;
  }
  const jb_json_tree_view_t usable = field(memory, "ramUsableBytes");
  int64_t usable_bytes = 0;
  if (status == HAL_OK && !(jh_json_int64(usable, &usable_bytes) &&
                            usable_bytes >= 0 && usable_bytes <= ram_total)) {
    jh_put(&text, &status, "an integer in range 0..");
    jh_put_int64(&text, &status, ram_total);
    status = status == HAL_OK
                 ? fail_value(c, jh_span("$.memory.ramUsableBytes"), &usable,
                              jb_string_data(&text))
                 : storage(c, status);
  }
  jb_json_tree_view_t external = {0};
  if (status == HAL_OK && !absent(memory, "externalRam", &external)) {
    static const char *const fields[] = {"interfaces", "maxBytes"};
    status = exact(c, jh_span("$.memory.externalRam"), external, fields,
                   COUNTOF(fields), fields, COUNTOF(fields));
    const jb_json_tree_view_t interfaces = field(external, "interfaces");
    bool unique = is_array(interfaces) && jh_json_count(interfaces) != 0;
    for (size_t i = 0; unique && i < jh_json_count(interfaces); ++i) {
      const jb_json_tree_view_t item = jh_json_item(interfaces, i);
      unique = string_is(item, "quad") || string_is(item, "octal");
      for (size_t j = 0; unique && j < i; ++j) {
        jb_json_span_t a = {NULL, 0}, b = {NULL, 0};
        (void)jh_json_string(item, &a);
        (void)jh_json_string(jh_json_item(interfaces, j), &b);
        unique = jh_span_compare(a, b) != 0;
      }
    }
    if (status == HAL_OK && !unique)
      status = fail_value(c, jh_span("$.memory.externalRam.interfaces"),
                          &interfaces, "unique quad/octal interfaces");
    const jb_json_tree_view_t most = field(external, "maxBytes");
    int64_t bytes = 0;
    if (status == HAL_OK && !(jh_json_int64(most, &bytes) && bytes > 0))
      status = fail_value(c, jh_span("$.memory.externalRam.maxBytes"), &most,
                          "a positive integer");
  }
  jb_string_destroy(&text);
  jb_string_destroy(&place);
  jb_string_destroy(&at);
  return status;
}

/* The symbols of SYMBOL=1 entries. */
static hal_status_t symbols_of(const jb_vector_t *entries, jb_vector_t *out) {
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(entries); ++i)
    status = jh_list_push(out, symbol_of(jh_list_at(entries, i)));
  jh_list_sort(out);
  return status;
}

/* The strings of a that b lacks, in code point order. */
static hal_status_t list_minus(const jb_vector_t *a, const jb_vector_t *b,
                               jb_vector_t *out) {
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(a); ++i) {
    const jb_json_span_t item = jh_string_span(jh_list_at(a, i));
    if (!jh_list_contains(b, item) && !jh_list_contains(out, item))
      status = jh_list_push(out, item);
  }
  jh_list_sort(out);
  return status;
}

static hal_status_t validate_target_features(loader_t *l, const check_t *c,
                                             jb_json_tree_view_t target) {
  jb_vector_t raw = {0}, required = {0}, supported_raw = {0}, supported = {0},
              required_symbols = {0}, supported_symbols = {0}, lacking = {0},
              requestable = {0}, resolved = {0}, missing = {0},
              unreachable = {0};
  jb_vector_t *lists[] = {&raw,       &required,         &supported_raw,
                          &supported, &required_symbols, &supported_symbols,
                          &lacking,   &requestable,      &resolved,
                          &missing,   &unreachable};
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(lists); ++i)
    status = storage(c, jh_list_init(lists[i]));
  jb_json_tree_view_t features = {0};
  const bool has_required = member(target, "requiredFeatures", &features);
  if (status == HAL_OK && has_required &&
      !(is_array(features) && only_strings(features, NULL)))
    status = fail_value(c, jh_span("$.requiredFeatures"), &features,
                        "a feature symbol array");
  if (status == HAL_OK && has_required)
    status = storage(c, strings_of(features, &raw));
  if (status == HAL_OK)
    status = normalize_features(&raw, &required, c->failure);
  if (status == HAL_OK && jb_vector_size(&required) != jb_vector_size(&raw))
    status = fail_value(c, jh_span("$.requiredFeatures"), &features,
                        "unique feature symbols");
  jb_json_tree_view_t listed = {0};
  if (status == HAL_OK && !absent(target, "supportedFeatures", &listed)) {
    if (!(is_array(listed) && only_strings(listed, "HAL_ENABLE_")))
      status = fail_value(c, jh_span("$.supportedFeatures"), &listed,
                          "a HAL_ENABLE_* feature symbol array");
    if (status == HAL_OK)
      status = storage(c, strings_of(listed, &supported_raw));
    if (status == HAL_OK)
      status = normalize_features(&supported_raw, &supported, c->failure);
    if (status == HAL_OK &&
        jb_vector_size(&supported) != jb_vector_size(&supported_raw))
      status = fail_value(c, jh_span("$.supportedFeatures"), &listed,
                          "unique feature symbols");
    if (status == HAL_OK)
      status = storage(c, symbols_of(&required, &required_symbols));
    if (status == HAL_OK)
      status = storage(c, symbols_of(&supported, &supported_symbols));
    if (status == HAL_OK)
      status = storage(
          c, list_minus(&required_symbols, &supported_symbols, &lacking));
    if (status == HAL_OK && jb_vector_size(&lacking) != 0)
      status = fail_value(c, jh_span("$.supportedFeatures"), &listed,
                          "a superset of target-required features");
    if (status == HAL_OK)
      status = loaded_features(l);
    jh_list_destroy(&lacking);
    if (status == HAL_OK)
      status = storage(c, jh_list_init(&lacking));
    for (size_t i = 0;
         status == HAL_OK && i < jb_vector_size(&supported_symbols); ++i) {
      const jb_json_span_t symbol =
          jh_string_span(jh_list_at(&supported_symbols, i));
      if (find_feature(&l->features, symbol) == NULL)
        status = storage(c, jh_list_push(&lacking, symbol));
    }
    if (status == HAL_OK && jb_vector_size(&lacking) != 0)
      status = fail_list(c, jh_span("$.supportedFeatures"), &lacking,
                         "registered HAL feature symbols");
    for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&supported);
         ++i) {
      const jb_string_t *entry = jh_list_at(&supported, i);
      const jh_feature_t *feature =
          find_feature(&l->features, symbol_of(entry));
      if (!feature->derived)
        status = storage(c, jh_list_push(&requestable, jh_string_span(entry)));
    }
    if (status == HAL_OK)
      status = resolve_features(l, target, &requestable, &resolved);
    jh_list_sort(&resolved);
    if (status == HAL_OK)
      status = storage(c, list_minus(&resolved, &supported_symbols, &missing));
    if (status == HAL_OK)
      status =
          storage(c, list_minus(&supported_symbols, &resolved, &unreachable));
    if (status == HAL_OK &&
        (jb_vector_size(&missing) != 0 || jb_vector_size(&unreachable) != 0)) {
      jb_string_t details = {0};
      status = storage(c, text_init(&details));
      jh_put(&details, &status,
             "the exact closure of requestable supported features (");
      if (jb_vector_size(&missing) != 0) {
        jh_put(&details, &status, "missing closure ");
        if (status == HAL_OK)
          status = jh_repr_list(&details, &missing);
      }
      if (jb_vector_size(&unreachable) != 0) {
        jh_put(&details, &status,
               jb_vector_size(&missing) != 0 ? "; unreachable entries "
                                             : "unreachable entries ");
        if (status == HAL_OK)
          status = jh_repr_list(&details, &unreachable);
      }
      jh_put(&details, &status, ")");
      status = status == HAL_OK ? fail_value(c, jh_span("$.supportedFeatures"),
                                             &listed, jb_string_data(&details))
                                : storage(c, status);
      jb_string_destroy(&details);
    }
  }
  jh_list_destroy(&requestable);
  if (status == HAL_OK)
    status = storage(c, jh_list_init(&requestable));
  if (status == HAL_OK)
    status = resolve_features(l, target, &requestable, NULL);
  for (size_t i = 0; i < COUNTOF(lists); ++i)
    jh_list_destroy(lists[i]);
  return status;
}

/* validate_target: the target's valid pins into valid_pins. */
static hal_status_t validate_target(loader_t *l, const check_t *c,
                                    jb_json_tree_view_t target,
                                    jb_vector_t *valid_pins) {
  hal_status_t status = validate_common(c, target, true);
  jb_json_tree_view_t usb = {0};
  if (status == HAL_OK && member(target, "usbDevice", &usb))
    status = validate_usb_ids(c, jh_span("$.usbDevice"), usb);
  if (status == HAL_OK)
    status = validate_architecture(c, target);
  jb_json_span_t provider = {NULL, 0};
  if (status == HAL_OK)
    status = validate_target_build(c, target, &provider);
  if (status == HAL_OK)
    status = validate_target_gpio(c, target, valid_pins);
  if (status == HAL_OK)
    status = validate_clock_inputs(l, c, target, valid_pins);
  if (status == HAL_OK)
    status = validate_target_memory(c, target);
  const jb_json_tree_view_t board = field(target, "defaultBoard");
  jb_json_span_t text = {NULL, 0};
  if (status == HAL_OK && !jh_json_string(board, &text))
    status = fail_value(c, jh_span("$.defaultBoard"), &board, "a board ID");
  jb_json_tree_view_t fallback = {0};
  if (status == HAL_OK && member(target, "sourceFallbackBoard", &fallback) &&
      !jh_json_string(fallback, &text))
    status = fail_value(c, jh_span("$.sourceFallbackBoard"), &fallback,
                        "a board ID");
  jb_vector_t ids = {0};
  if (status == HAL_OK)
    status = storage(c, jh_list_init(&ids));
  if (status == HAL_OK)
    status = validate_components(c, jh_span("$.components"),
                                 field(target, "components"), provider,
                                 &l->registry->components, &ids);
  jh_list_destroy(&ids);
  if (status == HAL_OK)
    status = validate_target_features(l, c, target);
  return status;
}

/* ---- Clock sources ---- */

/* repr() of the ids of a JSON array as str() of a list prints them. */
static hal_status_t ids_repr(jb_string_t *out, jb_json_tree_view_t array,
                             bool endpoint_ids) {
  hal_status_t status = jh_append(out, "[");
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(array); ++i) {
    jb_json_tree_view_t item = jh_json_item(array, i);
    if (endpoint_ids)
      item = field(item, "id");
    if (i != 0)
      status = jh_append(out, ", ");
    if (status == HAL_OK)
      status = jh_python_repr_json_append(out, item);
  }
  return status == HAL_OK ? jh_append(out, "]") : status;
}

/* The ids of the pins of the source and the required pins hold the same
 * values the same number of times as a set comparison needs. */
static bool same_pins(jb_json_tree_view_t required, jb_json_tree_view_t pins) {
  if (jh_json_count(required) != jh_json_count(pins))
    return false;
  for (int pass = 0; pass < 2; ++pass) {
    const jb_json_tree_view_t from = pass == 0 ? required : pins;
    const jb_json_tree_view_t to = pass == 0 ? pins : required;
    for (size_t i = 0; i < jh_json_count(from); ++i) {
      pin_key_t wanted;
      jb_json_tree_view_t value = jh_json_item(from, i);
      if (pass == 1)
        value = field(value, "id");
      if (!pin_key_of(value, &wanted))
        return false;
      bool found = false;
      for (size_t j = 0; !found && j < jh_json_count(to); ++j) {
        pin_key_t key;
        jb_json_tree_view_t other = jh_json_item(to, j);
        if (pass == 0)
          other = field(other, "id");
        found = pin_key_of(other, &key) && key_compare(key, wanted) == 0;
      }
      if (!found)
        return false;
    }
  }
  return true;
}

/* source_problems: the first reason the source cannot drive the target's
 * clock input key; an empty problem when it can. */
hal_status_t jh_board_clock_source_problem(jb_json_tree_view_t inputs,
                                           jb_json_span_t key,
                                           jb_json_tree_view_t source,
                                           jb_string_t *problem) {
  jb_json_tree_view_t kinds = {0}, required = {0};
  jb_json_span_t kind = {NULL, 0};
  (void)jh_json_string(field(source, "kind"), &kind);
  jb_string_clear(problem);
  hal_status_t status = HAL_OK;
  const bool has_kinds = jb_json_tree_find(inputs, key, &kinds) == HAL_OK;
  if (!has_kinds || jb_json_tree_find(kinds, kind, &required) != HAL_OK ||
      jh_json_type(required) == JB_JSON_NULL) {
    jh_put(problem, &status, "the target has no ");
    jh_put_span(problem, &status, key);
    jh_put(problem, &status, " input for the ");
    jh_put_span(problem, &status, kind);
    return status;
  }
  const jb_json_tree_view_t pins = field(source, "pins");
  if (same_pins(required, pins))
    return HAL_OK;
  jh_put(problem, &status, "the ");
  jh_put_span(problem, &status, kind);
  jh_put(problem, &status, " on ");
  jh_put_span(problem, &status, key);
  jh_put(problem, &status, " occupies ");
  if (status == HAL_OK)
    status = ids_repr(problem, required, false);
  jh_put(problem, &status, ", the board lists ");
  if (status == HAL_OK)
    status = ids_repr(problem, pins, true);
  return status;
}

/* validate_clock_sources. */
static hal_status_t validate_clock_sources(loader_t *l, const check_t *c,
                                           jb_json_tree_view_t board,
                                           const pins_context_t *pins) {
  jb_json_tree_view_t sources = {0};
  if (absent(board, "clockSources", &sources))
    return HAL_OK;
  jb_string_t pointer = {0}, message = {0}, at = {0}, problem = {0},
              owner = {0};
  bool valid = false;
  hal_status_t status = storage(c, text_init(&pointer));
  if (status == HAL_OK)
    status = storage(c, text_init(&message));
  if (status == HAL_OK)
    status = storage(c, text_init(&at));
  if (status == HAL_OK)
    status = storage(c, text_init(&problem));
  if (status == HAL_OK)
    status = storage(c, text_init(&owner));
  if (status == HAL_OK)
    status = schema_first_error(l, &l->sources_schema, &l->sources_loaded,
                                "clock_sources.schema.json", sources, &valid,
                                &pointer, &message);
  if (status == HAL_OK && !valid)
    status = fail_schema(c, "$.clockSources", &pointer, sources, &message);
  const jb_json_tree_view_t compatible = field(board, "compatibleTargets");
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(sources); ++i) {
    const jb_json_tree_view_t source = jh_json_item(sources, i);
    const jb_json_span_t key = jh_json_key(source);
    for (size_t t = 0; status == HAL_OK && t < jh_json_count(compatible); ++t) {
      jb_json_span_t target_id = {NULL, 0};
      (void)jh_json_string(jh_json_item(compatible, t), &target_id);
      const jh_board_descriptor_t *target =
          jh_board_registry_find(&l->registry->targets, target_id);
      status = storage(
          c, jh_board_clock_source_problem(field(target->root, "clockInputs"),
                                           key, source, &problem));
      if (status != HAL_OK || jb_string_size(&problem) == 0)
        continue;
      status =
          storage(c, jh_join(&at, JH_PARTS(jh_span("$.clockSources."), key)));
      if (status == HAL_OK)
        status =
            fail_value_with(c, jh_string_span(&at), &source,
                            JH_PARTS(jh_span("a source on a "), target_id,
                                     jh_span(" clock input ("),
                                     jh_string_span(&problem), jh_span(")")));
    }
    const jb_json_tree_view_t source_pins = field(source, "pins");
    if (status == HAL_OK)
      status = storage(c, jh_join(&owner, JH_PARTS(jh_span("clock."), key)));
    for (size_t p = 0; status == HAL_OK && p < jh_json_count(source_pins);
         ++p) {
      const jb_json_tree_view_t endpoint = jh_json_item(source_pins, p);
      char index[32];
      (void)snprintf(index, sizeof index, "[%zu]", p);
      status =
          storage(c, jh_join(&at, JH_PARTS(jh_span("$.clockSources."), key,
                                           jh_span(".pins"), jh_span(index))));
      if (status == HAL_OK)
        status = validate_endpoint(c, jh_string_span(&at), endpoint,
                                   pins->valid_pins, pins->components);
      if (status == HAL_OK)
        status =
            require_hard_owner(c, jh_string_span(&at), field(endpoint, "id"),
                               pins->reserved, jh_string_span(&owner));
    }
  }
  jb_string_destroy(&owner);
  jb_string_destroy(&problem);
  jb_string_destroy(&at);
  jb_string_destroy(&message);
  jb_string_destroy(&pointer);
  return status;
}

/* ---- Boards ---- */

/* An array of strings among names; strings that repeat make unique false. */
static bool string_array(jb_json_tree_view_t array,
                         bool (*pattern)(jb_json_span_t), bool *unique) {
  *unique = true;
  if (!is_array(array))
    return false;
  for (size_t i = 0; i < jh_json_count(array); ++i) {
    jb_json_span_t text = {NULL, 0};
    if (!jh_json_string(jh_json_item(array, i), &text) ||
        (pattern != NULL && !pattern(text)))
      return false;
    for (size_t j = 0; j < i; ++j) {
      jb_json_span_t other = {NULL, 0};
      (void)jh_json_string(jh_json_item(array, j), &other);
      *unique = *unique && jh_span_compare(text, other) != 0;
    }
  }
  return true;
}

/* len(set(array)) == len(array), with values equal when their repr is. */
static hal_status_t unique_items(jb_json_tree_view_t array, bool *unique) {
  jb_vector_t seen = {0};
  jb_string_t repr = {0};
  hal_status_t status = jh_list_init(&seen);
  if (status == HAL_OK)
    status = text_init(&repr);
  *unique = true;
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(array); ++i) {
    jb_string_clear(&repr);
    status = jh_python_repr_json_append(&repr, jh_json_item(array, i));
    if (status == HAL_OK && jh_list_contains(&seen, jh_string_span(&repr)))
      *unique = false;
    if (status == HAL_OK)
      status = jh_list_push(&seen, jh_string_span(&repr));
  }
  jb_string_destroy(&repr);
  jh_list_destroy(&seen);
  return status;
}

static hal_status_t validate_board_hal(const check_t *c,
                                       jb_json_tree_view_t board) {
  static const char *const fields[] = {
      "profileId", "selector", "legacySelectors", "autoDetectSelectors"};
  const jb_json_tree_view_t hal = field(board, "hal");
  hal_status_t status = exact(c, jh_span("$.hal"), hal, fields, COUNTOF(fields),
                              fields, COUNTOF(fields));
  const jb_json_tree_view_t profile = field(hal, "profileId");
  int64_t number = 0;
  if (status == HAL_OK && !(jh_json_int64(profile, &number) && number >= 1))
    status = fail_value(c, jh_span("$.hal.profileId"), &profile,
                        "a positive integer");
  const jb_json_tree_view_t selector = field(hal, "selector");
  if (status == HAL_OK && !string_matches(selector, macro_pattern))
    status = fail_value(c, jh_span("$.hal.selector"), &selector, "a C macro");
  static const char *const lists[] = {"legacySelectors", "autoDetectSelectors"};
  static const char *const places[] = {"$.hal.legacySelectors",
                                       "$.hal.autoDetectSelectors"};
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(lists); ++i) {
    const jb_json_tree_view_t list = field(hal, lists[i]);
    bool unique = true;
    if (!string_array(list, macro_pattern, &unique))
      status = fail_value(c, jh_span(places[i]), &list, "C macro array");
    else if (!unique)
      status = fail_value(c, jh_span(places[i]), &list, "unique C macros");
  }
  return status;
}

static hal_status_t validate_board_memory(loader_t *l, const check_t *c,
                                          jb_json_tree_view_t board) {
  static const char *const required[] = {"flash"};
  static const char *const allowed[] = {"flash", "psram"};
  static const char *const flash_fields[] = {"source", "expectedBytes"};
  static const char *const psram_fields[] = {"interface", "sizeBytes"};
  const jb_json_tree_view_t memory = field(board, "memory");
  hal_status_t status = exact(c, jh_span("$.memory"), memory, required,
                              COUNTOF(required), allowed, COUNTOF(allowed));
  const jb_json_tree_view_t flash = field(memory, "flash");
  if (status == HAL_OK)
    status = exact(c, jh_span("$.memory.flash"), flash, flash_fields,
                   COUNTOF(flash_fields), flash_fields, COUNTOF(flash_fields));
  static const char *const sources[] = {"sdk", "target", "none"};
  const jb_json_tree_view_t source = field(flash, "source");
  if (status == HAL_OK && !string_in(source, sources, COUNTOF(sources)))
    status = fail_value(c, jh_span("$.memory.flash.source"), &source,
                        "sdk, target, or none");
  const jb_json_tree_view_t expected = field(flash, "expectedBytes");
  int64_t number = 0;
  if (status == HAL_OK && !(jh_json_int64(expected, &number) && number >= 0))
    status = fail_value(c, jh_span("$.memory.flash.expectedBytes"), &expected,
                        "a non-negative integer");
  jb_json_tree_view_t psram = {0};
  if (status != HAL_OK || absent(memory, "psram", &psram))
    return status;
  status = exact(c, jh_span("$.memory.psram"), psram, psram_fields,
                 COUNTOF(psram_fields), psram_fields, COUNTOF(psram_fields));
  const jb_json_tree_view_t interface = field(psram, "interface");
  if (status == HAL_OK && !string_is(interface, "quad") &&
      !string_is(interface, "octal"))
    status = fail_value(c, jh_span("$.memory.psram.interface"), &interface,
                        "quad or octal");
  const jb_json_tree_view_t size = field(psram, "sizeBytes");
  int64_t bytes = 0;
  if (status == HAL_OK && !(jh_json_int64(size, &bytes) && bytes > 0))
    status = fail_value(c, jh_span("$.memory.psram.sizeBytes"), &size,
                        "a positive integer");
  const jb_json_tree_view_t compatible = field(board, "compatibleTargets");
  for (size_t t = 0; status == HAL_OK && t < jh_json_count(compatible); ++t) {
    jb_json_span_t id = {NULL, 0}, kind = {NULL, 0};
    (void)jh_json_string(jh_json_item(compatible, t), &id);
    (void)jh_json_string(interface, &kind);
    const jh_board_descriptor_t *target =
        jh_board_registry_find(&l->registry->targets, id);
    jb_json_tree_view_t support = {0};
    int64_t most = 0;
    bool fits = !absent(field(target->root, "memory"), "externalRam", &support);
    if (fits) {
      const jb_json_tree_view_t interfaces = field(support, "interfaces");
      bool listed = false;
      for (size_t i = 0; i < jh_json_count(interfaces); ++i)
        listed = listed ||
                 string_is(jh_json_item(interfaces, i),
                           string_is(interface, "quad") ? "quad" : "octal");
      fits = listed && jh_json_int64(field(support, "maxBytes"), &most) &&
             bytes <= most;
    }
    if (!fits)
      status = fail_value_with(
          c, jh_span("$.memory.psram"), &psram,
          JH_PARTS(jh_span("external RAM supported by target "), id));
  }
  return status;
}

static hal_status_t validate_programming(const check_t *c,
                                         jb_json_tree_view_t board,
                                         jb_json_span_t provider) {
  jb_json_tree_view_t programming = {0};
  if (absent(board, "programming", &programming))
    return HAL_OK;
  static const char *const fields[] = {"transport", "usb", "reset", "boot"};
  hal_status_t status = exact(c, jh_span("$.programming"), programming, fields,
                              COUNTOF(fields), fields, COUNTOF(fields));
  if (status == HAL_OK && jh_span_compare(provider, jh_span("esp-idf")) != 0)
    status = fail_value(c, jh_span("$.programming"), &programming,
                        "programming metadata for an ESP-IDF board");
  const jb_json_tree_view_t transport = field(programming, "transport");
  const bool jtag = string_is(transport, "usb-serial-jtag");
  if (status == HAL_OK && !jtag && !string_is(transport, "usb-uart-bridge"))
    status = fail_value(c, jh_span("$.programming.transport"), &transport,
                        "usb-serial-jtag or usb-uart-bridge");
  if (status == HAL_OK)
    status = validate_usb_ids(c, jh_span("$.programming.usb"),
                              field(programming, "usb"));
  static const char *const lines[] = {"reset", "boot"};
  static const char *const places[] = {"$.programming.reset",
                                       "$.programming.boot"};
  static const char *const jtag_lines[] = {"usb-serial-jtag-control-lines",
                                           "usb-serial-jtag-control-lines"};
  static const char *const bridge_lines[] = {"dtr-rts-en", "dtr-rts-io0"};
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(lines); ++i) {
    const char *wanted = jtag ? jtag_lines[i] : bridge_lines[i];
    const jb_json_tree_view_t value = field(programming, lines[i]);
    if (!string_is(value, wanted))
      status = fail_value(c, jh_span(places[i]), &value, wanted);
  }
  return status;
}

/* The pin key of a reservation endpoint holds owner text. */
static hal_status_t validate_board_gpio(const check_t *c,
                                        jb_json_tree_view_t board,
                                        const jb_vector_t *valid_union,
                                        const jb_vector_t *components,
                                        jb_vector_t *reserved) {
  static const char *const required[] = {"exposedPins", "reservations",
                                         "aliases"};
  static const char *const allowed[] = {"exposedPins", "connectors",
                                        "reservations", "aliases"};
  const jb_json_tree_view_t gpio = field(board, "gpio");
  hal_status_t status = exact(c, jh_span("$.gpio"), gpio, required,
                              COUNTOF(required), allowed, COUNTOF(allowed));
  jb_vector_t exposed = {0}, outside = {0};
  jb_string_t at = {0}, place = {0};
  if (status == HAL_OK)
    status = storage(c, pins_init(&exposed));
  if (status == HAL_OK)
    status = storage(c, pins_init(&outside));
  if (status == HAL_OK)
    status = storage(c, text_init(&at));
  if (status == HAL_OK)
    status = storage(c, text_init(&place));
  if (status == HAL_OK)
    status = expand_pin_set(c, jh_span("$.gpio.exposedPins"),
                            field(gpio, "exposedPins"), &exposed);
  if (status == HAL_OK)
    status = storage(c, pins_outside(&exposed, valid_union, &outside));
  if (status == HAL_OK && jb_vector_size(&outside) != 0)
    status = fail_pins(c, jh_span("$.gpio.exposedPins"), &outside,
                       "target valid pins");
  jb_json_tree_view_t connectors = {0};
  const bool has_connectors = member(gpio, "connectors", &connectors);
  if (status == HAL_OK && has_connectors && !is_object(connectors))
    status =
        fail_value(c, jh_span("$.gpio.connectors"), &connectors, "an object");
  for (size_t i = 0;
       status == HAL_OK && has_connectors && i < jh_json_count(connectors);
       ++i) {
    static const char *const fields[] = {"pins"};
    const jb_json_tree_view_t connector = jh_json_item(connectors, i);
    status = storage(c, jh_join(&at, JH_PARTS(jh_span("$.gpio.connectors."),
                                              jh_json_key(connector))));
    if (status == HAL_OK)
      status = exact(c, jh_string_span(&at), connector, fields, COUNTOF(fields),
                     fields, COUNTOF(fields));
    const jb_json_tree_view_t pins = field(connector, "pins");
    if (status == HAL_OK)
      status =
          storage(c, jh_json_path_join(&place, jh_string_span(&at), ".pins"));
    if (status == HAL_OK && !is_array(pins))
      status = fail_value(c, jh_string_span(&place), &pins, "an array");
    for (size_t p = 0; status == HAL_OK && p < jh_json_count(pins); ++p) {
      jb_string_t item = {0};
      status = storage(c, text_init(&item));
      if (status == HAL_OK)
        status =
            storage(c, jh_json_path_index(&item, jh_string_span(&place), p));
      if (status == HAL_OK)
        status =
            validate_endpoint(c, jh_string_span(&item), jh_json_item(pins, p),
                              valid_union, components);
      jb_string_destroy(&item);
    }
  }
  const jb_json_tree_view_t reservations = field(gpio, "reservations");
  if (status == HAL_OK && !is_object(reservations))
    status = fail_value(c, jh_span("$.gpio.reservations"), &reservations,
                        "an object");
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(reservations); ++i) {
    static const char *const fields[] = {"pins", "owner", "strength", "reason"};
    const jb_json_tree_view_t reservation = jh_json_item(reservations, i);
    status = storage(c, jh_join(&at, JH_PARTS(jh_span("$.gpio.reservations."),
                                              jh_json_key(reservation))));
    if (status == HAL_OK)
      status = exact(c, jh_string_span(&at), reservation, fields,
                     COUNTOF(fields), fields, COUNTOF(fields));
    const jb_json_tree_view_t strength = field(reservation, "strength");
    const bool hard = string_is(strength, "hard");
    if (status == HAL_OK && !hard && !string_is(strength, "soft")) {
      status = storage(
          c, jh_json_path_join(&place, jh_string_span(&at), ".strength"));
      if (status == HAL_OK)
        status =
            fail_value(c, jh_string_span(&place), &strength, "hard or soft");
    }
    const jb_json_tree_view_t pins = field(reservation, "pins");
    if (status == HAL_OK)
      status =
          storage(c, jh_json_path_join(&place, jh_string_span(&at), ".pins"));
    if (status == HAL_OK && !(is_array(pins) && jh_json_count(pins) != 0))
      status =
          fail_value(c, jh_string_span(&place), &pins, "a non-empty array");
    for (size_t p = 0; status == HAL_OK && p < jh_json_count(pins); ++p) {
      const jb_json_tree_view_t endpoint = jh_json_item(pins, p);
      jb_string_t item = {0};
      status = storage(c, text_init(&item));
      if (status == HAL_OK)
        status =
            storage(c, jh_json_path_index(&item, jh_string_span(&place), p));
      if (status == HAL_OK)
        status = validate_endpoint(c, jh_string_span(&item), endpoint,
                                   valid_union, components);
      jb_string_destroy(&item);
      if (status == HAL_OK && hard &&
          string_is(field(endpoint, "domain"), "soc-gpio"))
        status = storage(c, reserve(reserved, field(endpoint, "id"),
                                    field(reservation, "owner")));
    }
  }
  const jb_json_tree_view_t aliases = field(gpio, "aliases");
  if (status == HAL_OK && !is_object(aliases))
    status = fail_value(c, jh_span("$.gpio.aliases"), &aliases, "an object");
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(aliases); ++i) {
    static const char *const fields[] = {"endpoint"};
    const jb_json_tree_view_t alias = jh_json_item(aliases, i);
    status = storage(c, jh_join(&at, JH_PARTS(jh_span("$.gpio.aliases."),
                                              jh_json_key(alias))));
    if (status == HAL_OK)
      status = exact(c, jh_string_span(&at), alias, fields, COUNTOF(fields),
                     fields, COUNTOF(fields));
    if (status == HAL_OK)
      status = storage(
          c, jh_json_path_join(&place, jh_string_span(&at), ".endpoint"));
    if (status == HAL_OK)
      status =
          validate_endpoint(c, jh_string_span(&place), field(alias, "endpoint"),
                            valid_union, components);
  }
  jb_string_destroy(&place);
  jb_string_destroy(&at);
  pins_destroy(&outside);
  pins_destroy(&exposed);
  return status;
}

static hal_status_t validate_board_capabilities(const check_t *c,
                                                jb_json_tree_view_t board,
                                                jb_json_tree_view_t registry,
                                                const jb_vector_t *components) {
  const jb_json_tree_view_t capabilities = field(board, "capabilities");
  if (!is_object(capabilities))
    return fail_value(c, jh_span("$.capabilities"), &capabilities, "an object");
  jb_vector_t names = {0}, unknown = {0}, active = {0}, missing = {0};
  jb_string_t at = {0};
  hal_status_t status = storage(c, jh_list_init(&names));
  if (status == HAL_OK)
    status = storage(c, jh_list_init(&unknown));
  if (status == HAL_OK)
    status = storage(c, jh_list_init(&active));
  if (status == HAL_OK)
    status = storage(c, jh_list_init(&missing));
  if (status == HAL_OK)
    status = storage(c, text_init(&at));
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(registry); ++i) {
    const jb_json_tree_view_t config = jh_json_item(registry, i);
    jb_json_tree_view_t state = {0};
    status = storage(c, jh_list_push(&names, jh_json_key(config)));
    if (status == HAL_OK &&
        !(member(config, "status", &state) && string_is(state, "reserved")))
      status = storage(c, jh_list_push(&active, jh_json_key(config)));
  }
  if (status == HAL_OK)
    status = storage(c, keys_against(capabilities, &names, false, &unknown));
  if (status == HAL_OK && jb_vector_size(&unknown) != 0)
    status = fail_list(c, jh_span("$.capabilities"), &unknown,
                       "known capability IDs");
  if (status == HAL_OK)
    status = storage(c, names_missing(&active, capabilities, &missing));
  if (status == HAL_OK && jb_vector_size(&missing) != 0)
    status = fail_list(c, jh_span("$.capabilities"), &missing,
                       "all active capability IDs");
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(capabilities); ++i) {
    static const char *const fields[] = {"present"};
    const jb_json_tree_view_t value = jh_json_item(capabilities, i);
    status = storage(c, jh_join(&at, JH_PARTS(jh_span("$.capabilities."),
                                              jh_json_key(value))));
    if (status == HAL_OK)
      status = exact(c, jh_string_span(&at), value, fields, COUNTOF(fields),
                     fields, COUNTOF(fields));
    const jb_json_tree_view_t present = field(value, "present");
    if (status == HAL_OK && !is_bool(present)) {
      status = storage(
          c, jh_join(&at, JH_PARTS(jh_span("$.capabilities."),
                                   jh_json_key(value), jh_span(".present"))));
      if (status == HAL_OK)
        status = fail_value(c, jh_string_span(&at), &present, "a boolean");
    }
  }
  jb_json_tree_view_t cyw43 = {0};
  if (status == HAL_OK && member(capabilities, "cyw43", &cyw43) &&
      truthy_bool(field(cyw43, "present"))) {
    const bool transport =
        jh_list_contains(components, jh_span("cyw43-pico-pio")) ||
        jh_list_contains(components, jh_span("cyw43-stm32-gspi"));
    if (!transport || !jh_list_contains(components, jh_span("cyw43-lwip")))
      status =
          jh_check_got(c->failure, c->path, jh_span("$.capabilities.cyw43"),
                       jh_span("True"), "CYW43 components");
  }
  jb_string_destroy(&at);
  jh_list_destroy(&missing);
  jh_list_destroy(&active);
  jh_list_destroy(&unknown);
  jh_list_destroy(&names);
  return status;
}

static hal_status_t validate_board_devices(const check_t *c,
                                           jb_json_tree_view_t board,
                                           const pins_context_t *pins) {
  const jb_json_tree_view_t devices = field(board, "devices");
  if (!is_object(devices))
    return fail_value(c, jh_span("$.devices"), &devices, "an object");
  jb_string_t at = {0}, owner = {0};
  jb_vector_t roles = {0}, declared_by = {0};
  hal_status_t status = storage(c, text_init(&at));
  if (status == HAL_OK)
    status = storage(c, text_init(&owner));
  if (status == HAL_OK)
    status = storage(c, jh_list_init(&roles));
  if (status == HAL_OK)
    status = storage(c, jh_list_init(&declared_by));
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(devices); ++i) {
    const jb_json_tree_view_t device = jh_json_item(devices, i);
    const jb_json_span_t id = jh_json_key(device);
    status = storage(c, jh_join(&at, JH_PARTS(jh_span("$.devices."), id)));
    if (status == HAL_OK && !camel_pattern(id))
      status = fail_text_with(c, jh_string_span(&at), id,
                              JH_PARTS(jh_span("a camelCase device ID")));
    if (status == HAL_OK)
      status = storage(c, device_owner(id, &owner));
    if (status == HAL_OK)
      status = validate_device(c, jh_string_span(&at), device, pins,
                               jh_string_span(&owner));
    jb_json_tree_view_t role = {0};
    jb_json_span_t role_text = {NULL, 0};
    if (status != HAL_OK || absent(device, "role", &role))
      continue;
    (void)jh_json_string(role, &role_text);
    for (size_t r = 0; status == HAL_OK && r < jb_vector_size(&roles); ++r)
      if (jh_span_compare(jh_string_span(jh_list_at(&roles, r)), role_text) ==
          0) {
        status = storage(c, jh_join(&at, JH_PARTS(jh_span("$.devices."), id,
                                                  jh_span(".role"))));
        if (status == HAL_OK)
          status = fail_value_with(
              c, jh_string_span(&at), &role,
              JH_PARTS(jh_span("a role not already declared by "),
                       jh_string_span(jh_list_at(&declared_by, r))));
      }
    if (status == HAL_OK)
      status = storage(c, jh_list_push(&roles, role_text));
    if (status == HAL_OK)
      status = storage(c, jh_list_push(&declared_by, id));
  }
  jh_list_destroy(&declared_by);
  jh_list_destroy(&roles);
  jb_string_destroy(&owner);
  jb_string_destroy(&at);
  return status;
}

/* validate_board. */
static hal_status_t validate_board(loader_t *l, const check_t *c,
                                   jb_json_tree_view_t board) {
  hal_status_t status = validate_common(c, board, false);
  const jb_json_tree_view_t compatible = field(board, "compatibleTargets");
  bool unique = true;
  bool strings = status == HAL_OK && is_array(compatible) &&
                 jh_json_count(compatible) != 0;
  if (status == HAL_OK && !strings)
    status = fail_value(c, jh_span("$.compatibleTargets"), &compatible,
                        "a non-empty target ID array");
  if (status == HAL_OK)
    status = storage(c, unique_items(compatible, &unique));
  if (status == HAL_OK && !unique)
    status = fail_value(c, jh_span("$.compatibleTargets"), &compatible,
                        "unique target IDs");
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(compatible); ++i) {
    const jb_json_tree_view_t id = jh_json_item(compatible, i);
    jb_json_span_t text = {NULL, 0};
    if (!(jh_json_string(id, &text) &&
          jh_board_registry_find(&l->registry->targets, text) != NULL))
      status = fail_value(c, jh_span("$.compatibleTargets"), &id,
                          "a known target ID");
  }
  static const char *const build_required[] = {"provider"};
  static const char *const build_allowed[] = {"provider", "board"};
  const jb_json_tree_view_t build = field(board, "build");
  if (status == HAL_OK)
    status =
        exact(c, jh_span("$.build"), build, build_required,
              COUNTOF(build_required), build_allowed, COUNTOF(build_allowed));
  jb_vector_t providers = {0};
  if (status == HAL_OK)
    status = storage(c, jh_list_init(&providers));
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(compatible); ++i) {
    jb_json_span_t id = {NULL, 0}, provider = {NULL, 0};
    (void)jh_json_string(jh_json_item(compatible, i), &id);
    const jh_board_descriptor_t *target =
        jh_board_registry_find(&l->registry->targets, id);
    (void)jh_json_string(field(field(target->root, "build"), "provider"),
                         &provider);
    if (!jh_list_contains(&providers, provider))
      status = storage(c, jh_list_push(&providers, provider));
  }
  jh_list_sort(&providers);
  const jb_json_tree_view_t provider_value = field(build, "provider");
  jb_json_span_t provider = {NULL, 0};
  const bool provider_text = jh_json_string(provider_value, &provider);
  if (status == HAL_OK &&
      !(provider_text && jb_vector_size(&providers) == 1 &&
        jh_span_compare(jh_string_span(jh_list_at(&providers, 0)), provider) ==
            0)) {
    jb_string_t set = {0};
    status = storage(c, text_init(&set));
    jh_put(&set, &status, "target provider {");
    for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&providers);
         ++i) {
      jh_put(&set, &status, i == 0 ? "" : ", ");
      if (status == HAL_OK)
        status = jh_python_repr_append(
            &set, jh_string_span(jh_list_at(&providers, i)));
    }
    jh_put(&set, &status, "}");
    status = status == HAL_OK
                 ? fail_value(c, jh_span("$.build.provider"), &provider_value,
                              jb_string_data(&set))
                 : storage(c, status);
    jb_string_destroy(&set);
  }
  jh_list_destroy(&providers);
  jb_json_tree_view_t sdk_board = {0};
  if (status == HAL_OK && string_is(provider_value, "pico-sdk") &&
      !member(build, "board", &sdk_board))
    status = fail_value(c, jh_span("$.build.board"), NULL, "a Pico SDK board");
  if (status == HAL_OK)
    status = validate_board_hal(c, board);
  if (status == HAL_OK)
    status = validate_board_memory(l, c, board);
  if (status == HAL_OK)
    status = validate_programming(c, board, provider);
  jb_vector_t components = {0}, valid_union = {0}, reserved = {0};
  if (status == HAL_OK)
    status = storage(c, jh_list_init(&components));
  if (status == HAL_OK)
    status = storage(c, pins_init(&valid_union));
  if (status == HAL_OK)
    status = storage(c, jb_vector_init(&reserved, sizeof(reservation_t),
                                       jb_heap_allocator()));
  if (status == HAL_OK)
    status = validate_components(c, jh_span("$.components"),
                                 field(board, "components"), provider,
                                 &l->registry->components, &components);
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(compatible); ++i) {
    jb_json_span_t id = {NULL, 0};
    (void)jh_json_string(jh_json_item(compatible, i), &id);
    const jh_board_descriptor_t *target =
        jh_board_registry_find(&l->registry->targets, id);
    const jb_json_tree_view_t listed = field(target->root, "components");
    for (size_t k = 0; status == HAL_OK && k < jh_json_count(listed); ++k)
      if (!jh_list_contains(&components, jh_json_key(jh_json_item(listed, k))))
        status = storage(
            c, jh_list_push(&components, jh_json_key(jh_json_item(listed, k))));
    if (status == HAL_OK)
      status = storage(c, pins_union(&valid_union, &target->valid_pins));
  }
  if (status == HAL_OK)
    status =
        validate_board_gpio(c, board, &valid_union, &components, &reserved);
  if (status == HAL_OK)
    status = validate_board_capabilities(c, board, l->registry->capabilities,
                                         &components);
  const pins_context_t pins = {&valid_union, &components, &reserved};
  if (status == HAL_OK)
    status = validate_board_devices(c, board, &pins);
  const jb_json_tree_view_t peripherals = field(board, "peripherals");
  if (status == HAL_OK && !is_object(peripherals))
    status = fail_value(c, jh_span("$.peripherals"), &peripherals, "an object");
  if (status == HAL_OK)
    status = validate_can(c, board, &pins);
  if (status == HAL_OK)
    status = validate_clock_sources(l, c, board, &pins);
  reservations_destroy(&reserved);
  pins_destroy(&valid_union);
  jh_list_destroy(&components);
  return status;
}

/* ---- The registry ---- */

static void descriptor_destroy(jh_board_descriptor_t *descriptor) {
  jb_string_destroy(&descriptor->id);
  jb_string_destroy(&descriptor->path);
  if (descriptor->tree != NULL)
    jb_json_tree_destroy(descriptor->tree);
  pins_destroy(&descriptor->valid_pins);
}

static void descriptors_destroy(jb_vector_t *descriptors) {
  for (size_t i = 0; i < jb_vector_size(descriptors); ++i)
    descriptor_destroy(jb_vector_at(descriptors, i));
  jb_vector_destroy(descriptors);
}

void jh_board_registry_init(jh_board_registry_t *registry) {
  static const jh_board_registry_t empty = {0};
  if (registry != NULL)
    *registry = empty;
}

void jh_board_registry_destroy(jh_board_registry_t *registry) {
  if (registry == NULL)
    return;
  jb_string_destroy(&registry->boards_root);
  descriptors_destroy(&registry->targets);
  descriptors_destroy(&registry->boards);
  if (registry->capabilities_tree != NULL)
    jb_json_tree_destroy(registry->capabilities_tree);
  for (size_t i = 0; i < jb_vector_size(&registry->components); ++i)
    component_destroy(jb_vector_at(&registry->components, i));
  jb_vector_destroy(&registry->components);
  jh_board_registry_init(registry);
}

const jh_board_descriptor_t *
jh_board_registry_find(const jb_vector_t *descriptors, jb_json_span_t id) {
  if (descriptors == NULL)
    return NULL;
  for (size_t i = 0; i < jb_vector_size(descriptors); ++i) {
    const jh_board_descriptor_t *item = jb_vector_const_at(descriptors, i);
    if (jh_span_compare(jh_string_span(&item->id), id) == 0)
      return item;
  }
  return NULL;
}

/* sorted(directory.glob("*.json")): the file names in code point order; a
 * missing directory has none. */
static hal_status_t json_names(const char *directory, jb_vector_t *names,
                               jh_failure_t *failure) {
  const hal_status_t listed = jh_json_names(directory, names);
  if (listed == HAL_ENOMEM)
    return jh_fail_storage(failure, listed);
  if (listed != HAL_OK && listed != HAL_ENOENT)
    return fail_plain(failure, JH_PARTS(jh_span(directory),
                                        jh_span(": cannot list descriptors: "),
                                        jh_span(hal_status_to_string(listed))));
  return HAL_OK;
}

/* load_registry: capabilities.json. */
static hal_status_t load_capabilities(loader_t *l) {
  jh_board_registry_t *registry = l->registry;
  jb_string_t path = {0}, at = {0};
  hal_status_t status = jh_fail_storage(l->failure, text_init(&path));
  if (status == HAL_OK)
    status = jh_fail_storage(l->failure, text_init(&at));
  if (status == HAL_OK)
    status = jh_fail_storage(
        l->failure, jh_path_join(jb_string_data(&registry->boards_root),
                                 "capabilities.json", &path));
  jb_json_tree_view_t root = {0};
  if (status == HAL_OK)
    status =
        jh_check_load_object(jb_string_data(&path), l->limit,
                             &registry->capabilities_tree, &root, l->failure);
  const check_t c = {jh_string_span(&path), l->failure};
  static const char *const fields[] = {"schemaVersion", "capabilities"};
  if (status == HAL_OK)
    status = exact(&c, jh_span("$"), root, fields, COUNTOF(fields), fields,
                   COUNTOF(fields));
  const jb_json_tree_view_t version = field(root, "schemaVersion");
  int64_t number = 0;
  if (status == HAL_OK && !(jh_json_int64(version, &number) && number == 1))
    status = fail_value(&c, jh_span("$.schemaVersion"), &version, "1");
  registry->capabilities = field(root, "capabilities");
  const jb_json_tree_view_t capabilities = registry->capabilities;
  if (status == HAL_OK && !is_object(capabilities))
    status =
        fail_value(&c, jh_span("$.capabilities"), &capabilities, "an object");
  bool bits[32] = {false};
  jb_vector_t macros = {0};
  if (status == HAL_OK)
    status = storage(&c, jh_list_init(&macros));
  static const char *const required[] = {"bit", "macro", "stateModel",
                                         "runtimeOwner"};
  static const char *const allowed[] = {"bit", "macro", "stateModel",
                                        "runtimeOwner", "status"};
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(capabilities); ++i) {
    const jb_json_tree_view_t config = jh_json_item(capabilities, i);
    const jb_json_span_t id = jh_json_key(config);
    status =
        storage(&c, jh_join(&at, JH_PARTS(jh_span("$.capabilities."), id)));
    if (status == HAL_OK && !id_pattern(id))
      status = fail_text_with(&c, jh_string_span(&at), id,
                              JH_PARTS(jh_span("a kebab-case ID")));
    if (status == HAL_OK)
      status = exact(&c, jh_string_span(&at), config, required,
                     COUNTOF(required), allowed, COUNTOF(allowed));
    const jb_json_tree_view_t bit = field(config, "bit");
    int64_t index = 0;
    const bool in_range =
        jh_json_int64(bit, &index) && index >= 0 && index <= 31;
    if (status == HAL_OK && (!in_range || bits[index])) {
      status = storage(&c, jh_join(&at, JH_PARTS(jh_span("$.capabilities."), id,
                                                 jh_span(".bit"))));
      if (status == HAL_OK)
        status = fail_value(&c, jh_string_span(&at), &bit,
                            in_range ? "a unique capability bit"
                                     : "an integer from 0 through 31");
    }
    if (status == HAL_OK)
      bits[index] = true;
    const jb_json_tree_view_t macro = field(config, "macro");
    jb_json_span_t text = {NULL, 0};
    if (status == HAL_OK &&
        !(jh_json_string(macro, &text) && macro_pattern(text) &&
          !jh_list_contains(&macros, text))) {
      status = storage(&c, jh_join(&at, JH_PARTS(jh_span("$.capabilities."), id,
                                                 jh_span(".macro"))));
      if (status == HAL_OK)
        status =
            fail_value(&c, jh_string_span(&at), &macro, "a unique C macro");
    }
    if (status == HAL_OK)
      status = storage(&c, jh_list_push(&macros, text));
    const jb_json_tree_view_t model = field(config, "stateModel");
    if (status == HAL_OK && !string_is(model, "static") &&
        !string_is(model, "lifecycle")) {
      status = storage(&c, jh_join(&at, JH_PARTS(jh_span("$.capabilities."), id,
                                                 jh_span(".stateModel"))));
      if (status == HAL_OK)
        status =
            fail_value(&c, jh_string_span(&at), &model, "static or lifecycle");
    }
  }
  jh_list_destroy(&macros);
  jb_string_destroy(&at);
  jb_string_destroy(&path);
  return status;
}

/* The profile ID, selector and auto-detection selectors of one board are
 * not held by an earlier board. */
static hal_status_t check_board_unique(loader_t *l, const check_t *c,
                                       jb_json_tree_view_t root) {
  const jb_json_tree_view_t hal = field(root, "hal");
  const jb_json_tree_view_t profile = field(hal, "profileId");
  const jb_json_tree_view_t selector = field(hal, "selector");
  jb_string_t key = {0};
  hal_status_t status = storage(c, text_init(&key));
  int64_t number = 0;
  (void)jh_json_int64(profile, &number);
  jh_put_int64(&key, &status, number);
  status = storage(c, status);
  const jb_json_tree_view_t values[] = {profile, selector};
  seen_t *seen[] = {&l->profiles, &l->selectors};
  static const char *const places[] = {"$.hal.profileId", "$.hal.selector"};
  for (size_t v = 0; status == HAL_OK && v < COUNTOF(values); ++v) {
    jb_json_span_t text = jh_string_span(&key);
    if (v == 1)
      (void)jh_json_string(selector, &text);
    const jb_string_t *first = seen_path(seen[v], text);
    if (first != NULL)
      status =
          fail_value_with(c, jh_span(places[v]), &values[v],
                          JH_PARTS(jh_span("a unique value; first used by "),
                                   jh_string_span(first)));
    if (status == HAL_OK)
      status = storage(c, seen_add(seen[v], text, c->path));
  }
  const jb_json_tree_view_t automatic = field(hal, "autoDetectSelectors");
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(automatic); ++i) {
    const jb_json_tree_view_t item = jh_json_item(automatic, i);
    jb_json_span_t text = {NULL, 0};
    (void)jh_json_string(item, &text);
    if (seen_path(&l->detected, text) != NULL)
      status = fail_value(c, jh_span("$.hal.autoDetectSelectors"), &item,
                          "a globally unique auto-detection selector");
    if (status == HAL_OK)
      status = storage(c, seen_add(&l->detected, text, c->path));
  }
  jb_string_destroy(&key);
  return status;
}

/* Read and validate every descriptor of boards_root/<kind>. */
static hal_status_t load_descriptors(loader_t *l, bool targets) {
  jh_board_registry_t *registry = l->registry;
  jb_vector_t *list = targets ? &registry->targets : &registry->boards;
  jb_vector_t names = {0};
  jb_string_t directory = {0};
  hal_status_t status = jh_fail_storage(l->failure, jh_list_init(&names));
  if (status == HAL_OK)
    status = jh_fail_storage(l->failure, text_init(&directory));
  if (status == HAL_OK)
    status = jh_fail_storage(
        l->failure, jh_path_join(jb_string_data(&registry->boards_root),
                                 targets ? "targets" : "profiles", &directory));
  if (status == HAL_OK)
    status = json_names(jb_string_data(&directory), &names, l->failure);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&names); ++i) {
    jh_board_descriptor_t item = {0};
    status = jh_fail_storage(l->failure, text_init(&item.id));
    if (status == HAL_OK)
      status = jh_fail_storage(l->failure, text_init(&item.path));
    if (status == HAL_OK)
      status = jh_fail_storage(l->failure, pins_init(&item.valid_pins));
    if (status == HAL_OK)
      status = jh_fail_storage(
          l->failure,
          jh_path_join(jb_string_data(&directory),
                       jb_string_data(jh_list_at(&names, i)), &item.path));
    if (status == HAL_OK)
      status = jh_check_load_object(jb_string_data(&item.path), l->limit,
                                    &item.tree, &item.root, l->failure);
    const check_t c = {jh_string_span(&item.path), l->failure};
    if (status == HAL_OK)
      status = targets ? validate_target(l, &c, item.root, &item.valid_pins)
                       : validate_board(l, &c, item.root);
    jb_json_span_t id = {NULL, 0};
    const jb_json_tree_view_t id_value = field(item.root, "id");
    (void)jh_json_string(id_value, &id);
    if (status == HAL_OK && jh_board_registry_find(list, id) != NULL)
      status = fail_value(&c, jh_span("$.id"), &id_value,
                          targets ? "a globally unique target ID"
                                  : "a globally unique board ID");
    if (status == HAL_OK && !targets)
      status = check_board_unique(l, &c, item.root);
    if (status == HAL_OK)
      status = jh_fail_storage(l->failure, jh_append_span(&item.id, id));
    if (status == HAL_OK)
      status = jh_fail_storage(l->failure, jb_vector_append(list, &item, 1));
    if (status != HAL_OK)
      descriptor_destroy(&item);
  }
  jb_string_destroy(&directory);
  jh_list_destroy(&names);
  return status;
}

/* A selector of a list of the board's hal object, by index. */
static jb_json_tree_view_t hal_list(const jh_board_descriptor_t *board,
                                    const char *name) {
  return field(field(board->root, "hal"), name);
}

/* Legacy selectors distinct from every primary and auto-detection selector
 * and unambiguous per target; auto-detection selectors distinct from the
 * primary ones. */
static hal_status_t check_selectors(loader_t *l) {
  const jb_vector_t *boards = &l->registry->boards;
  const seen_t *selectors = &l->selectors, *detected = &l->detected;
  seen_t legacy = {0};
  jb_string_t key = {0};
  hal_status_t status = jh_fail_storage(l->failure, seen_init(&legacy));
  if (status == HAL_OK)
    status = jh_fail_storage(l->failure, text_init(&key));
  for (size_t b = 0; status == HAL_OK && b < jb_vector_size(boards); ++b) {
    const jh_board_descriptor_t *board = jb_vector_const_at(boards, b);
    const check_t c = {jh_string_span(&board->path), l->failure};
    const jb_json_tree_view_t list = hal_list(board, "legacySelectors");
    for (size_t i = 0; status == HAL_OK && i < jh_json_count(list); ++i) {
      const jb_json_tree_view_t item = jh_json_item(list, i);
      jb_json_span_t text = {NULL, 0};
      (void)jh_json_string(item, &text);
      if (seen_path(selectors, text) != NULL)
        status = fail_value(&c, jh_span("$.hal.legacySelectors"), &item,
                            "a selector distinct from every primary board "
                            "selector");
      else if (seen_path(detected, text) != NULL)
        status = fail_value(&c, jh_span("$.hal.legacySelectors"), &item,
                            "a selector distinct from every provider "
                            "auto-detection selector");
    }
  }
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&detected->keys);
       ++i) {
    const jb_json_span_t text = jh_string_span(jh_list_at(&detected->keys, i));
    if (seen_path(selectors, text) == NULL)
      continue;
    const check_t c = {jh_string_span(jh_list_at(&detected->paths, i)),
                       l->failure};
    jb_string_t got = {0};
    status = storage(&c, text_init(&got));
    if (status == HAL_OK)
      status = storage(&c, jh_python_repr_append(&got, text));
    if (status == HAL_OK)
      status =
          jh_check_got(c.failure, c.path, jh_span("$.hal.autoDetectSelectors"),
                       jh_string_span(&got),
                       "a selector distinct from every primary board "
                       "selector");
    jb_string_destroy(&got);
  }
  for (size_t b = 0; status == HAL_OK && b < jb_vector_size(boards); ++b) {
    const jh_board_descriptor_t *board = jb_vector_const_at(boards, b);
    const check_t c = {jh_string_span(&board->path), l->failure};
    const jb_json_tree_view_t list = hal_list(board, "legacySelectors");
    const jb_json_tree_view_t compatible =
        field(board->root, "compatibleTargets");
    for (size_t i = 0; status == HAL_OK && i < jh_json_count(list); ++i) {
      const jb_json_tree_view_t item = jh_json_item(list, i);
      jb_json_span_t text = {NULL, 0};
      (void)jh_json_string(item, &text);
      for (size_t t = 0; status == HAL_OK && t < jh_json_count(compatible);
           ++t) {
        jb_json_span_t target = {NULL, 0};
        (void)jh_json_string(jh_json_item(compatible, t), &target);
        status =
            storage(&c, jh_join(&key, JH_PARTS(text, jh_span("\x1f"), target)));
        const jb_string_t *first =
            status == HAL_OK ? seen_path(&legacy, jh_string_span(&key)) : NULL;
        if (first != NULL) {
          jb_string_t repr = {0};
          status = storage(&c, text_init(&repr));
          if (status == HAL_OK)
            status = storage(&c, jh_python_repr_append(&repr, target));
          if (status == HAL_OK)
            status = fail_value_with(
                &c, jh_span("$.hal.legacySelectors"), &item,
                JH_PARTS(jh_span("an unambiguous selector for target "),
                         jh_string_span(&repr), jh_span("; first used by "),
                         jh_string_span(first)));
          jb_string_destroy(&repr);
        }
        if (status == HAL_OK)
          status = storage(&c, seen_add(&legacy, jh_string_span(&key), c.path));
      }
    }
  }
  jb_string_destroy(&key);
  seen_destroy(&legacy);
  return status;
}

/* defaultBoard and sourceFallbackBoard name a compatible board. */
static hal_status_t check_default_boards(loader_t *l) {
  const jh_board_registry_t *registry = l->registry;
  hal_status_t status = HAL_OK;
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&registry->targets);
       ++i) {
    const jh_board_descriptor_t *target =
        jb_vector_const_at(&registry->targets, i);
    const check_t c = {jh_string_span(&target->path), l->failure};
    static const char *const fields[] = {"defaultBoard", "sourceFallbackBoard"};
    static const char *const places[] = {"$.defaultBoard",
                                         "$.sourceFallbackBoard"};
    for (size_t f = 0; status == HAL_OK && f < COUNTOF(fields); ++f) {
      jb_json_tree_view_t value = {0};
      if (f == 1 && absent(target->root, fields[f], &value))
        continue;
      value = field(target->root, fields[f]);
      jb_json_span_t id = {NULL, 0};
      (void)jh_json_string(value, &id);
      const jh_board_descriptor_t *board =
          jh_board_registry_find(&registry->boards, id);
      bool compatible = false;
      const jb_json_tree_view_t targets =
          board != NULL ? field(board->root, "compatibleTargets")
                        : (jb_json_tree_view_t){0};
      for (size_t t = 0; board != NULL && t < jh_json_count(targets); ++t) {
        jb_json_span_t text = {NULL, 0};
        compatible = compatible ||
                     (jh_json_string(jh_json_item(targets, t), &text) &&
                      jh_span_compare(text, jh_string_span(&target->id)) == 0);
      }
      if (!compatible)
        status =
            fail_value(&c, jh_span(places[f]), &value, "a compatible board ID");
    }
  }
  return status;
}

hal_status_t jh_board_registry_load(jh_board_registry_t *registry,
                                    const char *checkout_root,
                                    const char *boards_root, size_t limit,
                                    jh_failure_t *failure) {
  if (registry == NULL || checkout_root == NULL || boards_root == NULL)
    return jh_fail_arguments(failure);
  loader_t l = {0};
  l.registry = registry;
  l.checkout_root = checkout_root;
  l.limit = limit;
  l.failure = failure;
  jh_feature_registry_init(&l.features);
  jh_json_location_init(&l.schema_where);
  jh_schema_files_init(&l.inputs_schema, &l.schema_where, limit);
  jh_schema_files_init(&l.sources_schema, &l.schema_where, limit);
  hal_status_t status = jh_fail_storage(failure, seen_init(&l.profiles));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, seen_init(&l.selectors));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, seen_init(&l.detected));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, text_init(&registry->boards_root));
  if (status == HAL_OK)
    status =
        jh_fail_storage(failure, jb_vector_init(&registry->targets,
                                                sizeof(jh_board_descriptor_t),
                                                jb_heap_allocator()));
  if (status == HAL_OK)
    status =
        jh_fail_storage(failure, jb_vector_init(&registry->boards,
                                                sizeof(jh_board_descriptor_t),
                                                jb_heap_allocator()));
  if (status == HAL_OK)
    status =
        jh_fail_storage(failure, jb_vector_init(&registry->components,
                                                sizeof(jh_board_component_t),
                                                jb_heap_allocator()));
  if (status == HAL_OK)
    status = load_components(registry, checkout_root, limit, failure);
  if (status == HAL_OK)
    status = check_roles(failure);
  if (status == HAL_OK)
    status = jh_fail_storage(
        failure, jh_resolved_path(boards_root, &registry->boards_root));
  if (status == HAL_OK)
    status = load_capabilities(&l);
  if (status == HAL_OK)
    status = load_descriptors(&l, true);
  if (status == HAL_OK)
    status = load_descriptors(&l, false);
  if (status == HAL_OK)
    status = check_selectors(&l);
  if (status == HAL_OK)
    status = check_default_boards(&l);
  seen_destroy(&l.detected);
  seen_destroy(&l.selectors);
  seen_destroy(&l.profiles);
  jh_schema_files_destroy(&l.sources_schema);
  jh_schema_files_destroy(&l.inputs_schema);
  jh_json_location_destroy(&l.schema_where);
  jh_feature_registry_destroy(&l.features);
  return status;
}

/* ---- The fixed tables as JSON ---- */

static hal_status_t write_strings(jb_json_writer_t *writer,
                                  const char *const *names, size_t count) {
  hal_status_t status = jb_json_writer_array_begin(writer);
  for (size_t i = 0; status == HAL_OK && i < count; ++i)
    status = jb_json_writer_string(writer, jh_span(names[i]));
  return status == HAL_OK ? jb_json_writer_array_end(writer) : status;
}

static hal_status_t write_key(jb_json_writer_t *writer, const char *key) {
  return jb_json_writer_key(writer, jh_span(key));
}

static hal_status_t write_role(jb_json_writer_t *writer,
                               const role_spec_t *spec) {
  hal_status_t status = write_key(writer, spec->role);
  if (status == HAL_OK)
    status = jb_json_writer_object_begin(writer);
  if (status == HAL_OK)
    status = write_key(writer, "busKinds");
  if (status == HAL_OK)
    status = write_strings(writer, spec->bus_kinds, spec->bus_kind_count);
  if (status == HAL_OK)
    status = write_key(writer, "macroPrefix");
  if (status == HAL_OK)
    status = jb_json_writer_string(writer, jh_span(spec->macro_prefix));
  if (status == HAL_OK)
    status = write_key(writer, "signals");
  if (status == HAL_OK)
    status = jb_json_writer_object_begin(writer);
  for (size_t i = 0; status == HAL_OK && i < spec->signal_count; ++i) {
    status = write_key(writer, spec->signals[i].name);
    if (status == HAL_OK)
      status = jb_json_writer_string(writer, jh_span(spec->signals[i].mode));
  }
  if (status == HAL_OK)
    status = jb_json_writer_object_end(writer);
  if (status == HAL_OK)
    status = write_key(writer, "attributes");
  if (status == HAL_OK)
    status = jb_json_writer_object_begin(writer);
  for (size_t i = 0; status == HAL_OK && i < spec->attribute_count; ++i) {
    const role_attribute_t *attribute = &spec->attributes[i];
    status = write_key(writer, attribute->name);
    if (status == HAL_OK)
      status = jb_json_writer_object_begin(writer);
    if (status == HAL_OK)
      status = write_key(writer, "type");
    if (status == HAL_OK)
      status = jb_json_writer_string(writer, jh_span(attribute->type));
    if (status == HAL_OK && attribute->has_min) {
      status = write_key(writer, "min");
      if (status == HAL_OK)
        status = jb_json_writer_i64(writer, attribute->min);
    }
    if (status == HAL_OK && attribute->value_count != 0) {
      status = write_key(writer, "values");
      if (status == HAL_OK)
        status =
            write_strings(writer, attribute->values, attribute->value_count);
    }
    if (status == HAL_OK && attribute->conditional) {
      status = write_key(writer, "conditional");
      if (status == HAL_OK)
        status = jb_json_writer_bool(writer, true);
    }
    if (status == HAL_OK)
      status = jb_json_writer_object_end(writer);
  }
  if (status == HAL_OK)
    status = jb_json_writer_object_end(writer);
  if (status == HAL_OK)
    status = write_key(writer, "ordered");
  if (status == HAL_OK)
    status = jb_json_writer_array_begin(writer);
  for (size_t i = 0; status == HAL_OK && i < spec->ordered_count; ++i) {
    const char *const pair[] = {spec->ordered[i].lower, spec->ordered[i].upper};
    status = write_strings(writer, pair, COUNTOF(pair));
  }
  if (status == HAL_OK)
    status = jb_json_writer_array_end(writer);
  if (status == HAL_OK)
    status = write_key(writer, "requires");
  if (status == HAL_OK)
    status = jb_json_writer_array_begin(writer);
  for (size_t i = 0; status == HAL_OK && i < spec->rule_count; ++i) {
    const role_rule_t *rule = &spec->rules[i];
    status = jb_json_writer_object_begin(writer);
    if (status == HAL_OK)
      status = write_key(writer, "when");
    if (status == HAL_OK)
      status = jb_json_writer_array_begin(writer);
    if (status == HAL_OK)
      status = jb_json_writer_string(writer, jh_span(rule->gate));
    if (status == HAL_OK)
      status = write_strings(writer, rule->allowed, rule->allowed_count);
    if (status == HAL_OK)
      status = jb_json_writer_array_end(writer);
    if (status == HAL_OK && rule->signal_count != 0) {
      status = write_key(writer, "signals");
      if (status == HAL_OK)
        status = write_strings(writer, rule->signals, rule->signal_count);
    }
    if (status == HAL_OK && rule->attribute_count != 0) {
      status = write_key(writer, "attributes");
      if (status == HAL_OK)
        status = write_strings(writer, rule->attributes, rule->attribute_count);
    }
    if (status == HAL_OK)
      status = jb_json_writer_object_end(writer);
  }
  if (status == HAL_OK)
    status = jb_json_writer_array_end(writer);
  return status == HAL_OK ? jb_json_writer_object_end(writer) : status;
}

hal_status_t jh_board_write_tables(jb_json_writer_t *writer) {
  if (writer == NULL)
    return HAL_EINVAL;
  hal_status_t status = write_key(writer, "deviceRoles");
  if (status == HAL_OK)
    status = jb_json_writer_object_begin(writer);
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(device_roles); ++i)
    status = write_role(writer, &device_roles[i]);
  if (status == HAL_OK)
    status = jb_json_writer_object_end(writer);
  if (status == HAL_OK)
    status = write_key(writer, "canControllers");
  if (status == HAL_OK)
    status = jb_json_writer_object_begin(writer);
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(can_controllers); ++i) {
    const can_controller_t *controller = &can_controllers[i];
    status = write_key(writer, controller->id);
    if (status == HAL_OK)
      status = jb_json_writer_object_begin(writer);
    if (status == HAL_OK)
      status = write_key(writer, "targets");
    if (status == HAL_OK)
      status =
          write_strings(writer, controller->targets, controller->target_count);
    if (status == HAL_OK)
      status = write_key(writer, "instances");
    if (status == HAL_OK)
      status = jb_json_writer_i64(writer, controller->instances);
    if (status == HAL_OK)
      status = write_key(writer, "backend");
    if (status == HAL_OK)
      status = jb_json_writer_string(writer, jh_span(controller->backend));
    if (status == HAL_OK)
      status = jb_json_writer_object_end(writer);
  }
  if (status == HAL_OK)
    status = jb_json_writer_object_end(writer);
  if (status == HAL_OK)
    status = write_key(writer, "canTransceivers");
  if (status == HAL_OK)
    status = write_strings(writer, can_transceivers, COUNTOF(can_transceivers));
  if (status == HAL_OK)
    status = write_key(writer, "canChannelSignals");
  if (status == HAL_OK)
    status = write_strings(writer, can_channel_signals,
                           COUNTOF(can_channel_signals));
  if (status == HAL_OK)
    status = write_key(writer, "busDeviceKind");
  if (status == HAL_OK)
    status = jb_json_writer_string(writer, jh_span(bus_device_kind));
  if (status == HAL_OK)
    status = write_key(writer, "singleEndpointDeviceKinds");
  if (status == HAL_OK)
    status = jb_json_writer_object_begin(writer);
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(endpoint_kinds); ++i) {
    status = write_key(writer, endpoint_kinds[i].kind);
    if (status == HAL_OK)
      status = write_strings(writer, endpoint_kinds[i].fields,
                             endpoint_kinds[i].field_count);
  }
  return status == HAL_OK ? jb_json_writer_object_end(writer) : status;
}
