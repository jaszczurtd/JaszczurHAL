#include "hal/bluetooth/hal_gamepad.h"
#include "hal/bluetooth/jh_bluetooth_classic_bond_codec.h"
#include "hal/bluetooth/jh_bluetooth_gamepad_identity.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <string.h>

namespace {

hal_gamepad_t s_gamepad = nullptr;

void open_ready(bool known_device = false) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_open(&s_gamepad));
  TEST_ASSERT_NOT_NULL(s_gamepad);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_inject_ready(known_device));
}

void drain_snapshots(void) {
  hal_gamepad_snapshot_t snapshot{};
  while (hal_gamepad_snapshot_next(s_gamepad, &snapshot) == HAL_OK) {
  }
}

hal_bluetooth_classic_scan_result_t
zero2_scan_result(size_t name_length = 21u) {
  hal_bluetooth_classic_scan_result_t gamepad{};
  gamepad.address.bytes[0] = 0x10u;
  gamepad.address.bytes[5] = 0x60u;
  gamepad.class_of_device = 0x0508u;
  memcpy(gamepad.name, "8BitDo Zero 2 gamepad", 21u);
  gamepad.name_length = name_length;
  return gamepad;
}

void inject_gamepad_discovery(
    const hal_bluetooth_classic_scan_result_t &candidate, uint32_t services) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_bluetooth_classic_inject_scan_result(&candidate));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_poll(s_gamepad));

  hal_bluetooth_classic_scan_result_t resolved = candidate;
  resolved.services_resolved = true;
  resolved.services = services;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_bluetooth_classic_inject_scan_result(&resolved));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_poll(s_gamepad));
}

void inject_hid_input(const hal_bluetooth_classic_address_t &address) {
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_bluetooth_hid_inject_connected(&address));
  static constexpr uint8_t descriptor[] = {
      0x05u, 0x01u, 0x09u, 0x05u, 0xa1u, 0x01u, 0x15u,
      0x00u, 0x25u, 0x01u, 0x75u, 0x01u, 0x95u, 0x01u,
      0x05u, 0x09u, 0x09u, 0x01u, 0x81u, 0x02u, 0xc0u,
  };
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_hid_inject_descriptor(
                                    descriptor, sizeof(descriptor)));
  hal_bluetooth_hid_report_t report{};
  report.type = HAL_BLUETOOTH_HID_REPORT_INPUT;
  report.length = 1u;
  report.data[0] = 1u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_hid_inject_report(&report));
}

void authorize_and_inject_hid_input(
    const hal_bluetooth_classic_address_t &address) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_bluetooth_classic_inject_pairing_request(
                  &address, HAL_BLUETOOTH_CLASSIC_PAIRING_JUST_WORKS));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_pairing_authorize(s_gamepad));
  const uint8_t key[16] = {0x5au};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_bluetooth_classic_inject_link_key(&address, key, 4u));
  inject_hid_input(address);
}

hal_gamepad_snapshot_t active_snapshot(uint32_t buttons, int16_t x) {
  hal_gamepad_snapshot_t snapshot{};
  snapshot.buttons = buttons;
  snapshot.axes[HAL_GAMEPAD_AXIS_X] = x;
  snapshot.axes_present = (uint16_t)(1u << HAL_GAMEPAD_AXIS_X);
  snapshot.dpad = HAL_GAMEPAD_DPAD_UP | HAL_GAMEPAD_DPAD_RIGHT;
  snapshot.connected = true;
  return snapshot;
}

hal_gamepad_bond_blob_t s_fake_storage{};
bool s_fake_has_bond = false;
uint32_t s_fake_load_calls = 0u;
uint32_t s_fake_store_calls = 0u;
hal_status_t s_fake_store_status = HAL_OK;

hal_status_t fake_bond_load(void *, hal_gamepad_bond_blob_t *out_blob) {
  ++s_fake_load_calls;
  if (!s_fake_has_bond) {
    return HAL_ENOENT;
  }
  *out_blob = s_fake_storage;
  return HAL_OK;
}

hal_status_t fake_bond_store(void *, const hal_gamepad_bond_blob_t *blob) {
  ++s_fake_store_calls;
  if (s_fake_store_status != HAL_OK) {
    return s_fake_store_status;
  }
  s_fake_storage = *blob;
  s_fake_has_bond = true;
  return HAL_OK;
}

hal_status_t fake_bond_erase(void *) {
  s_fake_has_bond = false;
  return HAL_OK;
}

