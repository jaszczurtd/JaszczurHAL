#include "jh_bluetooth_gamepad_parser.h"

#include <limits.h>
#include <string.h>

enum {
  JH_HID_USAGE_JOYSTICK = 0x04u,
  JH_HID_USAGE_GAME_PAD = 0x05u,
  JH_HID_USAGE_X = 0x30u,
  JH_HID_USAGE_Y = 0x31u,
  JH_HID_USAGE_Z = 0x32u,
  JH_HID_USAGE_RX = 0x33u,
  JH_HID_USAGE_RY = 0x34u,
  JH_HID_USAGE_RZ = 0x35u,
  JH_HID_USAGE_SLIDER = 0x36u,
  JH_HID_USAGE_DIAL = 0x37u,
  JH_HID_USAGE_WHEEL = 0x38u,
  JH_HID_USAGE_HAT_SWITCH = 0x39u,
  JH_HID_COLLECTION_APPLICATION = 0x01u,
  JH_HID_MAIN_FLAG_CONSTANT = (1u << 0),
  JH_HID_MAIN_FLAG_VARIABLE = (1u << 1),
  JH_HID_MAIN_FLAG_NULL_STATE = (1u << 6),
  JH_HID_COLLECTION_DEPTH_MAX = 8u,
  JH_HID_GLOBAL_STACK_MAX = 4u,
  JH_HID_LOCAL_USAGE_CAPACITY = JH_BLUETOOTH_GAMEPAD_FIELD_CAPACITY,
  JH_HID_REPORT_ID_UNDEFINED = UINT16_MAX,
  JH_HID_USAGE_PAGE_DESKTOP = 0x01u,
  JH_HID_USAGE_PAGE_BUTTON = 0x09u,
  JH_HID_ITEM_TYPE_MAIN = 0u,
  JH_HID_ITEM_TYPE_GLOBAL = 1u,
  JH_HID_ITEM_TYPE_LOCAL = 2u,
  JH_HID_ITEM_TYPE_RESERVED = 3u,
  JH_HID_MAIN_INPUT = 8u,
  JH_HID_MAIN_COLLECTION = 10u,
  JH_HID_MAIN_END_COLLECTION = 12u,
  JH_HID_GLOBAL_USAGE_PAGE = 0u,
  JH_HID_GLOBAL_LOGICAL_MINIMUM = 1u,
  JH_HID_GLOBAL_LOGICAL_MAXIMUM = 2u,
  JH_HID_GLOBAL_PHYSICAL_MINIMUM = 3u,
  JH_HID_GLOBAL_PHYSICAL_MAXIMUM = 4u,
  JH_HID_GLOBAL_UNIT_EXPONENT = 5u,
  JH_HID_GLOBAL_UNIT = 6u,
  JH_HID_GLOBAL_REPORT_SIZE = 7u,
  JH_HID_GLOBAL_REPORT_ID = 8u,
  JH_HID_GLOBAL_REPORT_COUNT = 9u,
  JH_HID_GLOBAL_PUSH = 10u,
  JH_HID_GLOBAL_POP = 11u,
  JH_HID_LOCAL_USAGE = 0u,
  JH_HID_LOCAL_USAGE_MINIMUM = 1u,
  JH_HID_LOCAL_USAGE_MAXIMUM = 2u,
};

typedef struct {
  int32_t logical_minimum;
  int32_t logical_maximum;
  uint16_t usage_page;
  uint16_t report_id;
  uint8_t report_size;
  uint8_t report_count;
} jh_hid_globals_t;

typedef struct {
  uint32_t value;
  int32_t signed_value;
  uint8_t data_size;
  uint8_t type;
  uint8_t tag;
} jh_hid_item_t;

typedef struct {
  uint32_t usages[JH_HID_LOCAL_USAGE_CAPACITY];
  uint32_t usage_minimum;
  uint32_t usage_maximum;
  uint8_t usage_count;
  bool have_usage_minimum;
  bool have_usage_maximum;
} jh_hid_locals_t;

typedef struct {
  uint16_t usage_page;
  uint16_t usage;
  uint16_t report_id;
  uint16_t bit_position;
  int32_t logical_minimum;
  int32_t logical_maximum;
  uint8_t bit_size;
  uint8_t main_flags;
} jh_hid_usage_field_t;

static hal_status_t
reject_descriptor(jh_bluetooth_gamepad_parser_t *parser, hal_status_t status,
                  jh_bluetooth_gamepad_reject_reason_t reason) {
  ++parser->diagnostics.descriptors_rejected;
  parser->diagnostics.last_status = status;
  parser->diagnostics.last_reject_reason = reason;
  parser->configured = false;
  return status;
}

