#include "hal/impl/.mock/hal_mock.h"
#include "hal/network/jh_ntp_client.h"
#include "hal/rtc/hal_rtc.h"
#include "hal/time/hal_time.h"
#include "utils/unity.h"

#include <atomic>
#include <cstring>
#include <string.h>
#include <thread>

namespace {

constexpr uint64_t kNtpEpochOffset = UINT64_C(2208988800);

void make_ntp_response(uint8_t response[JH_NTP_PACKET_SIZE], uint64_t unix_time,
                       uint32_t fraction) {
  const uint8_t *request = hal_mock_udp_get_last_tx_payload();
  TEST_ASSERT_NOT_NULL(request);
  TEST_ASSERT_EQUAL_UINT16(JH_NTP_PACKET_SIZE, hal_mock_udp_get_last_tx_len());
  memset(response, 0, JH_NTP_PACKET_SIZE);
  response[0] = 0x24u;
  response[1] = 2u;
  memcpy(&response[24], &request[40], 8u);
  const uint32_t ntp_seconds =
      static_cast<uint32_t>(unix_time + kNtpEpochOffset);
  response[40] = static_cast<uint8_t>(ntp_seconds >> 24u);
  response[41] = static_cast<uint8_t>(ntp_seconds >> 16u);
  response[42] = static_cast<uint8_t>(ntp_seconds >> 8u);
  response[43] = static_cast<uint8_t>(ntp_seconds);
  response[44] = static_cast<uint8_t>(fraction >> 24u);
  response[45] = static_cast<uint8_t>(fraction >> 16u);
  response[46] = static_cast<uint8_t>(fraction >> 8u);
  response[47] = static_cast<uint8_t>(fraction);
}

void reentrant_time_read(void *ctx) {
  bool *called = static_cast<bool *>(ctx);
  *called = true;
  (void)hal_time_unix();
}

hal_rtc_t make_internal_rtc(void) {
  const hal_rtc_config_t config = {
      .chip = HAL_RTC_CHIP_INTERNAL,
      .bus = {.internal = {.clock_source = HAL_RTC_CLOCK_SOURCE_AUTO}},
  };
  hal_rtc_t rtc = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_init_ex(&config, &rtc));
  TEST_ASSERT_NOT_NULL(rtc);
  return rtc;
}

} // namespace

void setUp(void) {
  hal_mock_time_reset();
  hal_mock_udp_reset();
  hal_mock_net_reset();
  hal_mock_serial_reset();
  hal_mock_set_millis(0u);
  TEST_ASSERT_TRUE(hal_mock_net_set_dns_entry("pool.ntp.org", "192.0.2.10"));
  TEST_ASSERT_TRUE(hal_mock_net_set_dns_entry("time.nist.gov", "192.0.2.20"));
}

void tearDown(void) {
  hal_mock_time_reset();
  hal_mock_debug_serial_full_reset();
}

void test_timezone_and_ntp_sync_requests_are_recorded(void) {
  TEST_ASSERT_TRUE(hal_time_set_timezone("CET-1CEST,M3.5.0/2,M10.5.0/3"));
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0/2,M10.5.0/3",
                           hal_mock_time_get_timezone());

  TEST_ASSERT_TRUE(hal_time_sync_ntp("pool.ntp.org", "time.nist.gov"));
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", hal_mock_time_get_ntp_primary());
  TEST_ASSERT_EQUAL_STRING("time.nist.gov", hal_mock_time_get_ntp_secondary());
}

