#include "hal/bluetooth/hal_ble.h"
#include "hal/bluetooth/jh_ble_runtime.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/system/jh_board_runtime.h"
#include "utils/unity.h"

#include <string.h>
#include <thread>

namespace {

constexpr uint16_t kScanInterval60Ms = 0x0060u;
constexpr uint16_t kScanWindow30Ms = 0x0030u;
constexpr uint8_t kAdStructureTypeFieldSize = 1u;
constexpr uint8_t kAdTypeFlags = 0x01u;
constexpr uint8_t kAdTypeCompleteLocalName = 0x09u;
constexpr uint8_t kAdTypeManufacturerSpecificData = 0xffu;
constexpr uint8_t kAdFlagGeneralDiscoverable = 0x02u;
constexpr uint8_t kTeltonikaCompanyIdLow = 0x9au;
constexpr uint8_t kTeltonikaCompanyIdHigh = 0x08u;
constexpr uint8_t kTestManufacturerPayload = 0x42u;
constexpr uint16_t kAdvertisingInterval100Ms = 0x00a0u;
constexpr char kTestDeviceName[] = "JH BLE";

hal_ble_address_t
address(uint8_t tail, hal_ble_address_type_t type = HAL_BLE_ADDRESS_PUBLIC) {
  hal_ble_address_t value{};
  value.bytes[0] = 0x28u;
  value.bytes[1] = 0xCDu;
  value.bytes[2] = 0xC1u;
  value.bytes[3] = 0x14u;
  value.bytes[4] = 0x90u;
  value.bytes[5] = tail;
  value.type = type;
  return value;
}

hal_ble_advertising_config_t advertising(void) {
  hal_ble_advertising_config_t config{};
  config.interval_min = kAdvertisingInterval100Ms;
  config.interval_max = kAdvertisingInterval100Ms;
  const uint8_t payload[] = {
      kAdStructureTypeFieldSize + sizeof(uint8_t),
      kAdTypeFlags,
      kAdFlagGeneralDiscoverable,
      kAdStructureTypeFieldSize + sizeof(kTestDeviceName) - 1u,
      kAdTypeCompleteLocalName,
      'J',
      'H',
      ' ',
      'B',
      'L',
      'E',
  };
  config.data_length = (uint8_t)sizeof(payload);
  memcpy(config.data, payload, sizeof(payload));
  return config;
}

hal_ble_scan_config_t scan_config(void) {
  hal_ble_scan_config_t config{};
  config.interval = kScanInterval60Ms;
  config.window = kScanWindow30Ms;
  config.filter_duplicates = false;
  return config;
}

hal_ble_advertising_report_t advertising_report(uint8_t tail) {
  hal_ble_advertising_report_t report{};
  report.address = address(tail, HAL_BLE_ADDRESS_RANDOM);
  report.event_type = HAL_BLE_ADV_EVENT_NON_CONNECTABLE_UNDIRECTED;
  report.rssi = -52;
  const uint8_t payload[] = {
      kAdStructureTypeFieldSize + sizeof(uint8_t),
      kAdTypeFlags,
      kAdFlagGeneralDiscoverable,
      kAdStructureTypeFieldSize + sizeof(uint16_t) + sizeof(uint8_t),
      kAdTypeManufacturerSpecificData,
      kTeltonikaCompanyIdLow,
      kTeltonikaCompanyIdHigh,
      kTestManufacturerPayload,
  };
  report.data_length = (uint8_t)sizeof(payload);
  memcpy(report.data, payload, sizeof(payload));
  return report;
}

void drain_events(void) {
  hal_ble_event_t event{};
  while (hal_ble_event_next(&event) == HAL_OK) {
  }
}

struct callback_capture_t {
  unsigned calls;
  bool query_succeeded;
  hal_ble_event_type_t last_type;
};

void capture_callback(const hal_ble_event_t *event, void *context) {
  auto *capture = static_cast<callback_capture_t *>(context);
  hal_ble_info_t info{};
  capture->query_succeeded = hal_ble_get_info(&info) == HAL_OK;
  capture->last_type = event->type;
  ++capture->calls;
}

void ready(void) {
  const hal_ble_address_t local = address(0xF8u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_ready(&local));
}

} // namespace