hal_gamepad_bond_provider_t fake_provider(void) {
  hal_gamepad_bond_provider_t provider{};
  provider.context = nullptr;
  provider.load = fake_bond_load;
  provider.store = fake_bond_store;
  provider.erase = fake_bond_erase;
  return provider;
}

void reset_fake_provider(void) {
  s_fake_has_bond = false;
  s_fake_load_calls = 0u;
  s_fake_store_calls = 0u;
  s_fake_store_status = HAL_OK;
  memset(&s_fake_storage, 0, sizeof(s_fake_storage));
}

} // namespace

void setUp(void) {
  if (s_gamepad != nullptr) {
    (void)hal_gamepad_close(s_gamepad);
  }
  s_gamepad = nullptr;
  hal_mock_gamepad_reset();
  hal_mock_gamepad_runtime_full_reset();
  reset_fake_provider();
}

void tearDown(void) {
  if (s_gamepad != nullptr) {
    (void)hal_gamepad_close(s_gamepad);
  }
  s_gamepad = nullptr;
  hal_mock_gamepad_reset();
  hal_mock_gamepad_runtime_full_reset();
}

void test_open_ready_info_and_single_handle(void) {
  hal_gamepad_t second = nullptr;
  hal_gamepad_info_t info{};

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_gamepad_open(nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_open(&s_gamepad));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_gamepad_open(&second));
  TEST_ASSERT_NULL(second);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_STARTING, info.state);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_inject_ready(false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_READY, info.state);
  TEST_ASSERT_FALSE(info.known_device);

  hal_gamepad_t stale = s_gamepad;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_close(s_gamepad));
  s_gamepad = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_gamepad_poll(stale));
}

void test_pairing_window_authorization_and_reconnect(void) {
  open_ready(false);
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_gamepad_pairing_authorize(s_gamepad));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_pairing_open(s_gamepad));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_gamepad_pairing_open(s_gamepad));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_inject_pairing_request());

  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_TRUE(info.pairing_window_open);
  TEST_ASSERT_TRUE(info.pairing_pending);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_pairing_authorize(s_gamepad));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_inject_connect());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_inject_disconnect());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_TRUE(info.known_device);
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_READY, info.state);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_reconnect(s_gamepad));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_CONNECTING, info.state);
}

void test_reopen_keeps_closed_handle_invalid(void) {
  open_ready(false);
  const hal_gamepad_t stale = s_gamepad;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_close(s_gamepad));
  s_gamepad = nullptr;

  open_ready(false);
  TEST_ASSERT_NOT_EQUAL(stale, s_gamepad);
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_gamepad_poll(stale));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_gamepad_close(stale));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_poll(s_gamepad));
}

void test_known_device_can_be_replaced_and_is_cleared_by_close(void) {
  open_ready(true);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_pairing_open(s_gamepad));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_close(s_gamepad));
  s_gamepad = nullptr;
  open_ready(false);

  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_FALSE(info.known_device);
}

void test_connect_snapshot_and_disconnect_release(void) {
  open_ready();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_inject_connect());

  hal_gamepad_snapshot_t snapshot{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_gamepad_snapshot_next(s_gamepad, &snapshot));
  TEST_ASSERT_TRUE(snapshot.connected);
  TEST_ASSERT_NOT_EQUAL(0u, snapshot.generation);

  hal_gamepad_snapshot_t input = active_snapshot(0x80000005u, 12345);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_inject_snapshot(&input));
  memset(&snapshot, 0, sizeof(snapshot));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_snapshot(s_gamepad, &snapshot));
  TEST_ASSERT_EQUAL_HEX32(0x80000005u, snapshot.buttons);
  TEST_ASSERT_EQUAL_INT16(12345, snapshot.axes[HAL_GAMEPAD_AXIS_X]);
  TEST_ASSERT_EQUAL_UINT16(1u, snapshot.axes_present);
  TEST_ASSERT_EQUAL_UINT8(HAL_GAMEPAD_DPAD_UP | HAL_GAMEPAD_DPAD_RIGHT,
                          snapshot.dpad);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_disconnect(s_gamepad));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_poll(s_gamepad));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_gamepad_snapshot_next(s_gamepad, &snapshot));
  TEST_ASSERT_TRUE(snapshot.connected);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_gamepad_snapshot_next(s_gamepad, &snapshot));
  TEST_ASSERT_FALSE(snapshot.connected);
  TEST_ASSERT_EQUAL_HEX32(0u, snapshot.buttons);
  TEST_ASSERT_EQUAL_UINT16(0u, snapshot.axes_present);
  TEST_ASSERT_EQUAL_UINT8(HAL_GAMEPAD_DPAD_NONE, snapshot.dpad);
}

