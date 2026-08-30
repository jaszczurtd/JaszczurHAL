#include "hal/bluetooth/jh_bluetooth_gamepad_parser.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "utils/unity.h"

#ifndef JH_ZERO2_FIXTURE_PATH
#error "JH_ZERO2_FIXTURE_PATH must name the characterized Zero 2 fixture"
#endif

namespace {

jh_bluetooth_gamepad_parser_t s_parser;
std::string s_fixture;
std::vector<uint8_t> s_descriptor;
uint32_t s_tracked_allocations;
bool s_track_allocations;

std::string extract_json_string(const std::string &document,
                                const std::string &key, size_t from = 0u) {
  const size_t key_position = document.find(key, from);
  if (key_position == std::string::npos) {
    return {};
  }
  const size_t value_start = document.find('"', key_position + key.size());
  if (value_start == std::string::npos) {
    return {};
  }
  const size_t value_end = document.find('"', value_start + 1u);
  if (value_end == std::string::npos) {
    return {};
  }
  return document.substr(value_start + 1u, value_end - value_start - 1u);
}

std::vector<uint8_t> decode_hex(const std::string &hex) {
  std::istringstream stream(hex);
  std::vector<uint8_t> bytes;
  unsigned value = 0u;
  while (stream >> std::hex >> value) {
    bytes.push_back(static_cast<uint8_t>(value));
  }
  return bytes;
}

std::vector<uint8_t> fixture_report(const char *physical) {
  const std::string marker = std::string("\"physical\": \"") + physical + "\"";
  const size_t control = s_fixture.find(marker);
  return decode_hex(extract_json_string(s_fixture, "\"pressed\":", control));
}

std::vector<uint8_t> raw_report(const char *state) {
  const std::string marker = std::string("\"state\": \"") + state + "\"";
  const size_t record = s_fixture.find(marker);
  return decode_hex(extract_json_string(s_fixture, "\"report\":", record));
}

jh_bluetooth_gamepad_snapshot_t pop_snapshot() {
  jh_bluetooth_gamepad_snapshot_t snapshot{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_bluetooth_gamepad_parser_next(&s_parser, &snapshot));
  return snapshot;
}

void configure_zero2() {
  TEST_ASSERT_EQUAL_UINT32(136u, s_descriptor.size());
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_gamepad_parser_configure(
                  &s_parser, s_descriptor.data(), s_descriptor.size()));
}

void connect_zero2() {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_gamepad_parser_connection_opened(&s_parser));
  const auto connected = pop_snapshot();
  TEST_ASSERT_TRUE(connected.connected);
}

void test_fixture_descriptor_freezes_c6_limits_and_layout() {
  configure_zero2();

  TEST_ASSERT_EQUAL_UINT32(256u, JH_BLUETOOTH_GAMEPAD_DESCRIPTOR_MAX);
  TEST_ASSERT_EQUAL_UINT32(32u, JH_BLUETOOTH_GAMEPAD_REPORT_MAX);
  TEST_ASSERT_EQUAL_UINT32(16u, JH_BLUETOOTH_GAMEPAD_QUEUE_CAPACITY);
  TEST_ASSERT_EQUAL_UINT8(20u, s_parser.field_count);
  TEST_ASSERT_EQUAL_UINT8(2u, s_parser.report_layout_count);

  uint8_t report3_bytes = 0u;
  uint8_t report5_bytes = 0u;
  for (uint8_t index = 0u; index < s_parser.report_layout_count; ++index) {
    const auto &layout = s_parser.report_layouts[index];
    if (layout.report_id == 0x03u) {
      report3_bytes = layout.required_bytes;
    } else if (layout.report_id == 0x05u) {
      report5_bytes = layout.required_bytes;
    }
  }
  TEST_ASSERT_EQUAL_UINT8(10u, report3_bytes);
  TEST_ASSERT_EQUAL_UINT8(21u, report5_bytes);

  jh_bluetooth_gamepad_parser_diagnostics_t diagnostics{};
  jh_bluetooth_gamepad_parser_diagnostics(&s_parser, &diagnostics);
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.descriptors_accepted);
  TEST_ASSERT_EQUAL_UINT16(136u, diagnostics.descriptor_length_high_water);
}

