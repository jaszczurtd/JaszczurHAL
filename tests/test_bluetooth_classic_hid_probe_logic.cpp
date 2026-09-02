#include "hal/bluetooth/jh_bluetooth_classic_hid_probe_logic.h"

#include <array>
#include <cstring>

#include "utils/unity.h"

namespace {

jh_bluetooth_classic_hid_probe_logic_t s_logic;

void test_discovery_requires_explicit_open_and_expires_after_120_seconds() {
  TEST_ASSERT_FALSE(s_logic.discovery_open);
  TEST_ASSERT_TRUE(
      jh_bluetooth_classic_hid_probe_logic_open_discovery(&s_logic, 1000u));
  TEST_ASSERT_FALSE(
      jh_bluetooth_classic_hid_probe_logic_open_discovery(&s_logic, 1001u));
  TEST_ASSERT_FALSE(jh_bluetooth_classic_hid_probe_logic_discovery_expired(
      &s_logic, 120999u));
  TEST_ASSERT_TRUE(jh_bluetooth_classic_hid_probe_logic_discovery_expired(
      &s_logic, 121000u));
}

void test_discovery_deadline_handles_millisecond_wrap() {
  TEST_ASSERT_TRUE(jh_bluetooth_classic_hid_probe_logic_open_discovery(
      &s_logic, UINT32_MAX - 1000u));
  const uint32_t deadline =
      UINT32_MAX - 1000u + JH_CLASSIC_HID_DISCOVERY_WINDOW_MS;
  TEST_ASSERT_FALSE(jh_bluetooth_classic_hid_probe_logic_discovery_expired(
      &s_logic, deadline - 1u));
  TEST_ASSERT_TRUE(jh_bluetooth_classic_hid_probe_logic_discovery_expired(
      &s_logic, deadline));
}

void test_candidate_filter_requires_peripheral_class_and_exact_name() {
  static constexpr char kName[] = JH_CLASSIC_HID_EXPECTED_NAME;
  TEST_ASSERT_TRUE(jh_bluetooth_classic_hid_probe_logic_candidate_matches(
      0x002508u, reinterpret_cast<const uint8_t *>(kName), sizeof(kName) - 1u));
  TEST_ASSERT_FALSE(jh_bluetooth_classic_hid_probe_logic_candidate_matches(
      0x000104u, reinterpret_cast<const uint8_t *>(kName), sizeof(kName) - 1u));
  TEST_ASSERT_FALSE(jh_bluetooth_classic_hid_probe_logic_candidate_matches(
      0x002508u, reinterpret_cast<const uint8_t *>("Pro Controller"),
      sizeof("Pro Controller") - 1u));
}

void test_pnp_filter_requires_the_characterized_identity() {
  TEST_ASSERT_TRUE(jh_bluetooth_classic_hid_probe_logic_pnp_matches(
      0x2dc8u, 0x3230u, 0x0100u));
  TEST_ASSERT_FALSE(jh_bluetooth_classic_hid_probe_logic_pnp_matches(
      0x2dc8u, 0x3230u, 0x0101u));
  TEST_ASSERT_FALSE(jh_bluetooth_classic_hid_probe_logic_pnp_matches(
      0x057eu, 0x2009u, 0x0100u));
}

void test_reports_cover_all_controls_and_disconnect_releases_active_state() {
  std::array<jh_bluetooth_gamepad_snapshot_t, 12> snapshots{};
  static constexpr std::array<uint32_t, 8> kButtons = {
      UINT32_C(1) << 0u,  UINT32_C(1) << 1u,  UINT32_C(1) << 3u,
      UINT32_C(1) << 4u,  UINT32_C(1) << 6u,  UINT32_C(1) << 7u,
      UINT32_C(1) << 10u, UINT32_C(1) << 11u,
  };
  for (size_t index = 0u; index < snapshots.size(); ++index) {
    snapshots[index].connected = true;
  }
  for (size_t index = 0u; index < kButtons.size(); ++index) {
    snapshots[index].buttons = kButtons[index];
  }
  snapshots[8].axes[JH_BLUETOOTH_GAMEPAD_AXIS_Y] = INT16_MIN + 1;
  snapshots[9].axes[JH_BLUETOOTH_GAMEPAD_AXIS_Y] = INT16_MAX;
  snapshots[10].axes[JH_BLUETOOTH_GAMEPAD_AXIS_X] = INT16_MIN + 1;
  snapshots[11].axes[JH_BLUETOOTH_GAMEPAD_AXIS_X] = INT16_MAX;

  jh_bluetooth_classic_hid_probe_logic_connected(&s_logic);
  for (const auto &snapshot : snapshots) {
    TEST_ASSERT_NOT_EQUAL(0u, jh_bluetooth_classic_hid_probe_logic_report(
                                  &s_logic, &snapshot, 12u));
  }
  TEST_ASSERT_EQUAL_HEX16(JH_CLASSIC_HID_ALL_CONTROLS_MASK,
                          s_logic.seen_controls_mask);
  TEST_ASSERT_EQUAL_UINT16(12u, s_logic.report_length_high_water);
  TEST_ASSERT_NOT_EQUAL(0u, s_logic.active_controls_mask);

  jh_bluetooth_classic_hid_probe_logic_disconnected(&s_logic);
  TEST_ASSERT_EQUAL_HEX16(0u, s_logic.active_controls_mask);
  TEST_ASSERT_EQUAL_UINT32(1u, s_logic.release_all_events);
}

void test_bond_ready_requires_all_four_conditions() {
  static const uint8_t addr[6] = {1, 2, 3, 4, 5, 6};
  static const uint8_t key[16] = {0xAu, 0xBu, 0xCu, 0xDu, 0xEu, 0xFu, 1u, 2u,
                                  3u,   4u,   5u,   6u,   7u,   8u,   9u, 10u};
  jh_bluetooth_gamepad_snapshot_t snapshot{};
  snapshot.connected = true;

  TEST_ASSERT_FALSE(jh_bluetooth_classic_hid_probe_logic_bond_ready(&s_logic));

  jh_bluetooth_classic_hid_probe_logic_connected(&s_logic);
  jh_bluetooth_classic_hid_probe_logic_identity_validated(&s_logic);
  TEST_ASSERT_FALSE(jh_bluetooth_classic_hid_probe_logic_bond_ready(&s_logic));

  jh_bluetooth_classic_hid_probe_logic_descriptor_accepted(&s_logic);
  TEST_ASSERT_FALSE(jh_bluetooth_classic_hid_probe_logic_bond_ready(&s_logic));

  jh_bluetooth_classic_hid_probe_logic_link_key_received(&s_logic, addr, key,
                                                         4u);
  /* No report received yet -- the input gate still blocks. */
  TEST_ASSERT_FALSE(jh_bluetooth_classic_hid_probe_logic_bond_ready(&s_logic));

  /* The input gate only requires a report to arrive, not any particular
   * button state. */
  (void)jh_bluetooth_classic_hid_probe_logic_report(&s_logic, &snapshot, 12u);
  TEST_ASSERT_TRUE(jh_bluetooth_classic_hid_probe_logic_bond_ready(&s_logic));

  const jh_gamepad_bond_identity_t *identity =
      jh_bluetooth_classic_hid_probe_logic_take_pending_bond(&s_logic);
  TEST_ASSERT_NOT_NULL(identity);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(addr, identity->bd_addr, sizeof(addr));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(key, identity->link_key, sizeof(key));
  TEST_ASSERT_EQUAL_UINT8(4u, identity->link_key_type);

  /* Consumed: does not fire again until a new link key arrives. */
  TEST_ASSERT_FALSE(jh_bluetooth_classic_hid_probe_logic_bond_ready(&s_logic));
  TEST_ASSERT_NULL(
      jh_bluetooth_classic_hid_probe_logic_take_pending_bond(&s_logic));
}

void test_reconnect_without_new_link_key_does_not_resignal_bond() {
  static const uint8_t addr[6] = {1, 2, 3, 4, 5, 6};
  static const uint8_t key[16] = {0};
  jh_bluetooth_gamepad_snapshot_t snapshot{};
  snapshot.connected = true;

  jh_bluetooth_classic_hid_probe_logic_connected(&s_logic);
  jh_bluetooth_classic_hid_probe_logic_identity_validated(&s_logic);
  jh_bluetooth_classic_hid_probe_logic_descriptor_accepted(&s_logic);
  jh_bluetooth_classic_hid_probe_logic_link_key_received(&s_logic, addr, key,
                                                         0u);
  jh_bluetooth_classic_hid_probe_logic_report(&s_logic, &snapshot, 12u);
  TEST_ASSERT_NOT_NULL(
      jh_bluetooth_classic_hid_probe_logic_take_pending_bond(&s_logic));

  jh_bluetooth_classic_hid_probe_logic_disconnected(&s_logic);
  /* Reconnect of the same already-validated peer: identity persists, but a
   * fresh connection needs its own descriptor accept + report before
   * signalling bond-ready again -- and without a new link-key notification
   * (the normal case for a plain reconnect) it must never re-fire. */
  jh_bluetooth_classic_hid_probe_logic_connected(&s_logic);
  TEST_ASSERT_TRUE(s_logic.identity_validated);
  jh_bluetooth_classic_hid_probe_logic_descriptor_accepted(&s_logic);
  jh_bluetooth_classic_hid_probe_logic_report(&s_logic, &snapshot, 12u);
  TEST_ASSERT_FALSE(jh_bluetooth_classic_hid_probe_logic_bond_ready(&s_logic));
}

void test_reset_bond_progress_clears_identity_too() {
  static const uint8_t addr[6] = {1, 2, 3, 4, 5, 6};
  static const uint8_t key[16] = {0};
  jh_bluetooth_classic_hid_probe_logic_connected(&s_logic);
  jh_bluetooth_classic_hid_probe_logic_identity_validated(&s_logic);
  jh_bluetooth_classic_hid_probe_logic_link_key_received(&s_logic, addr, key,
                                                         0u);
  jh_bluetooth_classic_hid_probe_logic_reset_bond_progress(&s_logic);
  TEST_ASSERT_FALSE(s_logic.identity_validated);
  TEST_ASSERT_FALSE(s_logic.link_key_received);
  TEST_ASSERT_FALSE(jh_bluetooth_classic_hid_probe_logic_bond_ready(&s_logic));
}

void test_link_key_received_ignores_null_pointers() {
  jh_bluetooth_classic_hid_probe_logic_link_key_received(&s_logic, nullptr,
                                                         nullptr, 0u);
  TEST_ASSERT_FALSE(s_logic.link_key_received);
}

} // namespace

void setUp() { std::memset(&s_logic, 0, sizeof(s_logic)); }

void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_discovery_requires_explicit_open_and_expires_after_120_seconds);
  RUN_TEST(test_discovery_deadline_handles_millisecond_wrap);
  RUN_TEST(test_candidate_filter_requires_peripheral_class_and_exact_name);
  RUN_TEST(test_pnp_filter_requires_the_characterized_identity);
  RUN_TEST(
      test_reports_cover_all_controls_and_disconnect_releases_active_state);
  RUN_TEST(test_bond_ready_requires_all_four_conditions);
  RUN_TEST(test_reconnect_without_new_link_key_does_not_resignal_bond);
  RUN_TEST(test_reset_bond_progress_clears_identity_too);
  RUN_TEST(test_link_key_received_ignores_null_pointers);
  return UNITY_END();
}
