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
  return UNITY_END();
}