void test_sync_check_and_formatting(void) {
  hal_mock_time_set_unix(200000);
  TEST_ASSERT_TRUE(hal_time_is_synced(172800));
  TEST_ASSERT_EQUAL_UINT64(200000, hal_time_unix());

  struct tm tm_local = {};
  tm_local.tm_year = 126; // 2026
  tm_local.tm_mon = 2;    // March
  tm_local.tm_mday = 30;
  tm_local.tm_hour = 12;
  tm_local.tm_min = 34;
  tm_local.tm_sec = 56;
  hal_mock_time_set_local(&tm_local);

  struct tm out = {};
  TEST_ASSERT_TRUE(hal_time_get_local(&out));
  TEST_ASSERT_EQUAL_INT(30, out.tm_mday);
  TEST_ASSERT_EQUAL_INT(34, out.tm_min);

  char buf[32] = {};
  TEST_ASSERT_TRUE(
      hal_time_format_local(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S"));
  TEST_ASSERT_EQUAL_STRING("30/03/2026 12:34:56", buf);
}

void test_shared_setter_reports_source_and_subseconds(void) {
  hal_mock_set_micros64(UINT64_C(1000000));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_time_set_unix_ex(UINT64_C(1000), UINT32_C(250000),
                                             HAL_TIME_SOURCE_MANUAL));

  hal_time_status_t status = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_get_status_ex(&status));
  TEST_ASSERT_TRUE(status.valid);
  TEST_ASSERT_EQUAL_INT(HAL_TIME_SOURCE_MANUAL, status.source);
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(1000), status.unix_time);
  TEST_ASSERT_EQUAL_UINT32(UINT32_C(250000), status.micros);
  TEST_ASSERT_EQUAL_INT(HAL_TIME_NTP_IDLE, status.ntp_state);
  TEST_ASSERT_EQUAL_INT(HAL_NONE, status.last_ntp_status);
  TEST_ASSERT_FALSE(status.rtc_attached);
  TEST_ASSERT_EQUAL_INT(HAL_NONE, status.last_rtc_status);

  hal_mock_advance_micros64(UINT64_C(750000));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_get_status_ex(&status));
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(1001), status.unix_time);
  TEST_ASSERT_EQUAL_UINT32(0u, status.micros);
}

void test_wall_clock_uses_64_bit_monotonic_base_across_millis_wrap(void) {
  hal_mock_set_micros64(0u);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_time_set_unix_ex(UINT64_C(10), 0u, HAL_TIME_SOURCE_MANUAL));
  const uint64_t elapsed =
      (UINT64_C(1) << 32u) * UINT64_C(1000) + UINT64_C(123456);
  hal_mock_advance_micros64(elapsed);

  hal_time_status_t status = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_get_status_ex(&status));
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(4294977), status.unix_time);
  TEST_ASSERT_EQUAL_UINT32(UINT32_C(419456), status.micros);
}

void test_ntp_state_machine_accepts_response_and_keeps_fraction(void) {
  TEST_ASSERT_TRUE(hal_time_sync_ntp("pool.ntp.org", "time.nist.gov"));
  TEST_ASSERT_EQUAL_STRING("192.0.2.10",
                           hal_mock_udp_get_last_begin_packet_host());

  uint8_t response[JH_NTP_PACKET_SIZE] = {};
  make_ntp_response(response, UINT64_C(200000), UINT32_C(0x80000000));
  hal_mock_udp_inject_packet("192.0.2.10", JH_NTP_PORT, response,
                             sizeof(response));
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(200000), hal_time_unix());

  hal_time_status_t status = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_get_status_ex(&status));
  TEST_ASSERT_EQUAL_INT(HAL_TIME_SOURCE_NTP, status.source);
  TEST_ASSERT_EQUAL_INT(HAL_TIME_NTP_SYNCHRONIZED, status.ntp_state);
  TEST_ASSERT_EQUAL_INT(HAL_OK, status.last_ntp_status);
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(200000), status.last_ntp_sync_unix);

  hal_mock_advance_millis(600u);
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(200001), hal_time_unix());
}

void test_ntp_status_distinguishes_pending_request_from_valid_clock(void) {
  hal_mock_time_set_unix(UINT64_C(100000));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_time_sync_ntp_ex("pool.ntp.org", "time.nist.gov"));

  hal_time_status_t status = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_get_status_ex(&status));
  TEST_ASSERT_TRUE(status.valid);
  TEST_ASSERT_EQUAL_INT(HAL_TIME_SOURCE_MANUAL, status.source);
  TEST_ASSERT_EQUAL_INT(HAL_TIME_NTP_IN_PROGRESS, status.ntp_state);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, status.last_ntp_status);
}

void test_ntp_status_reports_final_timeout(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_sync_ntp_ex("pool.ntp.org", nullptr));
  hal_mock_advance_millis(5000u);

  hal_time_status_t status = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_get_status_ex(&status));
  TEST_ASSERT_FALSE(status.valid);
  TEST_ASSERT_EQUAL_INT(HAL_TIME_NTP_FAILED, status.ntp_state);
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, status.last_ntp_status);
}

void test_valid_rtc_restores_unset_wall_clock(void) {
  hal_rtc_t rtc = make_internal_rtc();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_rtc_set_epoch_ex(rtc, UINT64_C(1760000000)));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_time_attach_rtc_ex(rtc, HAL_TIME_RTC_RESTORE_IF_VALID |
                                              HAL_TIME_RTC_WRITE_AFTER_NTP));

  hal_time_status_t status = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_get_status_ex(&status));
  TEST_ASSERT_TRUE(status.valid);
  TEST_ASSERT_EQUAL_INT(HAL_TIME_SOURCE_RTC, status.source);
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(1760000000), status.unix_time);
  TEST_ASSERT_TRUE(status.rtc_attached);
  TEST_ASSERT_EQUAL_INT(HAL_OK, status.last_rtc_status);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_detach_rtc_ex());
  hal_rtc_deinit(rtc);
}