void setUp(void) {
  (void)hal_ble_deinitialize();
  hal_mock_ble_reset();
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_board_runtime_set_inactive(
                                    HAL_BOARD_CAP_BLUETOOTH_CONTROLLER));
}

void tearDown(void) {
  (void)hal_ble_deinitialize();
  hal_mock_ble_runtime_full_reset();
  hal_mock_board_runtime_full_reset();
#ifdef HAL_ENABLE_BLE_STREAM
  hal_mock_ble_stream_runtime_full_reset();
#endif
}

void test_lifecycle_ready_address_and_capability(void) {
  hal_ble_info_t info{};
  hal_ble_address_t local{};
  char text[HAL_BLE_ADDRESS_TEXT_SIZE] = {};

  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_ble_poll());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&info));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STATE_STARTING, info.state);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ble_get_local_address(&local));

  ready();
  hal_ble_event_t event{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_event_next(&event));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_EVENT_CONTROLLER_READY, event.type);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_local_address(&local));
  TEST_ASSERT_EQUAL_UINT8(0xF8u, local.bytes[5]);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_format_address(&local, text, sizeof(text)));
  TEST_ASSERT_EQUAL_STRING("28:CD:C1:14:90:F8", text);
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_ble_format_address(&local, text, sizeof(text) - 1u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_board_require_capabilities(
                                    jh_ble_required_board_capabilities()));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_deinitialize());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_deinitialize());
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_ble_get_local_address(&local));
}

void test_advertising_is_bounded_copied_and_handle_checked(void) {
  hal_ble_advertising_config_t config = advertising();
  hal_ble_advertising_handle_t handle = HAL_BLE_INVALID_HANDLE;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_advertising_start(&config, &handle));
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_INVALID_HANDLE, handle);
  config.data[5] = 'X';
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());

  bool enabled = false;
  hal_ble_advertising_config_t captured{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_get_advertising(&captured, &enabled));
  TEST_ASSERT_FALSE(enabled);

  ready();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_get_advertising(&captured, &enabled));
  TEST_ASSERT_TRUE(enabled);
  TEST_ASSERT_EQUAL_CHAR('J', captured.data[5]);
  hal_ble_advertising_handle_t second_handle = HAL_BLE_INVALID_HANDLE;
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY,
                        hal_ble_advertising_start(&config, &second_handle));
  TEST_ASSERT_EQUAL_UINT32(HAL_BLE_INVALID_HANDLE, second_handle);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_ble_advertising_stop(handle + 1u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_advertising_stop(handle));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_ble_advertising_stop(handle));

  config.data_length = 32u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_ble_advertising_start(&config, &handle));
}

void test_concurrent_backend_operations_are_serialized(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  const hal_ble_advertising_config_t config = advertising();
  hal_ble_advertising_handle_t first_handle = HAL_BLE_INVALID_HANDLE;
  hal_status_t first_status = HAL_NONE;
  hal_mock_ble_block_advertising_start(true);
  std::thread first([&]() {
    first_status = hal_ble_advertising_start(&config, &first_handle);
  });
  while (!hal_mock_ble_advertising_start_entered()) {
    std::this_thread::yield();
  }

  hal_ble_advertising_handle_t second_handle = HAL_BLE_INVALID_HANDLE;
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY,
                        hal_ble_advertising_start(&config, &second_handle));
  TEST_ASSERT_EQUAL_UINT32(HAL_BLE_INVALID_HANDLE, second_handle);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_ble_deinitialize());

  hal_mock_ble_block_advertising_start(false);
  first.join();
  TEST_ASSERT_EQUAL_INT(HAL_OK, first_status);
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_INVALID_HANDLE, first_handle);
}

void test_fatal_event_during_advertising_start_cannot_publish_a_handle(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  const hal_ble_advertising_config_t config = advertising();
  hal_ble_advertising_handle_t handle = HAL_BLE_INVALID_HANDLE;
  hal_status_t start_status = HAL_NONE;
  hal_mock_ble_block_advertising_start(true);
  std::thread starter(
      [&]() { start_status = hal_ble_advertising_start(&config, &handle); });
  while (!hal_mock_ble_advertising_start_entered()) {
    std::this_thread::yield();
  }

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_failure(HAL_EIO));
  hal_mock_ble_block_advertising_start(false);
  starter.join();

  TEST_ASSERT_EQUAL_INT(HAL_EIO, start_status);
  TEST_ASSERT_EQUAL_UINT32(HAL_BLE_INVALID_HANDLE, handle);
  hal_ble_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&info));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STATE_FAILED, info.state);
  TEST_ASSERT_EQUAL_UINT32(HAL_BLE_INVALID_HANDLE, info.advertising);
  TEST_ASSERT_FALSE(info.advertising_requested);
}