static hal_status_t reject_report(jh_bluetooth_gamepad_parser_t *parser,
                                  hal_status_t status,
                                  jh_bluetooth_gamepad_reject_reason_t reason) {
  ++parser->diagnostics.reports_rejected;
  parser->diagnostics.last_status = status;
  parser->diagnostics.last_reject_reason = reason;
  return status;
}

void jh_bluetooth_gamepad_parser_init(jh_bluetooth_gamepad_parser_t *parser) {
  if (parser != NULL) {
    memset(parser, 0, sizeof(*parser));
  }
}

static jh_bluetooth_gamepad_report_layout_t *
find_report_layout(jh_bluetooth_gamepad_parser_t *parser, uint16_t report_id) {
  for (uint8_t index = 0u; index < parser->report_layout_count; ++index) {
    if (parser->report_layouts[index].report_id == report_id) {
      return &parser->report_layouts[index];
    }
  }
  return NULL;
}

static jh_bluetooth_gamepad_report_layout_t *
get_report_layout(jh_bluetooth_gamepad_parser_t *parser, uint16_t report_id) {
  jh_bluetooth_gamepad_report_layout_t *layout =
      find_report_layout(parser, report_id);
  if (layout != NULL) {
    return layout;
  }
  if (parser->report_layout_count >= JH_BLUETOOTH_GAMEPAD_REPORT_ID_CAPACITY) {
    return NULL;
  }
  layout = &parser->report_layouts[parser->report_layout_count++];
  memset(layout, 0, sizeof(*layout));
  layout->report_id = report_id;
  return layout;
}

static bool next_item(const uint8_t *descriptor, uint16_t descriptor_length,
                      uint16_t *position, jh_hid_item_t *out_item) {
  static const uint8_t sizes[4] = {0u, 1u, 2u, 4u};
  while (*position < descriptor_length) {
    const uint8_t prefix = descriptor[(*position)++];
    if (prefix == UINT8_C(0xfe)) {
      if ((uint32_t)*position + 2u > descriptor_length) {
        return false;
      }
      const uint8_t data_size = descriptor[(*position)++];
      ++*position; /* Long-item tag. */
      if ((uint32_t)*position + data_size > descriptor_length) {
        return false;
      }
      *position = (uint16_t)(*position + data_size);
      continue;
    }
    const uint8_t data_size = sizes[prefix & 0x03u];
    if ((uint32_t)*position + data_size > descriptor_length) {
      return false;
    }
    uint32_t value = 0u;
    for (uint8_t index = 0u; index < data_size; ++index) {
      value |= (uint32_t)descriptor[(*position)++] << (index * 8u);
    }
    int32_t signed_value = (int32_t)value;
    if (data_size > 0u && data_size < 4u) {
      const uint8_t bit_count = (uint8_t)(data_size * 8u);
      const uint32_t sign_bit = UINT32_C(1) << (bit_count - 1u);
      if ((value & sign_bit) != 0u) {
        signed_value = (int32_t)(value | ~(sign_bit - 1u));
      }
    }
    out_item->value = value;
    out_item->signed_value = signed_value;
    out_item->data_size = data_size;
    out_item->type = (uint8_t)((prefix >> 2u) & 0x03u);
    out_item->tag = (uint8_t)(prefix >> 4u);
    return true;
  }
  memset(out_item, 0, sizeof(*out_item));
  out_item->type = JH_HID_ITEM_TYPE_RESERVED;
  return true;
}

static uint32_t local_usage_value(const jh_hid_item_t *item,
                                  uint16_t usage_page) {
  return item->data_size > 2u
             ? item->value
             : ((uint32_t)usage_page << 16u) | (item->value & UINT32_C(0xffff));
}

static void reset_locals(jh_hid_locals_t *locals) {
  memset(locals, 0, sizeof(*locals));
}

static bool accepted_application_usage(uint32_t usage) {
  const uint16_t usage_page = (uint16_t)(usage >> 16u);
  const uint16_t usage_id = (uint16_t)usage;
  return usage_page == JH_HID_USAGE_PAGE_DESKTOP &&
         (usage_id == JH_HID_USAGE_GAME_PAD ||
          usage_id == JH_HID_USAGE_JOYSTICK);
}