void test_invalid_rtc_attaches_without_seeding_wall_clock(void) {
  hal_rtc_t rtc = make_internal_rtc();
  hal_mock_rtc_set_clock_integrity(rtc, false);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_time_attach_rtc_ex(rtc, HAL_TIME_RTC_RESTORE_IF_VALID |
                                              HAL_TIME_RTC_WRITE_AFTER_NTP));

  hal_time_status_t status = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_get_status_ex(&status));
  TEST_ASSERT_FALSE(status.valid);
  TEST_ASSERT_TRUE(status.rtc_attached);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, status.last_rtc_status);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_detach_rtc_ex());
  hal_rtc_deinit(rtc);
}

void test_rtc_restore_error_keeps_handle_attached_for_ntp_recovery(void) {
  hal_rtc_t rtc = make_internal_rtc();
  const hal_rtc_datetime_t pre_unix = {
      59u, 59u, 23u, 31u, 3u, 12u, 1969u, true,
  };
  hal_mock_rtc_set_datetime(rtc, &pre_unix);

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_time_attach_rtc_ex(rtc, HAL_TIME_RTC_RESTORE_IF_VALID |
                                              HAL_TIME_RTC_WRITE_AFTER_NTP));
  hal_time_status_t status = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_get_status_ex(&status));
  TEST_ASSERT_FALSE(status.valid);
  TEST_ASSERT_TRUE(status.rtc_attached);
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, status.last_rtc_status);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_sync_ntp_ex("pool.ntp.org", nullptr));
  uint8_t response[JH_NTP_PACKET_SIZE] = {};
  make_ntp_response(response, UINT64_C(450000), 0u);
  hal_mock_udp_inject_packet("192.0.2.10", JH_NTP_PORT, response,
                             sizeof(response));
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(450000), hal_time_unix());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_get_status_ex(&status));
  TEST_ASSERT_EQUAL_INT(HAL_OK, status.last_rtc_status);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_detach_rtc_ex());
  hal_rtc_deinit(rtc);
}

void test_successful_ntp_sync_is_written_to_attached_rtc(void) {
  hal_rtc_t rtc = make_internal_rtc();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_set_epoch_ex(rtc, UINT64_C(100000)));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_time_attach_rtc_ex(rtc, HAL_TIME_RTC_WRITE_AFTER_NTP));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_sync_ntp_ex("pool.ntp.org", nullptr));

  uint8_t response[JH_NTP_PACKET_SIZE] = {};
  make_ntp_response(response, UINT64_C(400000), 0u);
  hal_mock_udp_inject_packet("192.0.2.10", JH_NTP_PORT, response,
                             sizeof(response));
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(400000), hal_time_unix());

  uint64_t rtc_epoch = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_rtc_get_epoch_ex(rtc, &rtc_epoch));
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(400000), rtc_epoch);
  hal_time_status_t status = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_get_status_ex(&status));
  TEST_ASSERT_EQUAL_INT(HAL_OK, status.last_rtc_status);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_time_detach_rtc_ex());
  hal_rtc_deinit(rtc);
}

void test_ntp_timeout_retries_secondary_server(void) {
  TEST_ASSERT_TRUE(hal_time_sync_ntp("pool.ntp.org", "time.nist.gov"));
  hal_mock_advance_millis(5000u);
  TEST_ASSERT_EQUAL_UINT64(0u, hal_time_unix());
  TEST_ASSERT_EQUAL_STRING("192.0.2.20",
                           hal_mock_udp_get_last_begin_packet_host());

  uint8_t response[JH_NTP_PACKET_SIZE] = {};
  make_ntp_response(response, UINT64_C(300000), 0u);
  hal_mock_udp_inject_packet("192.0.2.20", JH_NTP_PORT, response,
                             sizeof(response));
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(300000), hal_time_unix());
}

void test_network_service_can_reenter_time_getter(void) {
  TEST_ASSERT_TRUE(hal_time_sync_ntp("pool.ntp.org", nullptr));
  bool callback_called = false;
  hal_mock_net_set_service_callback(reentrant_time_read, &callback_called);

  TEST_ASSERT_EQUAL_UINT64(0u, hal_time_unix());
  TEST_ASSERT_TRUE(callback_called);
}

