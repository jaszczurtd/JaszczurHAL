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
  static constexpr std::array<std::array<uint8_t, 12>, 12> kReports = {{
      {0xa1u, 0x03u, 0x01u, 0x00u, 0x0fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x88u,
       0x01u, 0x00u},
      {0xa1u, 0x03u, 0x02u, 0x00u, 0x0fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x88u,
       0x01u, 0x00u},
      {0xa1u, 0x03u, 0x08u, 0x00u, 0x0fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x87u,
       0x01u, 0x00u},
      {0xa1u, 0x03u, 0x10u, 0x00u, 0x0fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x88u,
       0x01u, 0x00u},
      {0xa1u, 0x03u, 0x40u, 0x00u, 0x0fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x88u,
       0x01u, 0x00u},
      {0xa1u, 0x03u, 0x80u, 0x00u, 0x0fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x88u,
       0x01u, 0x00u},
      {0xa1u, 0x03u, 0x00u, 0x04u, 0x0fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x88u,
       0x01u, 0x00u},
      {0xa1u, 0x03u, 0x00u, 0x08u, 0x0fu, 0x7fu, 0x7fu, 0x7fu, 0x7fu, 0x88u,
       0x01u, 0x00u},
      {0xa1u, 0x03u, 0x00u, 0x00u, 0x0fu, 0x7fu, 0x00u, 0x7fu, 0x7fu, 0x88u,
       0x01u, 0x00u},
      {0xa1u, 0x03u, 0x00u, 0x00u, 0x0fu, 0x7fu, 0xffu, 0x7fu, 0x7fu, 0x88u,
       0x01u, 0x00u},
      {0xa1u, 0x03u, 0x00u, 0x00u, 0x0fu, 0x00u, 0x7fu, 0x7fu, 0x7fu, 0x87u,
       0x01u, 0x00u},
      {0xa1u, 0x03u, 0x00u, 0x00u, 0x0fu, 0xffu, 0x7fu, 0x7fu, 0x7fu, 0x87u,
       0x01u, 0x00u},
  }};

  jh_bluetooth_classic_hid_probe_logic_connected(&s_logic);
  for (const auto &report : kReports) {
    TEST_ASSERT_NOT_EQUAL(0u, jh_bluetooth_classic_hid_probe_logic_report(
                                  &s_logic, report.data(), report.size()));
  }
  TEST_ASSERT_EQUAL_HEX16(JH_CLASSIC_HID_ALL_CONTROLS_MASK,
                          s_logic.seen_controls_mask);
  TEST_ASSERT_EQUAL_UINT16(kReports.front().size(),
                           s_logic.report_length_high_water);
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