static bool local_usage_at(const jh_hid_locals_t *locals, uint8_t index,
                           uint32_t *out_usage) {
  if (index < locals->usage_count) {
    *out_usage = locals->usages[index];
    return true;
  }
  if (locals->have_usage_minimum && locals->have_usage_maximum &&
      locals->usage_maximum >= locals->usage_minimum) {
    const uint32_t offset = index;
    const uint32_t available =
        locals->usage_maximum - locals->usage_minimum + 1u;
    *out_usage =
        locals->usage_minimum + (offset < available ? offset : available - 1u);
    return true;
  }
  if (locals->usage_count > 0u) {
    *out_usage = locals->usages[locals->usage_count - 1u];
    return true;
  }
  return false;
}

static bool axis_index_for_usage(uint16_t usage, uint8_t *out_index) {
  if (usage < JH_HID_USAGE_X || usage > JH_HID_USAGE_WHEEL) {
    return false;
  }
  *out_index = (uint8_t)(usage - JH_HID_USAGE_X);
  return *out_index < JH_BLUETOOTH_GAMEPAD_AXIS_COUNT;
}

static bool mapped_field(const jh_hid_usage_field_t *item,
                         jh_bluetooth_gamepad_field_kind_t *out_kind,
                         uint8_t *out_index) {
  if (item->usage_page == JH_HID_USAGE_PAGE_BUTTON && item->usage >= 1u &&
      item->usage <= JH_BLUETOOTH_GAMEPAD_BUTTON_COUNT) {
    *out_kind = JH_BLUETOOTH_GAMEPAD_FIELD_BUTTON;
    *out_index = (uint8_t)(item->usage - 1u);
    return true;
  }
  if (item->usage_page != JH_HID_USAGE_PAGE_DESKTOP) {
    return false;
  }
  if (item->usage == JH_HID_USAGE_HAT_SWITCH) {
    *out_kind = JH_BLUETOOTH_GAMEPAD_FIELD_HAT;
    *out_index = 0u;
    return true;
  }
  if (axis_index_for_usage(item->usage, out_index)) {
    *out_kind = JH_BLUETOOTH_GAMEPAD_FIELD_AXIS;
    return true;
  }
  return false;
}

static bool duplicate_field(const jh_bluetooth_gamepad_parser_t *parser,
                            jh_bluetooth_gamepad_field_kind_t kind,
                            uint8_t index) {
  for (uint8_t field_index = 0u; field_index < parser->field_count;
       ++field_index) {
    const jh_bluetooth_gamepad_field_t *field = &parser->fields[field_index];
    if (field->kind == kind && field->index == index) {
      return true;
    }
  }
  return false;
}

static hal_status_t append_field(jh_bluetooth_gamepad_parser_t *parser,
                                 const jh_hid_usage_field_t *item) {
  jh_bluetooth_gamepad_field_kind_t kind;
  uint8_t index = 0u;
  if (!mapped_field(item, &kind, &index)) {
    ++parser->diagnostics.ignored_usages;
    return HAL_OK;
  }
  if (item->bit_size == 0u || item->bit_size > 32u ||
      item->logical_minimum > item->logical_maximum) {
    return HAL_EPROTO;
  }
  if (duplicate_field(parser, kind, index)) {
    return HAL_EEXIST;
  }
  if (parser->field_count >= JH_BLUETOOTH_GAMEPAD_FIELD_CAPACITY) {
    return HAL_EOVERFLOW;
  }

  jh_bluetooth_gamepad_field_t *field = &parser->fields[parser->field_count++];
  field->logical_minimum = item->logical_minimum;
  field->logical_maximum = item->logical_maximum;
  field->report_id = item->report_id;
  field->bit_position = item->bit_position;
  field->bit_size = item->bit_size;
  field->index = index;
  field->main_flags = item->main_flags;
  field->kind = kind;
  return HAL_OK;
}

static hal_status_t collect_input_fields(jh_bluetooth_gamepad_parser_t *parser,
                                         const jh_hid_globals_t *globals,
                                         const jh_hid_locals_t *locals,
                                         uint16_t bit_position,
                                         uint8_t main_flags) {
  if ((main_flags & JH_HID_MAIN_FLAG_VARIABLE) == 0u) {
    parser->diagnostics.ignored_usages += globals->report_count;
    return HAL_OK;
  }
  for (uint8_t index = 0u; index < globals->report_count; ++index) {
    uint32_t usage = 0u;
    if (!local_usage_at(locals, index, &usage)) {
      ++parser->diagnostics.ignored_usages;
      continue;
    }
    const jh_hid_usage_field_t field = {
        .usage_page = (uint16_t)(usage >> 16u),
        .usage = (uint16_t)usage,
        .report_id = globals->report_id,
        .bit_position =
            (uint16_t)(bit_position + (uint16_t)(index * globals->report_size)),
        .logical_minimum = globals->logical_minimum,
        .logical_maximum = globals->logical_maximum,
        .bit_size = globals->report_size,
        .main_flags = main_flags,
    };
    const hal_status_t status = append_field(parser, &field);
    if (status != HAL_OK) {
      return status;
    }
  }
  return HAL_OK;
}

