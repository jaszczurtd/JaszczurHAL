#pragma once

#include "jh_bluetooth_gamepad_identity.h"
#include "jh_bluetooth_gamepad_parser.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  JH_CLASSIC_HID_EXPECTED_VENDOR_ID = JH_BLUETOOTH_GAMEPAD_EXPECTED_VENDOR_ID,
  JH_CLASSIC_HID_EXPECTED_PRODUCT_ID = JH_BLUETOOTH_GAMEPAD_EXPECTED_PRODUCT_ID,
  JH_CLASSIC_HID_EXPECTED_VERSION = JH_BLUETOOTH_GAMEPAD_EXPECTED_VERSION,
  JH_CLASSIC_HID_DISCOVERY_WINDOW_MS = 120000u,
  JH_CLASSIC_HID_CONTROL_COUNT = 12u,
  JH_CLASSIC_HID_ALL_CONTROLS_MASK = 0x0fffu,
};

#define JH_CLASSIC_HID_EXPECTED_NAME JH_BLUETOOTH_GAMEPAD_EXPECTED_NAME

typedef struct {
  uint32_t discovery_deadline_ms;
  uint32_t reports;
  uint32_t report_bytes;
  uint32_t release_all_events;
  uint16_t active_controls_mask;
  uint16_t seen_controls_mask;
  uint16_t report_length_high_water;
  bool discovery_open;
  bool connected;
} jh_bluetooth_classic_hid_probe_logic_t;

void jh_bluetooth_classic_hid_probe_logic_reset(
    jh_bluetooth_classic_hid_probe_logic_t *logic);
bool jh_bluetooth_classic_hid_probe_logic_open_discovery(
    jh_bluetooth_classic_hid_probe_logic_t *logic, uint32_t now_ms);
bool jh_bluetooth_classic_hid_probe_logic_discovery_expired(
    const jh_bluetooth_classic_hid_probe_logic_t *logic, uint32_t now_ms);
void jh_bluetooth_classic_hid_probe_logic_close_discovery(
    jh_bluetooth_classic_hid_probe_logic_t *logic);
bool jh_bluetooth_classic_hid_probe_logic_candidate_matches(
    uint32_t class_of_device, const uint8_t *name, size_t name_length);
bool jh_bluetooth_classic_hid_probe_logic_pnp_matches(uint16_t vendor_id,
                                                      uint16_t product_id,
                                                      uint16_t version);
void jh_bluetooth_classic_hid_probe_logic_connected(
    jh_bluetooth_classic_hid_probe_logic_t *logic);
uint16_t jh_bluetooth_classic_hid_probe_logic_report(
    jh_bluetooth_classic_hid_probe_logic_t *logic,
    const jh_bluetooth_gamepad_snapshot_t *snapshot, size_t report_length);
void jh_bluetooth_classic_hid_probe_logic_disconnected(
    jh_bluetooth_classic_hid_probe_logic_t *logic);

#ifdef __cplusplus
}
#endif