void test_fixture_reports_normalize_buttons_and_axes() {
  configure_zero2();
  connect_zero2();

  struct ButtonCase {
    const char *physical;
    uint32_t mask;
  };
  static constexpr std::array<ButtonCase, 8> kButtons = {{
      {"A", UINT32_C(1) << 0u},
      {"B", UINT32_C(1) << 1u},
      {"X", UINT32_C(1) << 3u},
      {"Y", UINT32_C(1) << 4u},
      {"L", UINT32_C(1) << 6u},
      {"R", UINT32_C(1) << 7u},
      {"Select", UINT32_C(1) << 10u},
      {"Start", UINT32_C(1) << 11u},
  }};
  for (const auto &button : kButtons) {
    const auto report = fixture_report(button.physical);
    TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_gamepad_parser_parse_input(
                                      &s_parser, report.data(), report.size()));
    const auto snapshot = pop_snapshot();
    TEST_ASSERT_EQUAL_HEX32(button.mask, snapshot.buttons);
    TEST_ASSERT_EQUAL_UINT8(JH_BLUETOOTH_GAMEPAD_DPAD_NONE, snapshot.dpad);
  }

  struct AxisCase {
    const char *physical;
    jh_bluetooth_gamepad_axis_t axis;
    int16_t expected;
  };
  static constexpr std::array<AxisCase, 4> kAxes = {{
      {"Up", JH_BLUETOOTH_GAMEPAD_AXIS_Y, INT16_MIN + 1},
      {"Down", JH_BLUETOOTH_GAMEPAD_AXIS_Y, INT16_MAX},
      {"Left", JH_BLUETOOTH_GAMEPAD_AXIS_X, INT16_MIN + 1},
      {"Right", JH_BLUETOOTH_GAMEPAD_AXIS_X, INT16_MAX},
  }};
  for (const auto &axis : kAxes) {
    const auto report = fixture_report(axis.physical);
    TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_gamepad_parser_parse_input(
                                      &s_parser, report.data(), report.size()));
    const auto snapshot = pop_snapshot();
    TEST_ASSERT_EQUAL_INT16(axis.expected, snapshot.axes[axis.axis]);
    TEST_ASSERT_BITS_HIGH((uint16_t)(1u << axis.axis), snapshot.axes_present);
  }
}

void test_repeated_report_is_idempotent() {
  configure_zero2();
  connect_zero2();
  const auto repeated = raw_report("Y pressed repeated");

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_bluetooth_gamepad_parser_parse_input(
                            &s_parser, repeated.data(), repeated.size()));
  (void)pop_snapshot();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_bluetooth_gamepad_parser_parse_input(
                            &s_parser, repeated.data(), repeated.size()));
  jh_bluetooth_gamepad_snapshot_t snapshot{};
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        jh_bluetooth_gamepad_parser_next(&s_parser, &snapshot));

  jh_bluetooth_gamepad_parser_diagnostics_t diagnostics{};
  jh_bluetooth_gamepad_parser_diagnostics(&s_parser, &diagnostics);
  TEST_ASSERT_EQUAL_UINT32(2u, diagnostics.reports_accepted);
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.duplicate_reports);
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.state_changes);
}

void test_hat_switch_normalizes_all_directions_and_null_state() {
  static constexpr std::array<uint8_t, 31> kHatDescriptor = {
      0x05u, 0x01u, 0x09u, 0x05u, 0xa1u, 0x01u, 0x85u, 0x01u,
      0x15u, 0x00u, 0x25u, 0x07u, 0x35u, 0x00u, 0x46u, 0x3bu,
      0x01u, 0x75u, 0x04u, 0x95u, 0x01u, 0x65u, 0x14u, 0x09u,
      0x39u, 0x81u, 0x42u, 0x75u, 0x04u, 0x81u, 0x03u,
  };
  std::array<uint8_t, kHatDescriptor.size() + 1u> descriptor{};
  std::copy(kHatDescriptor.begin(), kHatDescriptor.end(), descriptor.begin());
  descriptor.back() = 0xc0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_bluetooth_gamepad_parser_configure(
                            &s_parser, descriptor.data(), descriptor.size()));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_gamepad_parser_connection_opened(&s_parser));
  (void)pop_snapshot();

  const std::array<uint8_t, 2> up = {0x01u, 0x00u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_gamepad_parser_parse_input(
                                    &s_parser, up.data(), up.size()));
  TEST_ASSERT_EQUAL_UINT8(JH_BLUETOOTH_GAMEPAD_DPAD_UP, pop_snapshot().dpad);

  const std::array<uint8_t, 2> down_left = {0x01u, 0x05u};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_bluetooth_gamepad_parser_parse_input(
                            &s_parser, down_left.data(), down_left.size()));
  TEST_ASSERT_EQUAL_UINT8(JH_BLUETOOTH_GAMEPAD_DPAD_DOWN |
                              JH_BLUETOOTH_GAMEPAD_DPAD_LEFT,
                          pop_snapshot().dpad);

  const std::array<uint8_t, 2> neutral = {0x01u, 0x0fu};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_gamepad_parser_parse_input(
                                    &s_parser, neutral.data(), neutral.size()));
  TEST_ASSERT_EQUAL_UINT8(JH_BLUETOOTH_GAMEPAD_DPAD_NONE, pop_snapshot().dpad);
}