static hal_status_t
parse_descriptor(jh_bluetooth_gamepad_parser_t *parser,
                 const uint8_t *descriptor, uint16_t descriptor_length,
                 jh_bluetooth_gamepad_reject_reason_t *out_reject_reason) {
  jh_hid_globals_t globals = {
      .logical_minimum = 0,
      .logical_maximum = 0,
      .usage_page = 0u,
      .report_id = JH_HID_REPORT_ID_UNDEFINED,
      .report_size = 0u,
      .report_count = 0u,
  };
  jh_hid_globals_t global_stack[JH_HID_GLOBAL_STACK_MAX];
  jh_hid_locals_t locals;
  bool accepted_collections[JH_HID_COLLECTION_DEPTH_MAX] = {false};
  uint16_t position = 0u;
  uint8_t global_depth = 0u;
  uint8_t collection_depth = 0u;
  bool found_gamepad = false;
  bool input_without_id = false;
  bool input_with_id = false;
  reset_locals(&locals);
  *out_reject_reason = JH_BLUETOOTH_GAMEPAD_REJECT_DESCRIPTOR_MALFORMED;

  while (position < descriptor_length) {
    jh_hid_item_t item;
    if (!next_item(descriptor, descriptor_length, &position, &item)) {
      return HAL_EPROTO;
    }
    if (item.type == JH_HID_ITEM_TYPE_GLOBAL) {
      switch (item.tag) {
      case JH_HID_GLOBAL_USAGE_PAGE:
        if (item.value > UINT16_MAX) {
          return HAL_EPROTO;
        }
        globals.usage_page = (uint16_t)item.value;
        break;
      case JH_HID_GLOBAL_LOGICAL_MINIMUM:
        globals.logical_minimum = item.signed_value;
        break;
      case JH_HID_GLOBAL_LOGICAL_MAXIMUM:
        globals.logical_maximum = item.signed_value;
        break;
      case JH_HID_GLOBAL_REPORT_SIZE:
        if (item.value == 0u || item.value > 32u) {
          return HAL_EPROTO;
        }
        globals.report_size = (uint8_t)item.value;
        break;
      case JH_HID_GLOBAL_REPORT_ID:
        if (item.value == 0u || item.value > UINT8_MAX) {
          return HAL_EPROTO;
        }
        globals.report_id = (uint16_t)item.value;
        parser->report_ids_declared = true;
        break;
      case JH_HID_GLOBAL_REPORT_COUNT:
        if (item.value == 0u || item.value > UINT8_MAX) {
          return HAL_EPROTO;
        }
        globals.report_count = (uint8_t)item.value;
        break;
      case JH_HID_GLOBAL_PUSH:
        if (global_depth >= JH_HID_GLOBAL_STACK_MAX) {
          return HAL_EOVERFLOW;
        }
        global_stack[global_depth++] = globals;
        break;
      case JH_HID_GLOBAL_POP:
        if (global_depth == 0u) {
          return HAL_EPROTO;
        }
        globals = global_stack[--global_depth];
        break;
      case JH_HID_GLOBAL_PHYSICAL_MINIMUM:
      case JH_HID_GLOBAL_PHYSICAL_MAXIMUM:
      case JH_HID_GLOBAL_UNIT_EXPONENT:
      case JH_HID_GLOBAL_UNIT:
        break;
      default:
        return HAL_EPROTO;
      }
      continue;
    }
    if (item.type == JH_HID_ITEM_TYPE_LOCAL) {
      const uint32_t usage = local_usage_value(&item, globals.usage_page);
      if (item.tag == JH_HID_LOCAL_USAGE) {
        if (locals.usage_count >= JH_HID_LOCAL_USAGE_CAPACITY) {
          return HAL_EOVERFLOW;
        }
        locals.usages[locals.usage_count++] = usage;
      } else if (item.tag == JH_HID_LOCAL_USAGE_MINIMUM) {
        locals.usage_minimum = usage;
        locals.have_usage_minimum = true;
      } else if (item.tag == JH_HID_LOCAL_USAGE_MAXIMUM) {
        locals.usage_maximum = usage;
        locals.have_usage_maximum = true;
      }
      continue;
    }
    if (item.type != JH_HID_ITEM_TYPE_MAIN) {
      continue;
    }

    hal_status_t main_status = HAL_OK;
    if (item.tag == JH_HID_MAIN_COLLECTION) {
      if (collection_depth >= JH_HID_COLLECTION_DEPTH_MAX) {
        return HAL_EOVERFLOW;
      }
      uint32_t usage = 0u;
      const bool have_usage =
          locals.usage_count > 0u
              ? (usage = locals.usages[locals.usage_count - 1u], true)
              : local_usage_at(&locals, 0u, &usage);
      const bool parent_accepted =
          collection_depth > 0u && accepted_collections[collection_depth - 1u];
      const bool this_accepted =
          parent_accepted || (item.value == JH_HID_COLLECTION_APPLICATION &&
                              have_usage && accepted_application_usage(usage));
      accepted_collections[collection_depth++] = this_accepted;
      found_gamepad = found_gamepad || this_accepted;
    } else if (item.tag == JH_HID_MAIN_END_COLLECTION) {
      if (collection_depth == 0u) {
        return HAL_EPROTO;
      }
      --collection_depth;
    } else if (item.tag == JH_HID_MAIN_INPUT) {
      if (globals.report_size == 0u || globals.report_count == 0u) {
        return HAL_EPROTO;
      }
      if (globals.report_id == JH_HID_REPORT_ID_UNDEFINED) {
        input_without_id = true;
      } else {
        input_with_id = true;
      }
      if (input_with_id && input_without_id) {
        return HAL_EPROTO;
      }
      jh_bluetooth_gamepad_report_layout_t *layout =
          get_report_layout(parser, globals.report_id);
      if (layout == NULL) {
        *out_reject_reason = JH_BLUETOOTH_GAMEPAD_REJECT_TOO_MANY_REPORT_IDS;
        return HAL_EOVERFLOW;
      }
      const uint32_t item_bits =
          (uint32_t)globals.report_size * globals.report_count;
      const uint32_t total_bits = (uint32_t)layout->bit_length + item_bits;
      const uint32_t total_bytes =
          ((total_bits + 7u) / 8u) +
          (globals.report_id == JH_HID_REPORT_ID_UNDEFINED ? 0u : 1u);
      if (total_bits > UINT16_MAX ||
          total_bytes > JH_BLUETOOTH_GAMEPAD_REPORT_MAX) {
        return HAL_EOVERFLOW;
      }
      const bool inside_gamepad =
          collection_depth > 0u && accepted_collections[collection_depth - 1u];
      if (inside_gamepad && (item.value & JH_HID_MAIN_FLAG_CONSTANT) == 0u) {
        main_status = collect_input_fields(
            parser, &globals, &locals, layout->bit_length, (uint8_t)item.value);
      }
      layout->bit_length = (uint16_t)total_bits;
      layout->required_bytes = (uint8_t)total_bytes;
    }
    reset_locals(&locals);
    if (main_status != HAL_OK) {
      if (main_status == HAL_EOVERFLOW) {
        *out_reject_reason = JH_BLUETOOTH_GAMEPAD_REJECT_TOO_MANY_FIELDS;
      } else if (main_status == HAL_EEXIST) {
        *out_reject_reason = JH_BLUETOOTH_GAMEPAD_REJECT_DUPLICATE_USAGE;
      }
      return main_status;
    }
  }
  if (collection_depth != 0u || global_depth != 0u) {
    return HAL_EPROTO;
  }
  if (!found_gamepad || parser->field_count == 0u) {
    *out_reject_reason = JH_BLUETOOTH_GAMEPAD_REJECT_UNSUPPORTED_COLLECTION;
    return HAL_EUNSUPPORTED;
  }
  return parser->report_layout_count == 0u ? HAL_EPROTO : HAL_OK;
}