void test_connection_mtu_disconnect_and_reconnect_invalidate_handles(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  ready();
  hal_ble_advertising_config_t config = advertising();
  hal_ble_advertising_handle_t advertising_handle = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_advertising_start(&config, &advertising_handle));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
  drain_events();

  const hal_ble_address_t peer = address(0x11u, HAL_BLE_ADDRESS_RANDOM);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_connection(&peer));
  hal_ble_event_t event{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_event_next(&event));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_EVENT_CONNECTED, event.type);
  const hal_ble_connection_handle_t first = event.connection;
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_INVALID_HANDLE, first);
  uint16_t mtu = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_mtu(first, &mtu));
  TEST_ASSERT_EQUAL_UINT16(HAL_BLE_DEFAULT_ATT_MTU, mtu);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_mtu(185u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_event_next(&event));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_EVENT_MTU_UPDATED, event.type);
  TEST_ASSERT_EQUAL_UINT16(185u, event.mtu);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_disconnect(first));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_event_next(&event));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_EVENT_DISCONNECTED, event.type);
  TEST_ASSERT_EQUAL_UINT8(0x16u, event.disconnect_reason);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_ble_get_mtu(first, &mtu));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_connection(&peer));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_event_next(&event));
  if (event.type == HAL_BLE_EVENT_ADVERTISING_STARTED) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_event_next(&event));
  }
  TEST_ASSERT_EQUAL_INT(HAL_BLE_EVENT_CONNECTED, event.type);
  TEST_ASSERT_NOT_EQUAL(first, event.connection);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_ble_disconnect(first));
}

void test_delayed_disconnect_does_not_close_a_reconnected_peer(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  ready();
  const hal_ble_address_t first_peer = address(0x61u);
  const hal_ble_address_t second_peer = address(0x62u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_connection(&first_peer));
  const uint16_t first_native = hal_mock_ble_native_connection();
  TEST_ASSERT_NOT_EQUAL(0u, first_native);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_disconnect(0x13u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_connection(&second_peer));
  const uint16_t second_native = hal_mock_ble_native_connection();
  TEST_ASSERT_NOT_EQUAL(first_native, second_native);
  drain_events();

  hal_ble_info_t before{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&before));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STATE_CONNECTED, before.state);
  TEST_ASSERT_EQUAL_MEMORY(&second_peer, &before.peer_address,
                           sizeof(second_peer));

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_ble_inject_delayed_disconnect(first_native, 0x16u));
  hal_ble_info_t after{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&after));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STATE_CONNECTED, after.state);
  TEST_ASSERT_EQUAL_UINT32(before.connection, after.connection);
  TEST_ASSERT_EQUAL_MEMORY(&second_peer, &after.peer_address,
                           sizeof(second_peer));
  hal_ble_event_t event{};
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ble_event_next(&event));
}

void test_peer_snapshot_is_cleared_when_connection_lifetime_ends(void) {
  const hal_ble_address_t peer = address(0x44u, HAL_BLE_ADDRESS_RANDOM);
  const hal_ble_address_t zero_address{};
  hal_ble_info_t info{};

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  ready();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_connection(&peer));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&info));
  TEST_ASSERT_EQUAL_MEMORY(&peer, &info.peer_address, sizeof(peer));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_deinitialize());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&info));
  TEST_ASSERT_EQUAL_UINT32(HAL_BLE_INVALID_HANDLE, info.connection);
  TEST_ASSERT_EQUAL_UINT16(0u, info.mtu);
  TEST_ASSERT_EQUAL_MEMORY(&zero_address, &info.peer_address,
                           sizeof(zero_address));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  ready();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_connection(&peer));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_failure(HAL_EHW));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&info));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STATE_FAILED, info.state);
  TEST_ASSERT_EQUAL_UINT32(HAL_BLE_INVALID_HANDLE, info.connection);
  TEST_ASSERT_EQUAL_UINT16(0u, info.mtu);
  TEST_ASSERT_EQUAL_MEMORY(&zero_address, &info.peer_address,
                           sizeof(zero_address));
}