void test_unknown_usage_is_ignored_without_rejecting_descriptor() {
  static constexpr std::array<uint8_t, 40> kDescriptor = {
      0x05u, 0x01u, 0x09u, 0x05u, 0xa1u, 0x01u, 0x85u, 0x01u, 0x15u, 0x00u,
      0x25u, 0x01u, 0x75u, 0x01u, 0x95u, 0x01u, 0x05u, 0x09u, 0x09u, 0x01u,
      0x81u, 0x02u, 0x75u, 0x07u, 0x95u, 0x01u, 0x81u, 0x03u, 0x06u, 0x00u,
      0xffu, 0x09u, 0x01u, 0x26u, 0xffu, 0x00u, 0x75u, 0x08u, 0x81u, 0x02u,
  };
  std::array<uint8_t, kDescriptor.size() + 1u> descriptor{};
  std::copy(kDescriptor.begin(), kDescriptor.end(), descriptor.begin());
  descriptor.back() = 0xc0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_bluetooth_gamepad_parser_configure(
                            &s_parser, descriptor.data(), descriptor.size()));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_gamepad_parser_connection_opened(&s_parser));
  (void)pop_snapshot();
  const std::array<uint8_t, 3> report = {0x01u, 0x01u, 0xa5u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_gamepad_parser_parse_input(
                                    &s_parser, report.data(), report.size()));
  TEST_ASSERT_EQUAL_HEX32(1u, pop_snapshot().buttons);

  jh_bluetooth_gamepad_parser_diagnostics_t diagnostics{};
  jh_bluetooth_gamepad_parser_diagnostics(&s_parser, &diagnostics);
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.ignored_usages);
}

void test_descriptor_rejects_malformed_oversize_and_duplicate_usage() {
  auto truncated = s_descriptor;
  truncated.pop_back();
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_bluetooth_gamepad_parser_configure(
                            &s_parser, truncated.data(), truncated.size()));
  TEST_ASSERT_EQUAL_INT(JH_BLUETOOTH_GAMEPAD_REJECT_DESCRIPTOR_MALFORMED,
                        s_parser.diagnostics.last_reject_reason);

  std::array<uint8_t, JH_BLUETOOTH_GAMEPAD_DESCRIPTOR_MAX + 1u> oversize{};
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        jh_bluetooth_gamepad_parser_configure(
                            &s_parser, oversize.data(), oversize.size()));
  TEST_ASSERT_EQUAL_INT(JH_BLUETOOTH_GAMEPAD_REJECT_DESCRIPTOR_TOO_LARGE,
                        s_parser.diagnostics.last_reject_reason);

  static constexpr std::array<uint8_t, 25> kDuplicateUsage = {
      0x05u, 0x01u, 0x09u, 0x05u, 0xa1u, 0x01u, 0x85u, 0x01u, 0x15u,
      0x00u, 0x26u, 0xffu, 0x00u, 0x75u, 0x08u, 0x95u, 0x02u, 0x09u,
      0x30u, 0x09u, 0x30u, 0x81u, 0x02u, 0xc0u, 0x00u,
  };
  TEST_ASSERT_EQUAL_INT(HAL_EEXIST, jh_bluetooth_gamepad_parser_configure(
                                        &s_parser, kDuplicateUsage.data(),
                                        kDuplicateUsage.size() - 1u));
  TEST_ASSERT_EQUAL_INT(JH_BLUETOOTH_GAMEPAD_REJECT_DUPLICATE_USAGE,
                        s_parser.diagnostics.last_reject_reason);
}

