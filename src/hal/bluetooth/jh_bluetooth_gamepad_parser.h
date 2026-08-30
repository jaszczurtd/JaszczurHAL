#pragma once

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  JH_BLUETOOTH_GAMEPAD_DESCRIPTOR_MAX = 256u,
  JH_BLUETOOTH_GAMEPAD_REPORT_MAX = 32u,
  JH_BLUETOOTH_GAMEPAD_QUEUE_CAPACITY = 16u,
  JH_BLUETOOTH_GAMEPAD_BUTTON_COUNT = 32u,
  JH_BLUETOOTH_GAMEPAD_AXIS_COUNT = 9u,
  JH_BLUETOOTH_GAMEPAD_FIELD_CAPACITY = 48u,
  JH_BLUETOOTH_GAMEPAD_REPORT_ID_CAPACITY = 8u,
};

typedef enum {
  JH_BLUETOOTH_GAMEPAD_AXIS_X = 0,
  JH_BLUETOOTH_GAMEPAD_AXIS_Y,
  JH_BLUETOOTH_GAMEPAD_AXIS_Z,
  JH_BLUETOOTH_GAMEPAD_AXIS_RX,
  JH_BLUETOOTH_GAMEPAD_AXIS_RY,
  JH_BLUETOOTH_GAMEPAD_AXIS_RZ,
  JH_BLUETOOTH_GAMEPAD_AXIS_SLIDER,
  JH_BLUETOOTH_GAMEPAD_AXIS_DIAL,
  JH_BLUETOOTH_GAMEPAD_AXIS_WHEEL,
} jh_bluetooth_gamepad_axis_t;

typedef enum {
  JH_BLUETOOTH_GAMEPAD_DPAD_NONE = 0u,
  JH_BLUETOOTH_GAMEPAD_DPAD_UP = (1u << 0),
  JH_BLUETOOTH_GAMEPAD_DPAD_RIGHT = (1u << 1),
  JH_BLUETOOTH_GAMEPAD_DPAD_DOWN = (1u << 2),
  JH_BLUETOOTH_GAMEPAD_DPAD_LEFT = (1u << 3),
} jh_bluetooth_gamepad_dpad_t;

typedef struct {
  uint32_t generation;
  uint32_t buttons;
  int16_t axes[JH_BLUETOOTH_GAMEPAD_AXIS_COUNT];
  uint16_t axes_present;
  uint8_t dpad;
  bool connected;
} jh_bluetooth_gamepad_snapshot_t;

typedef enum {
  JH_BLUETOOTH_GAMEPAD_REJECT_NONE = 0,
  JH_BLUETOOTH_GAMEPAD_REJECT_INVALID_ARGUMENT,
  JH_BLUETOOTH_GAMEPAD_REJECT_DESCRIPTOR_TOO_LARGE,
  JH_BLUETOOTH_GAMEPAD_REJECT_DESCRIPTOR_MALFORMED,
  JH_BLUETOOTH_GAMEPAD_REJECT_UNSUPPORTED_COLLECTION,
  JH_BLUETOOTH_GAMEPAD_REJECT_TOO_MANY_FIELDS,
  JH_BLUETOOTH_GAMEPAD_REJECT_TOO_MANY_REPORT_IDS,
  JH_BLUETOOTH_GAMEPAD_REJECT_DUPLICATE_USAGE,
  JH_BLUETOOTH_GAMEPAD_REJECT_REPORT_TOO_LARGE,
  JH_BLUETOOTH_GAMEPAD_REJECT_REPORT_TOO_SHORT,
  JH_BLUETOOTH_GAMEPAD_REJECT_UNKNOWN_REPORT_ID,
  JH_BLUETOOTH_GAMEPAD_REJECT_REPORT_VALUE,
  JH_BLUETOOTH_GAMEPAD_REJECT_DISCONNECTED,
  JH_BLUETOOTH_GAMEPAD_REJECT_QUEUE_OVERFLOW,
} jh_bluetooth_gamepad_reject_reason_t;