hal_status_t
jh_bluetooth_gamepad_parser_configure(jh_bluetooth_gamepad_parser_t *parser,
                                      const uint8_t *descriptor,
                                      size_t descriptor_length) {
  if (parser == NULL) {
    return HAL_EINVAL;
  }
  jh_bluetooth_gamepad_parser_init(parser);
  if (descriptor == NULL || descriptor_length == 0u) {
    return reject_descriptor(parser, HAL_EINVAL,
                             JH_BLUETOOTH_GAMEPAD_REJECT_INVALID_ARGUMENT);
  }
  parser->diagnostics.descriptor_length_high_water =
      descriptor_length > UINT16_MAX ? UINT16_MAX : (uint16_t)descriptor_length;
  if (descriptor_length > JH_BLUETOOTH_GAMEPAD_DESCRIPTOR_MAX) {
    return reject_descriptor(parser, HAL_EOVERFLOW,
                             JH_BLUETOOTH_GAMEPAD_REJECT_DESCRIPTOR_TOO_LARGE);
  }

  jh_bluetooth_gamepad_reject_reason_t reject_reason =
      JH_BLUETOOTH_GAMEPAD_REJECT_DESCRIPTOR_MALFORMED;
  const hal_status_t descriptor_status = parse_descriptor(
      parser, descriptor, (uint16_t)descriptor_length, &reject_reason);
  if (descriptor_status != HAL_OK) {
    return reject_descriptor(parser, descriptor_status, reject_reason);
  }

  parser->configured = true;
  ++parser->diagnostics.descriptors_accepted;
  parser->diagnostics.last_status = HAL_OK;
  parser->diagnostics.last_reject_reason = JH_BLUETOOTH_GAMEPAD_REJECT_NONE;
  return HAL_OK;
}