void test_reports_reject_truncation_unknown_id_and_oversize_without_state_change() {
  configure_zero2();
  connect_zero2();
  const auto initial = fixture_report("A");
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_gamepad_parser_parse_input(
                                    &s_parser, initial.data(), initial.size()));
  const auto accepted = pop_snapshot();

  auto truncated = initial;
  truncated.resize(9u);
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_bluetooth_gamepad_parser_parse_input(
                            &s_parser, truncated.data(), truncated.size()));
  TEST_ASSERT_EQUAL_INT(JH_BLUETOOTH_GAMEPAD_REJECT_REPORT_TOO_SHORT,
                        s_parser.diagnostics.last_reject_reason);

  auto unknown = initial;
  unknown[0] = 0x7fu;
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        jh_bluetooth_gamepad_parser_parse_input(
                            &s_parser, unknown.data(), unknown.size()));
  TEST_ASSERT_EQUAL_INT(JH_BLUETOOTH_GAMEPAD_REJECT_UNKNOWN_REPORT_ID,
                        s_parser.diagnostics.last_reject_reason);

  std::array<uint8_t, JH_BLUETOOTH_GAMEPAD_REPORT_MAX + 1u> oversize{};
  oversize[0] = 0x03u;
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        jh_bluetooth_gamepad_parser_parse_input(
                            &s_parser, oversize.data(), oversize.size()));
  TEST_ASSERT_EQUAL_INT(JH_BLUETOOTH_GAMEPAD_REJECT_REPORT_TOO_LARGE,
                        s_parser.diagnostics.last_reject_reason);

  jh_bluetooth_gamepad_snapshot_t current{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_gamepad_parser_snapshot(&s_parser, &current));
  TEST_ASSERT_EQUAL_HEX32(accepted.buttons, current.buttons);
  TEST_ASSERT_EQUAL_UINT32(3u, s_parser.diagnostics.reports_rejected);
}

void test_disconnect_discards_partial_report_before_new_generation() {
  configure_zero2();
  connect_zero2();
  const auto pressed = fixture_report("A");
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO, jh_bluetooth_gamepad_parser_parse_input(
                                        &s_parser, pressed.data(), 4u));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_gamepad_parser_connection_closed(&s_parser));
  auto disconnected = pop_snapshot();
  TEST_ASSERT_FALSE(disconnected.connected);
  TEST_ASSERT_EQUAL_HEX32(0u, disconnected.buttons);

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_gamepad_parser_connection_opened(&s_parser));
  const auto reconnected = pop_snapshot();
  TEST_ASSERT_TRUE(reconnected.connected);
  TEST_ASSERT_GREATER_THAN_UINT32(disconnected.generation,
                                  reconnected.generation);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        jh_bluetooth_gamepad_parser_parse_input(
                            &s_parser, &pressed[4], pressed.size() - 4u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_gamepad_parser_parse_input(
                                    &s_parser, pressed.data(), pressed.size()));
  TEST_ASSERT_EQUAL_HEX32(1u, pop_snapshot().buttons);
}

void test_reconnect_clears_axis_presence_and_state() {
  configure_zero2();
  connect_zero2();
  const auto pressed = fixture_report("Right");
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_gamepad_parser_parse_input(
                                    &s_parser, pressed.data(), pressed.size()));
  const auto active = pop_snapshot();
  TEST_ASSERT_BITS_HIGH((uint16_t)(1u << JH_BLUETOOTH_GAMEPAD_AXIS_X),
                        active.axes_present);
  TEST_ASSERT_EQUAL_INT16(INT16_MAX, active.axes[JH_BLUETOOTH_GAMEPAD_AXIS_X]);

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_gamepad_parser_connection_closed(&s_parser));
  const auto disconnected = pop_snapshot();
  TEST_ASSERT_EQUAL_UINT16(0u, disconnected.axes_present);
  TEST_ASSERT_EQUAL_INT16(0, disconnected.axes[JH_BLUETOOTH_GAMEPAD_AXIS_X]);

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_gamepad_parser_connection_opened(&s_parser));
  const auto reconnected = pop_snapshot();
  TEST_ASSERT_EQUAL_UINT16(0u, reconnected.axes_present);
  TEST_ASSERT_EQUAL_INT16(0, reconnected.axes[JH_BLUETOOTH_GAMEPAD_AXIS_X]);
}

void test_queue_overflow_is_reported_and_retains_latest_snapshot() {
  configure_zero2();
  connect_zero2();
  const auto pressed = fixture_report("A");
  const auto neutral = raw_report("neutral");
  for (size_t index = 0u; index < 17u; ++index) {
    const auto &report = (index & 1u) == 0u ? pressed : neutral;
    TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_gamepad_parser_parse_input(
                                      &s_parser, report.data(), report.size()));
  }

  jh_bluetooth_gamepad_snapshot_t snapshot{};
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        jh_bluetooth_gamepad_parser_next(&s_parser, &snapshot));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_bluetooth_gamepad_parser_next(&s_parser, &snapshot));
  TEST_ASSERT_EQUAL_HEX32(1u, snapshot.buttons);
  TEST_ASSERT_EQUAL_UINT32(JH_BLUETOOTH_GAMEPAD_QUEUE_CAPACITY,
                           s_parser.diagnostics.dropped_snapshots);
  TEST_ASSERT_EQUAL_UINT8(JH_BLUETOOTH_GAMEPAD_QUEUE_CAPACITY,
                          s_parser.diagnostics.queue_high_water);

  configure_zero2();
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_gamepad_parser_connection_opened(&s_parser));
  for (size_t index = 0u; index < 15u; ++index) {
    const auto &report = (index & 1u) == 0u ? pressed : neutral;
    TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bluetooth_gamepad_parser_parse_input(
                                      &s_parser, report.data(), report.size()));
  }
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_gamepad_parser_connection_closed(&s_parser));
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        jh_bluetooth_gamepad_parser_next(&s_parser, &snapshot));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_bluetooth_gamepad_parser_next(&s_parser, &snapshot));
  TEST_ASSERT_FALSE(snapshot.connected);
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, s_parser.diagnostics.last_status);
  TEST_ASSERT_EQUAL_INT(JH_BLUETOOTH_GAMEPAD_REJECT_QUEUE_OVERFLOW,
                        s_parser.diagnostics.last_reject_reason);
}