void test_queue_overflow_is_reported_and_latest_state_is_retained(void) {
  open_ready();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_inject_connect());
  drain_snapshots();

  for (uint32_t index = 0u; index <= HAL_GAMEPAD_SNAPSHOT_QUEUE_DEPTH;
       ++index) {
    const hal_gamepad_snapshot_t snapshot = active_snapshot(
        UINT32_C(1) << (index % HAL_GAMEPAD_BUTTON_COUNT), (int16_t)index);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_inject_snapshot(&snapshot));
  }

  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_UINT32(HAL_GAMEPAD_SNAPSHOT_QUEUE_DEPTH,
                           info.dropped_snapshots);
  TEST_ASSERT_EQUAL_UINT(1u, info.pending_snapshots);
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, hal_gamepad_poll(s_gamepad));

  hal_gamepad_snapshot_t snapshot{};
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        hal_gamepad_snapshot_next(s_gamepad, &snapshot));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_gamepad_snapshot_next(s_gamepad, &snapshot));
  TEST_ASSERT_EQUAL_INT16(HAL_GAMEPAD_SNAPSHOT_QUEUE_DEPTH,
                          snapshot.axes[HAL_GAMEPAD_AXIS_X]);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        hal_gamepad_snapshot_next(s_gamepad, &snapshot));
}

void test_transport_error_releases_inputs_and_fails_runtime(void) {
  open_ready();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_inject_connect());
  drain_snapshots();
  const hal_gamepad_snapshot_t input = active_snapshot(0x3u, -12000);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_inject_snapshot(&input));
  drain_snapshots();

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_gamepad_inject_transport_error(HAL_EIO));
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_gamepad_poll(s_gamepad));

  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_FAILED, info.state);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, info.last_status);

  hal_gamepad_snapshot_t snapshot{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_gamepad_snapshot_next(s_gamepad, &snapshot));
  TEST_ASSERT_FALSE(snapshot.connected);
  TEST_ASSERT_EQUAL_HEX32(0u, snapshot.buttons);
}

void test_legacy_service_status_hook_reports_poll_error(void) {
  open_ready();

  hal_mock_gamepad_set_service_status(HAL_EIO);

  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_gamepad_poll(s_gamepad));
}

void test_open_ex_with_provider_loads_existing_bond(void) {
  jh_bluetooth_classic_bond_identity_t identity{};
  identity.address.bytes[0] = 0x10u;
  identity.address.bytes[5] = 0x60u;
  memset(identity.link_key, 0x5au, sizeof(identity.link_key));
  identity.link_key_type = 4u;
  identity.profile_id = JH_BLUETOOTH_GAMEPAD_BOND_RULES_ID;
  identity.sequence = 7u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_classic_bond_encode(&identity, &s_fake_storage));
  s_fake_has_bond = true;
  const hal_gamepad_bond_provider_t provider = fake_provider();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_open_ex(&s_gamepad, &provider));
  TEST_ASSERT_EQUAL_UINT32(1u, s_fake_load_calls);
  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_TRUE(info.known_device);
}

void test_open_ex_with_provider_and_no_stored_bond_leaves_unknown(void) {
  const hal_gamepad_bond_provider_t provider = fake_provider();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_open_ex(&s_gamepad, &provider));
  TEST_ASSERT_EQUAL_UINT32(1u, s_fake_load_calls);
  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_FALSE(info.known_device);
}

void test_bond_store_persists_through_provider(void) {
  const hal_gamepad_bond_provider_t provider = fake_provider();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_open_ex(&s_gamepad, &provider));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_inject_ready(false));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_inject_bond_store());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_gamepad_bond_store_calls());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_last_bond_store_status());
  TEST_ASSERT_TRUE(s_fake_has_bond);

  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_TRUE(info.known_device);
}

void test_bond_store_without_provider_reports_unsupported(void) {
  open_ready();
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_mock_gamepad_inject_bond_store());
}

void test_forget_erases_bond_disconnects_and_clears_known_device(void) {
  s_fake_has_bond = true;
  const hal_gamepad_bond_provider_t provider = fake_provider();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_open_ex(&s_gamepad, &provider));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_inject_ready(true));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_inject_connect());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_forget(s_gamepad));
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_gamepad_bond_erase_calls());
  TEST_ASSERT_FALSE(s_fake_has_bond);

  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_FALSE(info.known_device);
  hal_gamepad_snapshot_t snapshot{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_snapshot(s_gamepad, &snapshot));
  TEST_ASSERT_FALSE(snapshot.connected);
}

