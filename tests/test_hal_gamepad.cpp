#include "hal/bluetooth/hal_gamepad.h"
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

hal_gamepad_snapshot_t active_snapshot(uint32_t buttons, int16_t x) {
  hal_gamepad_snapshot_t snapshot{};
  snapshot.buttons = buttons;
  snapshot.axes[HAL_GAMEPAD_AXIS_X] = x;
  snapshot.axes_present = (uint16_t)(1u << HAL_GAMEPAD_AXIS_X);
  snapshot.dpad = HAL_GAMEPAD_DPAD_UP | HAL_GAMEPAD_DPAD_RIGHT;
  snapshot.connected = true;
  return snapshot;
}

} // namespace

void setUp(void) {
  if (s_gamepad != nullptr) {
    (void)hal_gamepad_close(s_gamepad);
  }
  s_gamepad = nullptr;
  hal_mock_gamepad_reset();
  hal_mock_gamepad_runtime_full_reset();
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

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_open_ready_info_and_single_handle);
  RUN_TEST(test_pairing_window_authorization_and_reconnect);
  RUN_TEST(test_connect_snapshot_and_disconnect_release);
  RUN_TEST(test_queue_overflow_is_reported_and_latest_state_is_retained);
  RUN_TEST(test_transport_error_releases_inputs_and_fails_runtime);
  return UNITY_END();
}