void test_parser_performs_no_dynamic_allocation_after_init() {
  jh_bluetooth_gamepad_snapshot_t snapshot{};
  const auto report = fixture_report("A");
  s_tracked_allocations = 0u;
  s_track_allocations = true;
  const hal_status_t configure_status = jh_bluetooth_gamepad_parser_configure(
      &s_parser, s_descriptor.data(), s_descriptor.size());
  const hal_status_t connect_status =
      jh_bluetooth_gamepad_parser_connection_opened(&s_parser);
  const hal_status_t parse_status = jh_bluetooth_gamepad_parser_parse_input(
      &s_parser, report.data(), report.size());
  const hal_status_t snapshot_status =
      jh_bluetooth_gamepad_parser_snapshot(&s_parser, &snapshot);
  const hal_status_t next_status =
      jh_bluetooth_gamepad_parser_next(&s_parser, &snapshot);
  const hal_status_t disconnect_status =
      jh_bluetooth_gamepad_parser_connection_closed(&s_parser);
  s_track_allocations = false;

  TEST_ASSERT_EQUAL_INT(HAL_OK, configure_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, connect_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, parse_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, snapshot_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, next_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, disconnect_status);
  TEST_ASSERT_EQUAL_UINT32(0u, s_tracked_allocations);
}

} // namespace

extern "C" void *__real_malloc(size_t size);
extern "C" void *__real_calloc(size_t count, size_t size);
extern "C" void *__real_realloc(void *pointer, size_t size);

extern "C" void *__wrap_malloc(size_t size) {
  if (s_track_allocations) {
    ++s_tracked_allocations;
  }
  return __real_malloc(size);
}

extern "C" void *__wrap_calloc(size_t count, size_t size) {
  if (s_track_allocations) {
    ++s_tracked_allocations;
  }
  return __real_calloc(count, size);
}

extern "C" void *__wrap_realloc(void *pointer, size_t size) {
  if (s_track_allocations) {
    ++s_tracked_allocations;
  }
  return __real_realloc(pointer, size);
}

void setUp() { jh_bluetooth_gamepad_parser_init(&s_parser); }

void tearDown() {}

int main() {
  static_assert(std::is_trivially_copyable_v<jh_bluetooth_gamepad_parser_t>);
  std::ifstream fixture_file(JH_ZERO2_FIXTURE_PATH);
  s_fixture.assign(std::istreambuf_iterator<char>(fixture_file),
                   std::istreambuf_iterator<char>());
  if (s_fixture.empty()) {
    return 2;
  }
  s_descriptor = decode_hex(extract_json_string(s_fixture, "\"bytes\":"));
  if (s_descriptor.size() != 137u) {
    return 3;
  }
  s_descriptor.resize(136u);

  UNITY_BEGIN();
  RUN_TEST(test_fixture_descriptor_freezes_c6_limits_and_layout);
  RUN_TEST(test_fixture_reports_normalize_buttons_and_axes);
  RUN_TEST(test_repeated_report_is_idempotent);
  RUN_TEST(test_hat_switch_normalizes_all_directions_and_null_state);
  RUN_TEST(test_unknown_usage_is_ignored_without_rejecting_descriptor);
  RUN_TEST(test_descriptor_rejects_malformed_oversize_and_duplicate_usage);
  RUN_TEST(
      test_reports_reject_truncation_unknown_id_and_oversize_without_state_change);
  RUN_TEST(test_disconnect_discards_partial_report_before_new_generation);
  RUN_TEST(test_reconnect_clears_axis_presence_and_state);
  RUN_TEST(test_queue_overflow_is_reported_and_retains_latest_snapshot);
  RUN_TEST(test_parser_performs_no_dynamic_allocation_after_init);
  return UNITY_END();
}