void test_forget_without_provider_still_clears_ram_state(void) {
  open_ready(true);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_forget(s_gamepad));
  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_FALSE(info.known_device);
}

void test_adapter_filters_candidates_then_parses_generic_hid_input(void) {
  open_ready(false);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_pairing_open(s_gamepad));

  hal_bluetooth_classic_scan_result_t keyboard{};
  keyboard.address.bytes[5] = 0x11u;
  keyboard.class_of_device = 0x0540u;
  memcpy(keyboard.name, "Office keyboard", 15u);
  keyboard.name_length = 15u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_bluetooth_classic_inject_scan_result(&keyboard));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_poll(s_gamepad));
  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_DISCOVERING, info.state);

  const hal_bluetooth_classic_scan_result_t gamepad = zero2_scan_result(31u);
  inject_gamepad_discovery(gamepad, HAL_BLUETOOTH_CLASSIC_SERVICE_HID |
                                        HAL_BLUETOOTH_CLASSIC_SERVICE_PNP);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_DISCOVERING, info.state);

  hal_mock_advance_millis(999u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_poll(s_gamepad));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_DISCOVERING, info.state);

  hal_mock_advance_millis(1u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_poll(s_gamepad));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_CONNECTING, info.state);

  authorize_and_inject_hid_input(gamepad.address);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_poll(s_gamepad));

  hal_gamepad_snapshot_t snapshot{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_gamepad_snapshot_next(s_gamepad, &snapshot));
  TEST_ASSERT_TRUE(snapshot.connected);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_gamepad_snapshot_next(s_gamepad, &snapshot));
  TEST_ASSERT_EQUAL_HEX32(1u, snapshot.buttons);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_TRUE(info.known_device);
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_CONNECTED, info.state);
}

void test_adapter_keeps_hid_input_alive_while_bond_store_is_busy(void) {
  const hal_gamepad_bond_provider_t provider = fake_provider();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_open_ex(&s_gamepad, &provider));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_gamepad_inject_ready(false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_pairing_open(s_gamepad));

  const hal_bluetooth_classic_scan_result_t gamepad = zero2_scan_result();
  inject_gamepad_discovery(gamepad, HAL_BLUETOOTH_CLASSIC_SERVICE_HID |
                                        HAL_BLUETOOTH_CLASSIC_SERVICE_PNP);
  hal_mock_advance_millis(1000u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_poll(s_gamepad));

  authorize_and_inject_hid_input(gamepad.address);

  s_fake_store_status = HAL_EBUSY;
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_gamepad_poll(s_gamepad));
  TEST_ASSERT_EQUAL_UINT32(1u, s_fake_store_calls);
  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_CONNECTED, info.state);

  hal_gamepad_snapshot_t snapshot{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_gamepad_snapshot_next(s_gamepad, &snapshot));
  TEST_ASSERT_TRUE(snapshot.connected);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_gamepad_snapshot_next(s_gamepad, &snapshot));
  TEST_ASSERT_EQUAL_HEX32(1u, snapshot.buttons);

  s_fake_store_status = HAL_OK;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_poll(s_gamepad));
  TEST_ASSERT_EQUAL_UINT32(2u, s_fake_store_calls);
  TEST_ASSERT_TRUE(s_fake_has_bond);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_TRUE(info.known_device);
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_CONNECTED, info.state);
}

void test_adapter_restarts_discovery_after_hid_connection_failure(void) {
  open_ready(false);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_pairing_open(s_gamepad));

  const hal_bluetooth_classic_scan_result_t gamepad = zero2_scan_result();
  inject_gamepad_discovery(gamepad, HAL_BLUETOOTH_CLASSIC_SERVICE_HID |
                                        HAL_BLUETOOTH_CLASSIC_SERVICE_PNP);
  hal_mock_advance_millis(1000u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_poll(s_gamepad));

  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_CONNECTING, info.state);
  TEST_ASSERT_TRUE(info.pairing_window_open);

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_bluetooth_hid_inject_disconnected(HAL_EAUTH));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_poll(s_gamepad));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_DISCOVERING, info.state);
  TEST_ASSERT_TRUE(info.pairing_window_open);
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH, info.last_status);
}

