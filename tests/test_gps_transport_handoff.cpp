// hal_gps over the SoftwareSerial transport with a second thread standing in
// for the other core: neither side may wait for the other, and the transport
// handle may never be used after the pause released it.

#include <hal/gps/hal_gps.h>
#include <hal/serial/hal_swserial.h>
#include <utils/unity.h>

#include <atomic>
#include <chrono>
#include <thread>

struct hal_swserial_impl_s {
  bool alive;
};

namespace {

hal_swserial_impl_s s_port;
std::atomic<bool> s_hold_read{false};
std::atomic<bool> s_hold_destroy{false};
std::atomic<bool> s_in_read{false};
std::atomic<bool> s_in_destroy{false};
std::atomic<bool> s_other_done{false};
std::atomic<bool> s_other_done_while_held{false};
std::atomic<int> s_used_after_destroy{0};
std::atomic<int> s_destroyed{0};
std::atomic<int> s_pending{0};

void check_alive(hal_swserial_t handle) {
  if (handle != &s_port || !s_port.alive) {
    s_used_after_destroy++;
  }
}

// Holds the transport call until the other thread finished, or 1 s passed.
void hold_for_other_thread(void) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (!s_other_done && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  s_other_done_while_held = s_other_done.load();
}

void wait_for(const std::atomic<bool> &flag) {
  while (!flag) {
    std::this_thread::yield();
  }
}

} // namespace

extern "C" {

hal_status_t hal_swserial_create_ex(uint8_t, uint8_t, hal_swserial_t *out) {
  s_port.alive = true;
  *out = &s_port;
  return HAL_OK;
}

hal_status_t hal_swserial_begin(hal_swserial_t handle, uint32_t, uint16_t) {
  check_alive(handle);
  return HAL_OK;
}

void hal_swserial_destroy(hal_swserial_t handle) {
  check_alive(handle);
  if (s_hold_destroy.exchange(false)) {
    s_in_destroy = true;
    hold_for_other_thread();
  }
  s_port.alive = false;
  s_destroyed++;
}

int hal_swserial_available(hal_swserial_t handle) {
  check_alive(handle);
  return s_pending;
}

hal_status_t hal_swserial_read_ex(hal_swserial_t handle, uint8_t *out) {
  check_alive(handle);
  if (s_hold_read.exchange(false)) {
    s_in_read = true;
    hold_for_other_thread();
  }
  check_alive(handle);
  s_pending--;
  *out = (uint8_t)'$';
  return HAL_OK;
}

uint32_t hal_millis(void) { return 0u; }
void hal_derr_limited(const char *, const char *, ...) {}
void hal_deb(const char *, ...) {}
}

void setUp(void) {
  hal_gps_init(1u, 2u, 9600u, HAL_UART_CFG_8N1);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gps_resume());
  s_hold_read = s_hold_destroy = false;
  s_in_read = s_in_destroy = false;
  s_other_done = s_other_done_while_held = false;
  s_used_after_destroy = 0;
  s_destroyed = 0;
  s_pending = 0;
}

void tearDown(void) {}

void test_pause_during_update_returns_without_waiting(void) {
  s_pending = 4;
  s_hold_read = true;
  std::atomic<int> pause_status{HAL_EUNKNOWN};
  std::thread other_core([&pause_status] {
    wait_for(s_in_read);
    pause_status = hal_gps_pause();
    s_other_done = true;
  });
  hal_gps_update();
  other_core.join();

  TEST_ASSERT_EQUAL_INT(HAL_OK, pause_status.load());
  TEST_ASSERT_TRUE(s_other_done_while_held.load());
  TEST_ASSERT_EQUAL_INT(0, s_used_after_destroy.load());
  TEST_ASSERT_EQUAL_INT(0, s_pending.load());
  TEST_ASSERT_EQUAL_INT(1, s_destroyed.load());
  TEST_ASSERT_EQUAL_INT(0, hal_gps_serial_available());
}

void test_update_during_pause_skips_transport(void) {
  s_pending = 4;
  s_hold_destroy = true;
  std::thread other_core([] {
    wait_for(s_in_destroy);
    hal_gps_update();
    s_other_done = true;
  });
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gps_pause());
  other_core.join();

  TEST_ASSERT_TRUE(s_other_done_while_held.load());
  TEST_ASSERT_EQUAL_INT(0, s_used_after_destroy.load());
  TEST_ASSERT_EQUAL_INT(4, s_pending.load());
  TEST_ASSERT_EQUAL_INT(1, s_destroyed.load());
}

void test_resume_before_deferred_pause_keeps_transport(void) {
  s_pending = 4;
  s_hold_read = true;
  std::thread other_core([] {
    wait_for(s_in_read);
    (void)hal_gps_pause();
    (void)hal_gps_resume();
    s_other_done = true;
  });
  hal_gps_update();
  other_core.join();

  TEST_ASSERT_TRUE(s_other_done_while_held.load());
  TEST_ASSERT_EQUAL_INT(0, s_destroyed.load());
  s_pending = 2;
  TEST_ASSERT_EQUAL_INT(2, hal_gps_serial_available());
  hal_gps_update();
  TEST_ASSERT_EQUAL_INT(0, s_pending.load());
  TEST_ASSERT_EQUAL_INT(0, s_used_after_destroy.load());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pause_during_update_returns_without_waiting);
  RUN_TEST(test_update_during_pause_skips_transport);
  RUN_TEST(test_resume_before_deferred_pause_keeps_transport);
  return UNITY_END();
}