void test_time_snapshots_are_safe_during_concurrent_updates(void) {
  constexpr uint64_t kFirst = UINT64_C(1000000);
  constexpr uint64_t kLast = kFirst + UINT64_C(1999);
  hal_mock_time_set_unix(kFirst);
  std::atomic<bool> start{false};
  std::atomic<bool> failed{false};

  std::thread writer([&]() {
    while (!start.load(std::memory_order_acquire)) {
    }
    for (uint64_t value = kFirst; value <= kLast; ++value) {
      hal_mock_time_set_unix(value);
    }
  });
  std::thread reader([&]() {
    start.store(true, std::memory_order_release);
    for (unsigned i = 0u; i < 4000u; ++i) {
      const uint64_t value = hal_time_unix();
      if (value < kFirst || value > kLast) {
        failed.store(true, std::memory_order_relaxed);
      }
    }
  });
  writer.join();
  reader.join();
  TEST_ASSERT_FALSE(failed.load(std::memory_order_relaxed));
}

void test_invalid_inputs_are_rejected(void) {
  TEST_ASSERT_FALSE(hal_time_is_synced(0u));
  TEST_ASSERT_FALSE(hal_time_set_timezone(NULL));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_time_sync_ntp(NULL, NULL));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      hal_time_set_unix_ex(0u, UINT32_C(1000000), HAL_TIME_SOURCE_MANUAL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_time_set_unix_ex(0u, 0u, HAL_TIME_SOURCE_UNSET));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_time_get_status_ex(nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_time_detach_rtc_ex());
}

void test_time_from_components_epoch_base(void) {
  TEST_ASSERT_EQUAL_UINT32(0u, hal_time_from_components(1970, 1, 1, 0, 0, 0));
}

void test_time_from_components_leap_day(void) {
  // 2024-02-29 12:00:00 UTC
  TEST_ASSERT_EQUAL_UINT32(1709208000u,
                           hal_time_from_components(2024, 2, 29, 12, 0, 0));
}

void test_time_from_components_invalid_values(void) {
  TEST_ASSERT_EQUAL_UINT32(0u,
                           hal_time_from_components(1969, 12, 31, 23, 59, 59));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_time_from_components(2024, 2, 30, 0, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_time_from_components(2023, 2, 29, 0, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_time_from_components(2026, 4, 31, 0, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_time_from_components(2024, 13, 1, 0, 0, 0));
}

void test_time_from_components_rejects_uint32_epoch_overflow(void) {
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,
                           hal_time_from_components(2106, 2, 7, 6, 28, 15));
  TEST_ASSERT_EQUAL_UINT32(0u, hal_time_from_components(2106, 2, 7, 6, 28, 16));
}

void test_daylight_saving_interval_uses_last_sundays(void) {
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(2024, 3, 30));
  TEST_ASSERT_TRUE(hal_time_is_daylight_saving_time(2024, 3, 31));
  TEST_ASSERT_TRUE(hal_time_is_daylight_saving_time(2024, 10, 26));
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(2024, 10, 27));
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(2024, 1, 15));
  TEST_ASSERT_TRUE(hal_time_is_daylight_saving_time(2024, 7, 15));
}

void test_daylight_saving_rejects_invalid_dates(void) {
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(0, 3, 31));
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(2024, 13, 1));
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(2024, 4, 31));
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(2023, 2, 29));
}

void test_cet_cest_adjustment_applies_offsets_and_rollovers(void) {
  int year = 2024;
  int month = 7;
  int day = 15;
  int hour = 23;
  int minute = 42;
  hal_time_adjust_cet_cest(&year, &month, &day, &hour, &minute);
  TEST_ASSERT_EQUAL_INT(2024, year);
  TEST_ASSERT_EQUAL_INT(7, month);
  TEST_ASSERT_EQUAL_INT(16, day);
  TEST_ASSERT_EQUAL_INT(1, hour);
  TEST_ASSERT_EQUAL_INT(42, minute);

  year = 2024;
  month = 2;
  day = 29;
  hour = 23;
  minute = 59;
  hal_time_adjust_cet_cest(&year, &month, &day, &hour, &minute);
  TEST_ASSERT_EQUAL_INT(2024, year);
  TEST_ASSERT_EQUAL_INT(3, month);
  TEST_ASSERT_EQUAL_INT(1, day);
  TEST_ASSERT_EQUAL_INT(0, hour);
  TEST_ASSERT_EQUAL_INT(59, minute);

  year = 2023;
  month = 12;
  day = 31;
  hour = 23;
  minute = 0;
  hal_time_adjust_cet_cest(&year, &month, &day, &hour, &minute);
  TEST_ASSERT_EQUAL_INT(2024, year);
  TEST_ASSERT_EQUAL_INT(1, month);
  TEST_ASSERT_EQUAL_INT(1, day);
  TEST_ASSERT_EQUAL_INT(0, hour);
}