void test_failed_known_peer_reconnect_preserves_failure_status(void) {
  open_ready(true);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_reconnect(s_gamepad));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_bluetooth_hid_inject_disconnected(HAL_EBUSY));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_poll(s_gamepad));

  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_READY, info.state);
  TEST_ASSERT_TRUE(info.known_device);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, info.last_status);
}

void test_incoming_connection_waiting_for_descriptor_is_connecting(void) {
  open_ready(true);
  const hal_bluetooth_classic_address_t peer = {
      {0xe4u, 0x17u, 0xd8u, 0x2fu, 0x57u, 0x13u}};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_bluetooth_hid_inject_connected(&peer));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_poll(s_gamepad));

  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_CONNECTING, info.state);
  TEST_ASSERT_TRUE(info.known_device);
}

void test_incoming_connection_is_connecting_before_hid_channels_open(void) {
  open_ready(true);
  const hal_bluetooth_classic_address_t peer = {
      {0xe4u, 0x17u, 0xd8u, 0x2fu, 0x57u, 0x13u}};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_bluetooth_hid_inject_connecting(&peer));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_poll(s_gamepad));

  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_CONNECTING, info.state);
  TEST_ASSERT_TRUE(info.known_device);
}

void test_known_incoming_connection_accepts_input_without_reauthorization(
    void) {
  open_ready(true);
  drain_snapshots();
  const hal_bluetooth_classic_address_t peer = {
      {0x10u, 0x20u, 0x30u, 0x40u, 0x50u, 0x60u}};
  inject_hid_input(peer);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_poll(s_gamepad));
  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_CONNECTED, info.state);
  TEST_ASSERT_EQUAL_INT(HAL_OK, info.last_status);
  hal_gamepad_snapshot_t snapshot{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_gamepad_snapshot_next(s_gamepad, &snapshot));
  TEST_ASSERT_TRUE(snapshot.connected);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_gamepad_snapshot_next(s_gamepad, &snapshot));
  TEST_ASSERT_EQUAL_HEX32(1u, snapshot.buttons);
}

void test_adapter_restarts_discovery_after_incomplete_service_result(void) {
  open_ready(false);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_pairing_open(s_gamepad));

  const hal_bluetooth_classic_scan_result_t gamepad = zero2_scan_result();
  inject_gamepad_discovery(gamepad, HAL_BLUETOOTH_CLASSIC_SERVICE_HID);

  hal_gamepad_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gamepad_get_info(s_gamepad, &info));
  TEST_ASSERT_EQUAL_INT(HAL_GAMEPAD_STATE_DISCOVERING, info.state);
  TEST_ASSERT_TRUE(info.pairing_window_open);
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO, info.last_status);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_open_ready_info_and_single_handle);
  RUN_TEST(test_pairing_window_authorization_and_reconnect);
  RUN_TEST(test_reopen_keeps_closed_handle_invalid);
  RUN_TEST(test_known_device_can_be_replaced_and_is_cleared_by_close);
  RUN_TEST(test_connect_snapshot_and_disconnect_release);
  RUN_TEST(test_queue_overflow_is_reported_and_latest_state_is_retained);
  RUN_TEST(test_transport_error_releases_inputs_and_fails_runtime);
  RUN_TEST(test_legacy_service_status_hook_reports_poll_error);
  RUN_TEST(test_open_ex_with_provider_loads_existing_bond);
  RUN_TEST(test_open_ex_with_provider_and_no_stored_bond_leaves_unknown);
  RUN_TEST(test_bond_store_persists_through_provider);
  RUN_TEST(test_bond_store_without_provider_reports_unsupported);
  RUN_TEST(test_forget_erases_bond_disconnects_and_clears_known_device);
  RUN_TEST(test_forget_without_provider_still_clears_ram_state);
  RUN_TEST(test_adapter_filters_candidates_then_parses_generic_hid_input);
  RUN_TEST(test_adapter_keeps_hid_input_alive_while_bond_store_is_busy);
  RUN_TEST(test_adapter_restarts_discovery_after_hid_connection_failure);
  RUN_TEST(test_failed_known_peer_reconnect_preserves_failure_status);
  RUN_TEST(test_incoming_connection_waiting_for_descriptor_is_connecting);
  RUN_TEST(test_incoming_connection_is_connecting_before_hid_channels_open);
  RUN_TEST(
      test_known_incoming_connection_accepts_input_without_reauthorization);
  RUN_TEST(test_adapter_restarts_discovery_after_incomplete_service_result);
  return UNITY_END();
}