static bool snapshots_equal(const jh_bluetooth_gamepad_snapshot_t *left,
                            const jh_bluetooth_gamepad_snapshot_t *right) {
  return left->generation == right->generation &&
         left->buttons == right->buttons &&
         left->axes_present == right->axes_present &&
         left->dpad == right->dpad && left->connected == right->connected &&
         memcmp(left->axes, right->axes, sizeof(left->axes)) == 0;
}

static void enqueue_snapshot(jh_bluetooth_gamepad_parser_t *parser) {
  if (parser->queue_count == JH_BLUETOOTH_GAMEPAD_QUEUE_CAPACITY) {
    parser->diagnostics.dropped_snapshots += parser->queue_count;
    parser->queue_head = 0u;
    parser->queue_count = 0u;
    parser->overflow_pending = true;
    parser->diagnostics.last_status = HAL_EOVERFLOW;
    parser->diagnostics.last_reject_reason =
        JH_BLUETOOTH_GAMEPAD_REJECT_QUEUE_OVERFLOW;
  }
  const uint8_t tail = (uint8_t)((parser->queue_head + parser->queue_count) %
                                 JH_BLUETOOTH_GAMEPAD_QUEUE_CAPACITY);
  parser->queue[tail] = parser->current;
  ++parser->queue_count;
  if (parser->queue_count > parser->diagnostics.queue_high_water) {
    parser->diagnostics.queue_high_water = parser->queue_count;
  }
}

hal_status_t jh_bluetooth_gamepad_parser_connection_opened(
    jh_bluetooth_gamepad_parser_t *parser) {
  if (parser == NULL) {
    return HAL_EINVAL;
  }
  if (!parser->configured) {
    return HAL_EUNINIT;
  }
  if (parser->current.connected) {
    return HAL_ESTATE;
  }
  ++parser->current.generation;
  parser->current.connected = true;
  parser->current.buttons = 0u;
  parser->current.axes_present = 0u;
  parser->current.dpad = JH_BLUETOOTH_GAMEPAD_DPAD_NONE;
  memset(parser->current.axes, 0, sizeof(parser->current.axes));
  parser->diagnostics.last_status = HAL_OK;
  parser->diagnostics.last_reject_reason = JH_BLUETOOTH_GAMEPAD_REJECT_NONE;
  enqueue_snapshot(parser);
  return HAL_OK;
}

hal_status_t jh_bluetooth_gamepad_parser_connection_closed(
    jh_bluetooth_gamepad_parser_t *parser) {
  if (parser == NULL) {
    return HAL_EINVAL;
  }
  if (!parser->configured) {
    return HAL_EUNINIT;
  }
  if (!parser->current.connected) {
    return HAL_ESTATE;
  }
  parser->current.connected = false;
  parser->current.buttons = 0u;
  parser->current.axes_present = 0u;
  parser->current.dpad = JH_BLUETOOTH_GAMEPAD_DPAD_NONE;
  memset(parser->current.axes, 0, sizeof(parser->current.axes));
  parser->diagnostics.last_status = HAL_OK;
  parser->diagnostics.last_reject_reason = JH_BLUETOOTH_GAMEPAD_REJECT_NONE;
  enqueue_snapshot(parser);
  return HAL_OK;
}

static uint32_t read_unsigned_field(const uint8_t *report,
                                    uint16_t bit_position, uint8_t bit_size) {
  const uint8_t byte_offset = (uint8_t)(bit_position / 8u);
  const uint8_t bit_offset = (uint8_t)(bit_position % 8u);
  const uint8_t byte_count = (uint8_t)((bit_offset + bit_size + 7u) / 8u);
  uint64_t packed = 0u;
  for (uint8_t index = 0u; index < byte_count; ++index) {
    packed |= (uint64_t)report[byte_offset + index] << (index * 8u);
  }
  packed >>= bit_offset;
  const uint64_t mask =
      bit_size == 32u ? UINT32_MAX : ((UINT64_C(1) << bit_size) - 1u);
  return (uint32_t)(packed & mask);
}

