#include "jh_bluetooth_classic_hid_probe_logic.h"

#include <string.h>

enum {
  JH_CLASS_OF_DEVICE_MAJOR_MASK = 0x1f00u,
  JH_CLASS_OF_DEVICE_MAJOR_PERIPHERAL = 0x0500u,
  JH_ZERO2_REPORT_ID = 0x03u,
  JH_ZERO2_REPORT_MIN_LENGTH = 8u,
};

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms) {
  return (int32_t)(now_ms - deadline_ms) >= 0;
}

void jh_bluetooth_classic_hid_probe_logic_reset(
    jh_bluetooth_classic_hid_probe_logic_t *logic) {
  if (logic != NULL) {
    memset(logic, 0, sizeof(*logic));
  }
}

bool jh_bluetooth_classic_hid_probe_logic_open_discovery(
    jh_bluetooth_classic_hid_probe_logic_t *logic, uint32_t now_ms) {
  if (logic == NULL || logic->discovery_open || logic->connected) {
    return false;
  }
  logic->discovery_open = true;
  logic->discovery_deadline_ms = now_ms + JH_CLASSIC_HID_DISCOVERY_WINDOW_MS;
  return true;
}

bool jh_bluetooth_classic_hid_probe_logic_discovery_expired(
    const jh_bluetooth_classic_hid_probe_logic_t *logic, uint32_t now_ms) {
  return logic != NULL && logic->discovery_open &&
         deadline_reached(now_ms, logic->discovery_deadline_ms);
}

void jh_bluetooth_classic_hid_probe_logic_close_discovery(
    jh_bluetooth_classic_hid_probe_logic_t *logic) {
  if (logic != NULL) {
    logic->discovery_open = false;
  }
}

bool jh_bluetooth_classic_hid_probe_logic_candidate_matches(
    uint32_t class_of_device, const uint8_t *name, size_t name_length) {
  static const char expected_name[] = JH_CLASSIC_HID_EXPECTED_NAME;
  return (class_of_device & JH_CLASS_OF_DEVICE_MAJOR_MASK) ==
             JH_CLASS_OF_DEVICE_MAJOR_PERIPHERAL &&
         name != NULL && name_length == sizeof(expected_name) - 1u &&
         memcmp(name, expected_name, sizeof(expected_name) - 1u) == 0;
}

bool jh_bluetooth_classic_hid_probe_logic_pnp_matches(uint16_t vendor_id,
                                                      uint16_t product_id,
                                                      uint16_t version) {
  return vendor_id == JH_CLASSIC_HID_EXPECTED_VENDOR_ID &&
         product_id == JH_CLASSIC_HID_EXPECTED_PRODUCT_ID &&
         version == JH_CLASSIC_HID_EXPECTED_VERSION;
}

void jh_bluetooth_classic_hid_probe_logic_connected(
    jh_bluetooth_classic_hid_probe_logic_t *logic) {
  if (logic != NULL) {
    logic->connected = true;
    logic->discovery_open = false;
    logic->active_controls_mask = 0u;
  }
}

static uint16_t decode_zero2_controls(const uint8_t *report,
                                      size_t report_length) {
  if (report_length > 0u && report[0] == 0xa1u) {
    ++report;
    --report_length;
  }
  if (report_length < JH_ZERO2_REPORT_MIN_LENGTH ||
      report[0] != JH_ZERO2_REPORT_ID) {
    return 0u;
  }

  const uint16_t buttons = (uint16_t)report[1] | ((uint16_t)report[2] << 8u);
  uint16_t controls = 0u;
  controls |= (buttons & 0x0001u) != 0u ? 1u << 0u : 0u;
  controls |= (buttons & 0x0002u) != 0u ? 1u << 1u : 0u;
  controls |= (buttons & 0x0008u) != 0u ? 1u << 2u : 0u;
  controls |= (buttons & 0x0010u) != 0u ? 1u << 3u : 0u;
  controls |= (buttons & 0x0040u) != 0u ? 1u << 4u : 0u;
  controls |= (buttons & 0x0080u) != 0u ? 1u << 5u : 0u;
  controls |= (buttons & 0x0400u) != 0u ? 1u << 6u : 0u;
  controls |= (buttons & 0x0800u) != 0u ? 1u << 7u : 0u;
  controls |= report[5] == 0u ? 1u << 8u : 0u;
  controls |= report[5] == 0xffu ? 1u << 9u : 0u;
  controls |= report[4] == 0u ? 1u << 10u : 0u;
  controls |= report[4] == 0xffu ? 1u << 11u : 0u;
  return controls;
}

uint16_t jh_bluetooth_classic_hid_probe_logic_report(
    jh_bluetooth_classic_hid_probe_logic_t *logic, const uint8_t *report,
    size_t report_length) {
  if (logic == NULL || report == NULL || !logic->connected) {
    return 0u;
  }
  ++logic->reports;
  logic->report_bytes += (uint32_t)report_length;
  if (report_length > logic->report_length_high_water) {
    logic->report_length_high_water = (uint16_t)report_length;
  }
  logic->active_controls_mask = decode_zero2_controls(report, report_length);
  logic->seen_controls_mask |= logic->active_controls_mask;
  return logic->active_controls_mask;
}

void jh_bluetooth_classic_hid_probe_logic_disconnected(
    jh_bluetooth_classic_hid_probe_logic_t *logic) {
  if (logic == NULL) {
    return;
  }
  if (logic->active_controls_mask != 0u) {
    ++logic->release_all_events;
  }
  logic->active_controls_mask = 0u;
  logic->connected = false;
}