void test_cet_cest_adjustment_leaves_invalid_or_incomplete_input_unchanged(
    void) {
  int year = 2023;
  int month = 2;
  int day = 29;
  int hour = 23;
  int minute = 0;
  hal_time_adjust_cet_cest(&year, &month, &day, &hour, &minute);
  TEST_ASSERT_EQUAL_INT(2023, year);
  TEST_ASSERT_EQUAL_INT(2, month);
  TEST_ASSERT_EQUAL_INT(29, day);
  TEST_ASSERT_EQUAL_INT(23, hour);

  hal_time_adjust_cet_cest(nullptr, &month, &day, &hour, &minute);
  TEST_ASSERT_EQUAL_INT(2, month);
  TEST_ASSERT_EQUAL_INT(29, day);
  TEST_ASSERT_EQUAL_INT(23, hour);
}

void test_half_open_time_range_contract(void) {
  TEST_ASSERT_TRUE(hal_time_is_in_range(0, 0, 10));
  TEST_ASSERT_TRUE(hal_time_is_in_range(9, 0, 10));
  TEST_ASSERT_FALSE(hal_time_is_in_range(10, 0, 10));
  TEST_ASSERT_FALSE(hal_time_is_in_range(-1, 0, 10));
  TEST_ASSERT_FALSE(hal_time_is_in_range(5, 10, 0));
}

void test_extract_minutes_handles_sign_and_optional_outputs(void) {
  int hours = 0;
  int minutes = 0;
  hal_time_extract_minutes(125, &hours, &minutes);
  TEST_ASSERT_EQUAL_INT(2, hours);
  TEST_ASSERT_EQUAL_INT(5, minutes);

  hal_time_extract_minutes(-61, &hours, &minutes);
  TEST_ASSERT_EQUAL_INT(-1, hours);
  TEST_ASSERT_EQUAL_INT(-1, minutes);

  minutes = 99;
  hal_time_extract_minutes(60, nullptr, &minutes);
  TEST_ASSERT_EQUAL_INT(0, minutes);
  hal_time_extract_minutes(60, &hours, nullptr);
  TEST_ASSERT_EQUAL_INT(1, hours);
  hal_time_extract_minutes(60, nullptr, nullptr);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_timezone_and_ntp_sync_requests_are_recorded);
  RUN_TEST(test_sync_check_and_formatting);
  RUN_TEST(test_shared_setter_reports_source_and_subseconds);
  RUN_TEST(test_wall_clock_uses_64_bit_monotonic_base_across_millis_wrap);
  RUN_TEST(test_ntp_state_machine_accepts_response_and_keeps_fraction);
  RUN_TEST(test_ntp_status_distinguishes_pending_request_from_valid_clock);
  RUN_TEST(test_ntp_status_reports_final_timeout);
  RUN_TEST(test_ntp_timeout_retries_secondary_server);
  RUN_TEST(test_valid_rtc_restores_unset_wall_clock);
  RUN_TEST(test_invalid_rtc_attaches_without_seeding_wall_clock);
  RUN_TEST(test_rtc_restore_error_keeps_handle_attached_for_ntp_recovery);
  RUN_TEST(test_successful_ntp_sync_is_written_to_attached_rtc);
  RUN_TEST(test_network_service_can_reenter_time_getter);
  RUN_TEST(test_time_snapshots_are_safe_during_concurrent_updates);
  RUN_TEST(test_invalid_inputs_are_rejected);
  RUN_TEST(test_time_from_components_epoch_base);
  RUN_TEST(test_time_from_components_leap_day);
  RUN_TEST(test_time_from_components_invalid_values);
  RUN_TEST(test_time_from_components_rejects_uint32_epoch_overflow);
  RUN_TEST(test_daylight_saving_interval_uses_last_sundays);
  RUN_TEST(test_daylight_saving_rejects_invalid_dates);
  RUN_TEST(test_cet_cest_adjustment_applies_offsets_and_rollovers);
  RUN_TEST(
      test_cet_cest_adjustment_leaves_invalid_or_incomplete_input_unchanged);
  RUN_TEST(test_half_open_time_range_contract);
  RUN_TEST(test_extract_minutes_handles_sign_and_optional_outputs);
  return UNITY_END();
}