static int32_t sign_extend(uint32_t value, uint8_t bit_size) {
  if (bit_size == 32u) {
    return (int32_t)value;
  }
  const uint32_t sign_bit = UINT32_C(1) << (bit_size - 1u);
  if ((value & sign_bit) == 0u) {
    return (int32_t)value;
  }
  return (int32_t)(value | ~(sign_bit - 1u));
}

static int16_t normalize_axis(int32_t value, int32_t logical_minimum,
                              int32_t logical_maximum) {
  const int64_t range = (int64_t)logical_maximum - logical_minimum;
  const int64_t offset = (int64_t)value - logical_minimum;
  const int64_t scaled = ((offset * INT64_C(65534)) / range) - INT64_C(32767);
  if (scaled < INT16_MIN + 1) {
    return INT16_MIN + 1;
  }
  if (scaled > INT16_MAX) {
    return INT16_MAX;
  }
  return (int16_t)scaled;
}

static uint8_t normalize_hat(int32_t value, int32_t logical_minimum,
                             int32_t logical_maximum, uint8_t main_flags,
                             bool *valid) {
  if (value < logical_minimum || value > logical_maximum) {
    *valid = (main_flags & JH_HID_MAIN_FLAG_NULL_STATE) != 0u;
    return JH_BLUETOOTH_GAMEPAD_DPAD_NONE;
  }
  const int32_t position = value - logical_minimum;
  if (position < 0 || position > 7) {
    *valid = false;
    return JH_BLUETOOTH_GAMEPAD_DPAD_NONE;
  }
  static const uint8_t directions[8] = {
      JH_BLUETOOTH_GAMEPAD_DPAD_UP,
      JH_BLUETOOTH_GAMEPAD_DPAD_UP | JH_BLUETOOTH_GAMEPAD_DPAD_RIGHT,
      JH_BLUETOOTH_GAMEPAD_DPAD_RIGHT,
      JH_BLUETOOTH_GAMEPAD_DPAD_RIGHT | JH_BLUETOOTH_GAMEPAD_DPAD_DOWN,
      JH_BLUETOOTH_GAMEPAD_DPAD_DOWN,
      JH_BLUETOOTH_GAMEPAD_DPAD_DOWN | JH_BLUETOOTH_GAMEPAD_DPAD_LEFT,
      JH_BLUETOOTH_GAMEPAD_DPAD_LEFT,
      JH_BLUETOOTH_GAMEPAD_DPAD_LEFT | JH_BLUETOOTH_GAMEPAD_DPAD_UP,
  };
  *valid = true;
  return directions[position];
}

static bool apply_field(jh_bluetooth_gamepad_snapshot_t *snapshot,
                        const jh_bluetooth_gamepad_field_t *field,
                        const uint8_t *report) {
  const uint16_t bit_position =
      (uint16_t)(field->bit_position +
                 (field->report_id == JH_HID_REPORT_ID_UNDEFINED ? 0u : 8u));
  const uint32_t raw =
      read_unsigned_field(report, bit_position, field->bit_size);
  const int32_t value = field->logical_minimum < 0
                            ? sign_extend(raw, field->bit_size)
                            : (int32_t)raw;

  if (field->kind == JH_BLUETOOTH_GAMEPAD_FIELD_HAT) {
    bool valid = false;
    snapshot->dpad =
        normalize_hat(value, field->logical_minimum, field->logical_maximum,
                      field->main_flags, &valid);
    return valid;
  }
  if (value < field->logical_minimum || value > field->logical_maximum) {
    return false;
  }
  if (field->kind == JH_BLUETOOTH_GAMEPAD_FIELD_BUTTON) {
    const uint32_t mask = UINT32_C(1) << field->index;
    if (value != 0) {
      snapshot->buttons |= mask;
    } else {
      snapshot->buttons &= ~mask;
    }
    return true;
  }
  if (field->logical_maximum == field->logical_minimum) {
    return false;
  }
  snapshot->axes[field->index] =
      normalize_axis(value, field->logical_minimum, field->logical_maximum);
  snapshot->axes_present |= (uint16_t)(1u << field->index);
  return true;
}

