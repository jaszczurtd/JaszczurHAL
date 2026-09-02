#pragma once

#include "jh_bluetooth_gamepad_identity.h"
#include "jh_bluetooth_gamepad_parser.h"
#include "jh_gamepad_bond_codec.h"

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
  /* Bonding: a peer is trusted enough to persist only once every one of
   * these holds for the current connection attempt: identity
   * (SDP/PnP) validated,
   * descriptor accepted, at least one HID report received (the "input
   * gate" -- proves the link is genuinely flowing data, not just that SDP
   * matched), and a link key notification was captured. */
  bool identity_validated;
  bool descriptor_accepted;
  bool link_key_received;
  bool bond_consumed;
  jh_gamepad_bond_identity_t pending_bond;
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

/** @brief Reset per-candidate bonding progress (identity/descriptor/link-key
 *  flags). Call wherever the probe abandons a validated candidate (e.g. a
 *  connected-candidate rejection) or restarts from scratch -- the same
 *  points that reset the probe's own identity-validated flag. */
void jh_bluetooth_classic_hid_probe_logic_reset_bond_progress(
    jh_bluetooth_classic_hid_probe_logic_t *logic);

/** @brief Mark that SDP/PnP identity validation passed for the current
 *  candidate. Persists across the connected-candidate's HID connection
 *  lifecycle (including reconnects) until reset_bond_progress() is called. */
void jh_bluetooth_classic_hid_probe_logic_identity_validated(
    jh_bluetooth_classic_hid_probe_logic_t *logic);

/** @brief Mark that the report descriptor was accepted (matched the frozen
 *  capture and the parser was configured) for the current connection. */
void jh_bluetooth_classic_hid_probe_logic_descriptor_accepted(
    jh_bluetooth_classic_hid_probe_logic_t *logic);

/** @brief Stage a link key captured from HCI_EVENT_LINK_KEY_NOTIFICATION in
 *  bounded RAM. Safe to call from inside a stack callback: this only copies
 *  bytes into @p logic, it never touches persistent storage. */
void jh_bluetooth_classic_hid_probe_logic_link_key_received(
    jh_bluetooth_classic_hid_probe_logic_t *logic, const uint8_t *bd_addr,
    const uint8_t *link_key, uint8_t link_key_type);

/** @brief True exactly once all bonding conditions hold for the current
 *  connection and the pending identity has not yet been taken. */
bool jh_bluetooth_classic_hid_probe_logic_bond_ready(
    const jh_bluetooth_classic_hid_probe_logic_t *logic);

/** @brief Consume the pending bond identity: returns a pointer to it (valid
 *  until the next call that mutates @p logic) and marks it taken so
 *  bond_ready() will not fire again for this connection. Call only from
 *  outside any stack callback and after the radio lock has been released,
 *  once per readiness, to hand the identity to hal_gamepad's bond provider.
 *  Returns NULL if bond_ready() was false. */
const jh_gamepad_bond_identity_t *
jh_bluetooth_classic_hid_probe_logic_take_pending_bond(
    jh_bluetooth_classic_hid_probe_logic_t *logic);

#ifdef __cplusplus
}
#endif
