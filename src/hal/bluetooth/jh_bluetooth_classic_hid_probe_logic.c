#include "jh_bluetooth_classic_hid_probe_logic.h"

#include <string.h>

enum { JH_ZERO2_AXIS_DIGITAL_THRESHOLD = 30000 };

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
  return jh_bluetooth_gamepad_candidate_matches(class_of_device, name,
                                                name_length);
}

bool jh_bluetooth_classic_hid_probe_logic_pnp_matches(uint16_t vendor_id,
                                                      uint16_t product_id,
                                                      uint16_t version) {
  return jh_bluetooth_gamepad_pnp_matches(vendor_id, product_id, version);
}

void jh_bluetooth_classic_hid_probe_logic_connected(
    jh_bluetooth_classic_hid_probe_logic_t *logic) {
  if (logic != NULL) {
    logic->connected = true;
    logic->discovery_open = false;
    logic->active_controls_mask = 0u;
  }
}

static uint16_t
decode_zero2_controls(const jh_bluetooth_gamepad_snapshot_t *snapshot) {
  const uint32_t buttons = snapshot->buttons;
  uint16_t controls = 0u;
  controls |= (buttons & (UINT32_C(1) << 0u)) != 0u ? 1u << 0u : 0u;
  controls |= (buttons & (UINT32_C(1) << 1u)) != 0u ? 1u << 1u : 0u;
  controls |= (buttons & (UINT32_C(1) << 3u)) != 0u ? 1u << 2u : 0u;
  controls |= (buttons & (UINT32_C(1) << 4u)) != 0u ? 1u << 3u : 0u;
  controls |= (buttons & (UINT32_C(1) << 6u)) != 0u ? 1u << 4u : 0u;
  controls |= (buttons & (UINT32_C(1) << 7u)) != 0u ? 1u << 5u : 0u;
  controls |= (buttons & (UINT32_C(1) << 10u)) != 0u ? 1u << 6u : 0u;
  controls |= (buttons & (UINT32_C(1) << 11u)) != 0u ? 1u << 7u : 0u;

  const uint8_t dpad = snapshot->dpad;
  const int16_t x = snapshot->axes[JH_BLUETOOTH_GAMEPAD_AXIS_X];
  const int16_t y = snapshot->axes[JH_BLUETOOTH_GAMEPAD_AXIS_Y];
  controls |= ((dpad & JH_BLUETOOTH_GAMEPAD_DPAD_UP) != 0u ||
               y <= -JH_ZERO2_AXIS_DIGITAL_THRESHOLD)
                  ? 1u << 8u
                  : 0u;
  controls |= ((dpad & JH_BLUETOOTH_GAMEPAD_DPAD_DOWN) != 0u ||
               y >= JH_ZERO2_AXIS_DIGITAL_THRESHOLD)
                  ? 1u << 9u
                  : 0u;
  controls |= ((dpad & JH_BLUETOOTH_GAMEPAD_DPAD_LEFT) != 0u ||
               x <= -JH_ZERO2_AXIS_DIGITAL_THRESHOLD)
                  ? 1u << 10u
                  : 0u;
  controls |= ((dpad & JH_BLUETOOTH_GAMEPAD_DPAD_RIGHT) != 0u ||
               x >= JH_ZERO2_AXIS_DIGITAL_THRESHOLD)
                  ? 1u << 11u
                  : 0u;
  return controls;
}

uint16_t jh_bluetooth_classic_hid_probe_logic_report(
    jh_bluetooth_classic_hid_probe_logic_t *logic,
    const jh_bluetooth_gamepad_snapshot_t *snapshot, size_t report_length) {
  if (logic == NULL || snapshot == NULL || !logic->connected ||
      !snapshot->connected) {
    return 0u;
  }
  ++logic->reports;
  logic->report_bytes += (uint32_t)report_length;
  if (report_length > logic->report_length_high_water) {
    logic->report_length_high_water = (uint16_t)report_length;
  }
  logic->active_controls_mask = decode_zero2_controls(snapshot);
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