void test_callbacks_are_dispatched_by_poll_and_allow_state_queries(void) {
  callback_capture_t capture{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_set_event_callback(capture_callback, &capture));
  ready();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
  TEST_ASSERT_EQUAL_UINT32(1u, capture.calls);
  TEST_ASSERT_TRUE(capture.query_succeeded);
  TEST_ASSERT_EQUAL_INT(HAL_BLE_EVENT_CONTROLLER_READY, capture.last_type);
  hal_ble_event_t event{};
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ble_event_next(&event));
}

void test_queue_overflow_and_fatal_failure_are_observable(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  ready();
  const hal_ble_address_t peer = address(0x22u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_connection(&peer));
  for (unsigned index = 0u; index < HAL_BLE_EVENT_QUEUE_DEPTH + 3u; ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_mock_ble_inject_mtu((uint16_t)(23u + index)));
  }
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, hal_ble_poll());
  hal_ble_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&info));
  TEST_ASSERT_TRUE(info.dropped_events > 0u);

  drain_events();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_failure(HAL_EHW));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&info));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STATE_FAILED, info.state);
  TEST_ASSERT_EQUAL_INT(HAL_EHW, hal_ble_poll());
  TEST_ASSERT_EQUAL_INT(HAL_EHW, hal_ble_initialize());
}

void test_fatal_failure_is_dispatched_before_poll_reports_it(void) {
  callback_capture_t capture{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_set_event_callback(capture_callback, &capture));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_failure(HAL_EIO));
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_ble_poll());
  TEST_ASSERT_EQUAL_UINT32(1u, capture.calls);
  TEST_ASSERT_EQUAL_INT(HAL_BLE_EVENT_ERROR, capture.last_type);
}

void test_fatal_service_failure_invalidates_connection_snapshot(void) {
  const hal_ble_address_t peer = address(0x55u);
  const hal_ble_address_t zero_address{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  ready();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_connection(&peer));
  hal_mock_ble_set_service_status(HAL_EIO);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_ble_poll());

  hal_ble_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&info));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STATE_FAILED, info.state);
  TEST_ASSERT_EQUAL_UINT32(HAL_BLE_INVALID_HANDLE, info.connection);
  TEST_ASSERT_EQUAL_UINT16(0u, info.mtu);
  TEST_ASSERT_EQUAL_MEMORY(&zero_address, &info.peer_address,
                           sizeof(zero_address));
}

void test_fatal_state_ignores_delayed_controller_events(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  ready();
  drain_events();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_failure(HAL_EIO));
  hal_ble_info_t failed{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&failed));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STATE_FAILED, failed.state);
  drain_events();

  const hal_ble_address_t late_address = address(0x7Fu);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_ready(&late_address));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_advertising_stopped());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_scan_stopped());

  hal_ble_info_t after{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&after));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STATE_FAILED, after.state);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, after.last_status);
  TEST_ASSERT_EQUAL_UINT32(failed.generation, after.generation);
  TEST_ASSERT_EQUAL_MEMORY(&failed.local_address, &after.local_address,
                           sizeof(after.local_address));
  hal_ble_event_t event{};
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ble_event_next(&event));
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_ble_poll());
}