hal_status_t
jh_bluetooth_gamepad_parser_parse_input(jh_bluetooth_gamepad_parser_t *parser,
                                        const uint8_t *report,
                                        size_t report_length) {
  if (parser == NULL || report == NULL || report_length == 0u) {
    if (parser == NULL) {
      return HAL_EINVAL;
    }
    return reject_report(parser, HAL_EINVAL,
                         JH_BLUETOOTH_GAMEPAD_REJECT_INVALID_ARGUMENT);
  }
  ++parser->diagnostics.reports_received;
  if (UINT32_MAX - parser->diagnostics.report_bytes < report_length) {
    parser->diagnostics.report_bytes = UINT32_MAX;
  } else {
    parser->diagnostics.report_bytes += (uint32_t)report_length;
  }
  if (report_length > parser->diagnostics.report_length_high_water) {
    parser->diagnostics.report_length_high_water =
        report_length > UINT16_MAX ? UINT16_MAX : (uint16_t)report_length;
  }
  if (!parser->configured) {
    return reject_report(parser, HAL_EUNINIT,
                         JH_BLUETOOTH_GAMEPAD_REJECT_INVALID_ARGUMENT);
  }
  if (!parser->current.connected) {
    return reject_report(parser, HAL_ESTATE,
                         JH_BLUETOOTH_GAMEPAD_REJECT_DISCONNECTED);
  }
  if (report_length > JH_BLUETOOTH_GAMEPAD_REPORT_MAX) {
    return reject_report(parser, HAL_EOVERFLOW,
                         JH_BLUETOOTH_GAMEPAD_REJECT_REPORT_TOO_LARGE);
  }

  const uint16_t report_id =
      parser->report_ids_declared ? report[0] : JH_HID_REPORT_ID_UNDEFINED;
  jh_bluetooth_gamepad_report_layout_t *layout =
      find_report_layout(parser, report_id);
  if (layout == NULL) {
    ++parser->diagnostics.unknown_report_ids;
    return reject_report(parser, HAL_ENOENT,
                         JH_BLUETOOTH_GAMEPAD_REJECT_UNKNOWN_REPORT_ID);
  }
  if (report_length < layout->required_bytes) {
    ++parser->diagnostics.truncated_reports;
    return reject_report(parser, HAL_EPROTO,
                         JH_BLUETOOTH_GAMEPAD_REJECT_REPORT_TOO_SHORT);
  }

  jh_bluetooth_gamepad_snapshot_t next = parser->current;
  for (uint8_t index = 0u; index < parser->field_count; ++index) {
    const jh_bluetooth_gamepad_field_t *field = &parser->fields[index];
    if (field->report_id == report_id && !apply_field(&next, field, report)) {
      return reject_report(parser, HAL_EPROTO,
                           JH_BLUETOOTH_GAMEPAD_REJECT_REPORT_VALUE);
    }
  }

  ++parser->diagnostics.reports_accepted;
  parser->diagnostics.last_status = HAL_OK;
  parser->diagnostics.last_reject_reason = JH_BLUETOOTH_GAMEPAD_REJECT_NONE;
  if (snapshots_equal(&next, &parser->current)) {
    ++parser->diagnostics.duplicate_reports;
    return HAL_OK;
  }
  parser->current = next;
  ++parser->diagnostics.state_changes;
  enqueue_snapshot(parser);
  return HAL_OK;
}

hal_status_t jh_bluetooth_gamepad_parser_snapshot(
    const jh_bluetooth_gamepad_parser_t *parser,
    jh_bluetooth_gamepad_snapshot_t *out_snapshot) {
  if (parser == NULL || out_snapshot == NULL) {
    return HAL_EINVAL;
  }
  if (!parser->configured) {
    return HAL_EUNINIT;
  }
  *out_snapshot = parser->current;
  return HAL_OK;
}

hal_status_t jh_bluetooth_gamepad_parser_next(
    jh_bluetooth_gamepad_parser_t *parser,
    jh_bluetooth_gamepad_snapshot_t *out_snapshot) {
  if (parser == NULL || out_snapshot == NULL) {
    return HAL_EINVAL;
  }
  if (!parser->configured) {
    return HAL_EUNINIT;
  }
  if (parser->overflow_pending) {
    parser->overflow_pending = false;
    return HAL_EOVERFLOW;
  }
  if (parser->queue_count == 0u) {
    return HAL_EAGAIN;
  }
  *out_snapshot = parser->queue[parser->queue_head];
  parser->queue_head = (uint8_t)((parser->queue_head + 1u) %
                                 JH_BLUETOOTH_GAMEPAD_QUEUE_CAPACITY);
  --parser->queue_count;
  return HAL_OK;
}

void jh_bluetooth_gamepad_parser_diagnostics(
    const jh_bluetooth_gamepad_parser_t *parser,
    jh_bluetooth_gamepad_parser_diagnostics_t *out_diagnostics) {
  if (parser != NULL && out_diagnostics != NULL) {
    *out_diagnostics = parser->diagnostics;
  }
}