typedef struct {
  uint32_t descriptors_accepted;
  uint32_t descriptors_rejected;
  uint32_t reports_received;
  uint32_t reports_accepted;
  uint32_t reports_rejected;
  uint32_t duplicate_reports;
  uint32_t state_changes;
  uint32_t ignored_usages;
  uint32_t unknown_report_ids;
  uint32_t truncated_reports;
  uint32_t dropped_snapshots;
  uint32_t report_bytes;
  uint16_t descriptor_length_high_water;
  uint16_t report_length_high_water;
  uint8_t queue_high_water;
  hal_status_t last_status;
  jh_bluetooth_gamepad_reject_reason_t last_reject_reason;
} jh_bluetooth_gamepad_parser_diagnostics_t;

typedef enum {
  JH_BLUETOOTH_GAMEPAD_FIELD_BUTTON = 0,
  JH_BLUETOOTH_GAMEPAD_FIELD_AXIS,
  JH_BLUETOOTH_GAMEPAD_FIELD_HAT,
} jh_bluetooth_gamepad_field_kind_t;

typedef struct {
  int32_t logical_minimum;
  int32_t logical_maximum;
  uint16_t report_id;
  uint16_t bit_position;
  uint8_t bit_size;
  uint8_t index;
  uint8_t main_flags;
  jh_bluetooth_gamepad_field_kind_t kind;
} jh_bluetooth_gamepad_field_t;

typedef struct {
  uint16_t report_id;
  uint16_t bit_length;
  uint8_t required_bytes;
} jh_bluetooth_gamepad_report_layout_t;

typedef struct {
  jh_bluetooth_gamepad_field_t fields[JH_BLUETOOTH_GAMEPAD_FIELD_CAPACITY];
  jh_bluetooth_gamepad_report_layout_t
      report_layouts[JH_BLUETOOTH_GAMEPAD_REPORT_ID_CAPACITY];
  jh_bluetooth_gamepad_snapshot_t queue[JH_BLUETOOTH_GAMEPAD_QUEUE_CAPACITY];
  jh_bluetooth_gamepad_snapshot_t current;
  jh_bluetooth_gamepad_parser_diagnostics_t diagnostics;
  uint8_t field_count;
  uint8_t report_layout_count;
  uint8_t queue_head;
  uint8_t queue_count;
  bool configured;
  bool report_ids_declared;
  bool overflow_pending;
} jh_bluetooth_gamepad_parser_t;

void jh_bluetooth_gamepad_parser_init(jh_bluetooth_gamepad_parser_t *parser);
hal_status_t
jh_bluetooth_gamepad_parser_configure(jh_bluetooth_gamepad_parser_t *parser,
                                      const uint8_t *descriptor,
                                      size_t descriptor_length);
hal_status_t jh_bluetooth_gamepad_parser_connection_opened(
    jh_bluetooth_gamepad_parser_t *parser);
hal_status_t jh_bluetooth_gamepad_parser_connection_closed(
    jh_bluetooth_gamepad_parser_t *parser);
hal_status_t
jh_bluetooth_gamepad_parser_parse_input(jh_bluetooth_gamepad_parser_t *parser,
                                        const uint8_t *report,
                                        size_t report_length);
hal_status_t jh_bluetooth_gamepad_parser_snapshot(
    const jh_bluetooth_gamepad_parser_t *parser,
    jh_bluetooth_gamepad_snapshot_t *out_snapshot);
hal_status_t
jh_bluetooth_gamepad_parser_next(jh_bluetooth_gamepad_parser_t *parser,
                                 jh_bluetooth_gamepad_snapshot_t *out_snapshot);
void jh_bluetooth_gamepad_parser_diagnostics(
    const jh_bluetooth_gamepad_parser_t *parser,
    jh_bluetooth_gamepad_parser_diagnostics_t *out_diagnostics);

#ifdef __cplusplus
}
#endif