void test_passive_scan_copies_reports_and_parses_ad_fields(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  hal_ble_scan_config_t config = scan_config();
  config.window = config.interval + 1u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_ble_scan_start(&config));
  config = scan_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_scan_start(&config));
  hal_ble_advertising_handle_t advertising_handle = HAL_BLE_INVALID_HANDLE;
  const hal_ble_advertising_config_t advertising_config = advertising();
  TEST_ASSERT_EQUAL_INT(
      HAL_EBUSY,
      hal_ble_advertising_start(&advertising_config, &advertising_handle));

  bool enabled = true;
  hal_ble_scan_config_t captured{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_get_scan(&captured, &enabled));
  TEST_ASSERT_FALSE(enabled);

  ready();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_get_scan(&captured, &enabled));
  TEST_ASSERT_TRUE(enabled);
  TEST_ASSERT_EQUAL_UINT16(kScanInterval60Ms, captured.interval);
  TEST_ASSERT_EQUAL_UINT16(kScanWindow30Ms, captured.window);
  TEST_ASSERT_FALSE(captured.filter_duplicates);
  drain_events();

  hal_ble_advertising_report_t injected = advertising_report(0x31u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_ble_inject_advertising_report(&injected));
  injected.data[2] = 0xffu;
  hal_ble_event_t event{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_event_next(&event));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_EVENT_SCAN_REPORT_AVAILABLE, event.type);

  hal_ble_advertising_report_t report{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_scan_report_next(&report));
  TEST_ASSERT_EQUAL_INT(-52, report.rssi);
  TEST_ASSERT_EQUAL_UINT8(kAdFlagGeneralDiscoverable, report.data[2]);
  size_t offset = 0u;
  hal_ble_advertising_field_t field{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_advertising_field_next(&report, &offset, &field));
  TEST_ASSERT_EQUAL_UINT8(kAdTypeFlags, field.type);
  TEST_ASSERT_EQUAL_UINT8(1u, field.data_length);
  TEST_ASSERT_EQUAL_UINT8(kAdFlagGeneralDiscoverable, field.data[0]);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_advertising_field_next(&report, &offset, &field));
  TEST_ASSERT_EQUAL_UINT8(kAdTypeManufacturerSpecificData, field.type);
  TEST_ASSERT_EQUAL_UINT8(3u, field.data_length);
  TEST_ASSERT_EQUAL_UINT8(kTeltonikaCompanyIdLow, field.data[0]);
  TEST_ASSERT_EQUAL_UINT8(kTeltonikaCompanyIdHigh, field.data[1]);
  TEST_ASSERT_EQUAL_INT(
      HAL_EAGAIN, hal_ble_advertising_field_next(&report, &offset, &field));

  report.data_length = 3u;
  report.data[0] = 5u;
  offset = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EIO, hal_ble_advertising_field_next(&report, &offset, &field));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_scan_stop());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
  hal_ble_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&info));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STATE_READY, info.state);
  TEST_ASSERT_FALSE(info.scan_requested);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_ble_scan_stop());
}

void test_scan_report_queue_overflow_is_acknowledged(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_initialize());
  const hal_ble_scan_config_t config = scan_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_scan_start(&config));
  ready();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
  drain_events();

  for (uint8_t index = 0u; index < HAL_BLE_SCAN_REPORT_QUEUE_DEPTH + 2u;
       ++index) {
    const hal_ble_advertising_report_t report = advertising_report(index);
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_mock_ble_inject_advertising_report(&report));
  }
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, hal_ble_poll());
  hal_ble_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&info));
  TEST_ASSERT_EQUAL_UINT32(2u, info.dropped_scan_reports);
  TEST_ASSERT_EQUAL_UINT32(HAL_BLE_SCAN_REPORT_QUEUE_DEPTH,
                           info.pending_scan_reports);

  hal_ble_advertising_report_t report{};
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, hal_ble_scan_report_next(&report));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_scan_report_next(&report));
  TEST_ASSERT_EQUAL_UINT8(0u, report.address.bytes[5]);
  while (hal_ble_scan_report_next(&report) == HAL_OK) {
  }
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ble_scan_report_next(&report));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_poll());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_lifecycle_ready_address_and_capability);
  RUN_TEST(test_advertising_is_bounded_copied_and_handle_checked);
  RUN_TEST(test_concurrent_backend_operations_are_serialized);
  RUN_TEST(test_fatal_event_during_advertising_start_cannot_publish_a_handle);
  RUN_TEST(test_connection_mtu_disconnect_and_reconnect_invalidate_handles);
  RUN_TEST(test_delayed_disconnect_does_not_close_a_reconnected_peer);
  RUN_TEST(test_peer_snapshot_is_cleared_when_connection_lifetime_ends);
  RUN_TEST(test_callbacks_are_dispatched_by_poll_and_allow_state_queries);
  RUN_TEST(test_queue_overflow_and_fatal_failure_are_observable);
  RUN_TEST(test_fatal_failure_is_dispatched_before_poll_reports_it);
  RUN_TEST(test_fatal_service_failure_invalidates_connection_snapshot);
  RUN_TEST(test_fatal_state_ignores_delayed_controller_events);
  RUN_TEST(test_passive_scan_copies_reports_and_parses_ad_fields);
  RUN_TEST(test_scan_report_queue_overflow_is_acknowledged);
  return UNITY_END();
}
