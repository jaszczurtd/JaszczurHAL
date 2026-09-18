// Runs the ESP32 I2C target, LEDC, RMT and fault backends against a fake
// ESP-IDF that records driver calls and schedules FreeRTOS tasks on demand.

#include "esp_idf_fake.h"

#include "hal/gpio/hal_pwm.h"
#include "hal/gpio/hal_pwm_freq.h"
#include "hal/gpio/hal_rgb_led_internal.h"
#include "hal/i2c/hal_i2c_slave.h"
#include "hal/impl/esp32/jh_esp32_fault.h"
#include "hal/impl/esp32/jh_esp32_ledc.h"
#include "utils/unity.h"

#include <functional>
#include <string>
#include <vector>

namespace {

constexpr uint8_t kSda = 1u;
constexpr uint8_t kScl = 2u;
constexpr uint8_t kAddress = 0x42u;
constexpr uint8_t kPwmPin = 3u;
constexpr uint8_t kRgbPin = 4u;

bool asserts(const std::function<void()> &action) {
  try {
    action();
  } catch (const fake_idf::AssertFailure &) {
    return true;
  }
  return false;
}

std::vector<uint8_t> registers_from(uint8_t first) {
  std::vector<uint8_t> bytes;
  for (unsigned reg = first; reg < HAL_I2C_SLAVE_REG_MAP_SIZE; ++reg) {
    bytes.push_back((uint8_t)(0xA0u + reg));
  }
  return bytes;
}

void start_target(void) {
  hal_i2c_slave_init(kSda, kScl, kAddress);
  for (uint8_t reg = 0u; reg < HAL_I2C_SLAVE_REG_MAP_SIZE; ++reg) {
    hal_i2c_slave_reg_write8(reg, (uint8_t)(0xA0u + reg));
  }
  fake_idf::run_tasks();
  fake_idf::clear_calls();
}

void assert_no_violations(void) {
  const auto &violations = fake_idf::violations();
  TEST_ASSERT_EQUAL_MESSAGE(0, (int)violations.size(),
                            violations.empty() ? ""
                                               : violations.front().c_str());
}

/* A HAL assert or a fake deadlock inside a test is a test failure, not a
 * crash of the whole runner. */
template <void (*Test)(void)> void guarded(void) {
  static std::string failure;
  failure.clear();
  try {
    Test();
  } catch (const std::exception &error) {
    failure = error.what();
  }
  if (!failure.empty()) {
    TEST_FAIL_MESSAGE(failure.c_str());
  }
}

} // namespace

#define RUN_GUARDED(test) UnityDefaultTestRun(guarded<test>, #test, __LINE__)

void setUp(void) { fake_idf::reset(); }

void tearDown(void) {
  hal_i2c_slave_deinit();
  assert_no_violations();
  TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)fake_idf::live_tasks());
}

/* ---- I2C target ---------------------------------------------------------- */

static void test_i2c_events_raised_before_the_worker_runs_are_all_served(void) {
  start_target();
  // A pointer write followed by reads, all before the worker gets the CPU: a
  // one-slot event queue would drop the reads.
  fake_idf::i2c_isr_receive({4u});
  fake_idf::i2c_isr_request();
  fake_idf::i2c_isr_request();
  fake_idf::run_tasks();

  TEST_ASSERT_EQUAL_UINT32(1u,
                           (uint32_t)fake_idf::count_calls("i2c_slave_write"));
  TEST_ASSERT_TRUE(fake_idf::i2c_tx_fifo() == registers_from(4u));
  TEST_ASSERT_EQUAL_UINT32(3u, hal_i2c_slave_get_transaction_count());
}

static void test_i2c_stale_fifo_is_dropped_before_the_new_snapshot(void) {
  start_target();
  fake_idf::i2c_isr_receive({2u});
  fake_idf::i2c_isr_request();
  fake_idf::run_tasks();

  const long reset = fake_idf::find_call("i2c_slave_reset_tx_fifo");
  const long write = fake_idf::find_call("i2c_slave_write");
  TEST_ASSERT_TRUE(reset >= 0);
  TEST_ASSERT_TRUE(write > reset);
  TEST_ASSERT_TRUE(fake_idf::i2c_tx_fifo() == registers_from(2u));
}

static void test_i2c_failed_fifo_reset_defers_the_snapshot(void) {
  start_target();
  fake_idf::fail_next("i2c_slave_reset_tx_fifo", ESP_FAIL);
  fake_idf::i2c_isr_receive({1u});
  fake_idf::i2c_isr_request();
  fake_idf::run_tasks();
  TEST_ASSERT_EQUAL_UINT32(0u,
                           (uint32_t)fake_idf::count_calls("i2c_slave_write"));

  // The pending read survives; the next pointer write retries the reset and
  // then serves it from the new pointer.
  fake_idf::i2c_isr_receive({3u});
  fake_idf::run_tasks();
  TEST_ASSERT_EQUAL_UINT32(1u,
                           (uint32_t)fake_idf::count_calls("i2c_slave_write"));
  TEST_ASSERT_TRUE(fake_idf::i2c_tx_fifo() == registers_from(3u));
}

static void test_i2c_partial_write_continues_where_the_fifo_stopped(void) {
  start_target();
  fake_idf::i2c_isr_receive({4u});
  fake_idf::i2c_accept_next_write(5u);
  fake_idf::i2c_isr_request();
  fake_idf::run_tasks();
  fake_idf::i2c_isr_request();
  fake_idf::run_tasks();

  TEST_ASSERT_EQUAL_UINT32(2u,
                           (uint32_t)fake_idf::count_calls("i2c_slave_write"));
  TEST_ASSERT_TRUE(fake_idf::i2c_tx_fifo() == registers_from(4u));
}

static void test_i2c_pointer_write_during_a_snapshot_wins(void) {
  start_target();
  fake_idf::i2c_isr_receive({4u});
  // The master selects the same register again while the snapshot is queued.
  fake_idf::i2c_during_next_write([] { fake_idf::i2c_isr_receive({4u}); });
  fake_idf::i2c_isr_request();
  fake_idf::run_tasks();
  fake_idf::i2c_isr_request();
  fake_idf::run_tasks();

  // The new selection discards the queued snapshot, so the next read starts
  // at register 4 again instead of after the bytes that were queued.
  TEST_ASSERT_TRUE(fake_idf::i2c_tx_fifo() == registers_from(4u));
}

static void test_i2c_pointer_past_the_map_writes_nothing(void) {
  start_target();
  fake_idf::i2c_isr_receive({(uint8_t)(HAL_I2C_SLAVE_REG_MAP_SIZE + 3u)});
  fake_idf::i2c_isr_request();
  fake_idf::run_tasks();
  TEST_ASSERT_EQUAL_UINT32(0u,
                           (uint32_t)fake_idf::count_calls("i2c_slave_write"));
}

static void test_i2c_master_write_updates_registers(void) {
  start_target();
  fake_idf::i2c_isr_receive({5u, 0x11u, 0x22u});
  TEST_ASSERT_EQUAL_UINT8(0x11u, hal_i2c_slave_reg_read8(5u));
  TEST_ASSERT_EQUAL_UINT8(0x22u, hal_i2c_slave_reg_read8(6u));
  fake_idf::run_tasks();
}

static void test_i2c_deinit_keeps_a_valid_isr_context(void) {
  start_target();
  hal_i2c_slave_deinit();
  TEST_ASSERT_FALSE(fake_idf::i2c_null_context_window());
  TEST_ASSERT_FALSE(fake_idf::i2c_device_exists());
}

static void test_i2c_teardown_stops_the_worker_before_freeing_resources(void) {
  start_target();
  hal_i2c_slave_deinit();

  const long worker_end = fake_idf::find_call("vTaskDelete");
  const long device = fake_idf::find_call("i2c_del_slave_device");
  const long semaphore = fake_idf::find_call("vSemaphoreDelete");
  TEST_ASSERT_TRUE(worker_end >= 0);
  TEST_ASSERT_TRUE(device > worker_end);
  TEST_ASSERT_TRUE(semaphore > device);
}

static void test_i2c_failed_driver_delete_does_not_retain_the_handle(void) {
  start_target();
  fake_idf::fail_next("i2c_del_slave_device", ESP_FAIL);
  TEST_ASSERT_TRUE(asserts([] { hal_i2c_slave_deinit(); }));

  // ESP-IDF freed the device anyway; a new init must not delete it again.
  start_target();
  TEST_ASSERT_EQUAL_UINT32(
      0u, (uint32_t)fake_idf::count_calls("i2c_del_slave_device"));
  TEST_ASSERT_EQUAL_UINT8(kAddress, hal_i2c_slave_get_address());
}

/* ---- LEDC ---------------------------------------------------------------- */

static int ledc_channel_index(void) {
  for (int channel = 0; channel < 8; ++channel) {
    if (fake_idf::ledc_channel(channel).gpio == kPwmPin &&
        fake_idf::ledc_channel(channel).configured) {
      return channel;
    }
  }
  return -1;
}

static void test_ledc_full_scale_holds_the_output_high(void) {
  jh_esp32_ledc_channel_t *channel =
      jh_esp32_ledc_acquire(kPwmPin, 1000u, 255u);
  TEST_ASSERT_NOT_NULL(channel);

  TEST_ASSERT_TRUE(jh_esp32_ledc_write(channel, 128u));
  const int index = ledc_channel_index();
  TEST_ASSERT_TRUE(index >= 0);
  TEST_ASSERT_TRUE(fake_idf::ledc_channel(index).running);
  TEST_ASSERT_EQUAL_UINT32(
      0u, (uint32_t)fake_idf::count_calls("ledc_set_duty_and_update"));

  TEST_ASSERT_TRUE(jh_esp32_ledc_write(channel, 255u));
  TEST_ASSERT_FALSE(fake_idf::ledc_channel(index).running);
  TEST_ASSERT_EQUAL_UINT32(1u, fake_idf::ledc_channel(index).idle_level);
  TEST_ASSERT_TRUE(jh_esp32_ledc_write(channel, 255u));
  TEST_ASSERT_EQUAL_UINT32(1u, (uint32_t)fake_idf::count_calls("ledc_stop"));

  TEST_ASSERT_TRUE(jh_esp32_ledc_write(channel, 64u));
  TEST_ASSERT_TRUE(fake_idf::ledc_channel(index).running);
  TEST_ASSERT_TRUE(fake_idf::ledc_channel(index).duty > 0u);
  TEST_ASSERT_TRUE(jh_esp32_ledc_release(channel));
}

static void test_ledc_failed_release_keeps_the_channel_owned(void) {
  jh_esp32_ledc_channel_t *channel =
      jh_esp32_ledc_acquire(kPwmPin, 1000u, 255u);
  TEST_ASSERT_NOT_NULL(channel);
  TEST_ASSERT_TRUE(jh_esp32_ledc_write(channel, 10u));

  fake_idf::fail_next("ledc_stop", ESP_FAIL);
  TEST_ASSERT_FALSE(jh_esp32_ledc_release(channel));
  TEST_ASSERT_NULL(jh_esp32_ledc_acquire(kPwmPin, 1000u, 255u));
  TEST_ASSERT_TRUE(jh_esp32_ledc_write(channel, 20u));

  fake_idf::fail_next("ledc_channel_deconfigure", ESP_FAIL);
  TEST_ASSERT_FALSE(jh_esp32_ledc_release(channel));
  TEST_ASSERT_NULL(jh_esp32_ledc_acquire(kPwmPin, 1000u, 255u));
  TEST_ASSERT_TRUE(fake_idf::ledc_channel(ledc_channel_index()).configured);

  TEST_ASSERT_TRUE(jh_esp32_ledc_release(channel));
  jh_esp32_ledc_channel_t *again = jh_esp32_ledc_acquire(kPwmPin, 1000u, 255u);
  TEST_ASSERT_NOT_NULL(again);
  TEST_ASSERT_TRUE(jh_esp32_ledc_release(again));
}

static void test_pwm_resolution_change_waits_for_a_clean_release(void) {
  hal_pwm_write(kPwmPin, 100u);
  fake_idf::fail_next("ledc_stop", ESP_FAIL);
  TEST_ASSERT_TRUE(asserts([] { hal_pwm_set_resolution(10u); }));

  // Still 8-bit: 255 is full scale and switches the output to idle high.
  hal_pwm_write(kPwmPin, 255u);
  TEST_ASSERT_EQUAL_UINT32(
      1u, fake_idf::ledc_channel(ledc_channel_index()).idle_level);
  TEST_ASSERT_FALSE(fake_idf::ledc_channel(ledc_channel_index()).running);

  hal_pwm_set_resolution(10u);
  TEST_ASSERT_EQUAL_UINT32(
      1u, (uint32_t)fake_idf::count_calls("ledc_channel_deconfigure"));
  hal_pwm_set_resolution(8u);
}

static void test_pwm_freq_failed_destroy_keeps_the_channel(void) {
  hal_pwm_freq_channel_t channel = hal_pwm_freq_create(kPwmPin, 1000u, 100u);
  TEST_ASSERT_NOT_NULL(channel);
  hal_pwm_freq_write(channel, 50);

  fake_idf::fail_next("ledc_stop", ESP_FAIL);
  TEST_ASSERT_TRUE(asserts([channel] { hal_pwm_freq_destroy(channel); }));
  const size_t updates = fake_idf::count_calls("ledc_set_duty_and_update");
  hal_pwm_freq_write(channel, 60);
  TEST_ASSERT_EQUAL_UINT32(
      (uint32_t)updates + 1u,
      (uint32_t)fake_idf::count_calls("ledc_set_duty_and_update"));

  hal_pwm_freq_destroy(channel);
  fake_idf::clear_calls();
  hal_pwm_freq_write(channel, 70);
  TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)fake_idf::calls().size());
}

/* ---- RMT ----------------------------------------------------------------- */

static void test_rmt_failed_channel_delete_keeps_the_handle_for_retry(void) {
  TEST_ASSERT_EQUAL(HAL_OK, jh_hal_rgb_led_prepare_transport(kRgbPin, true));
  TEST_ASSERT_EQUAL_UINT32(1u, (uint32_t)fake_idf::rmt_live_channels());

  fake_idf::fail_next("rmt_del_channel", ESP_ERR_INVALID_STATE);
  TEST_ASSERT_EQUAL(HAL_ESTATE,
                    jh_hal_rgb_led_prepare_transport(kRgbPin, true));
  TEST_ASSERT_EQUAL_UINT32(1u, (uint32_t)fake_idf::rmt_live_channels());
  TEST_ASSERT_EQUAL_UINT32(1u, (uint32_t)fake_idf::rmt_live_encoders());

  jh_hal_rgb_led_release_transport();
  TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)fake_idf::rmt_live_channels());
  TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)fake_idf::rmt_live_encoders());
}

static void test_rmt_failed_encoder_delete_keeps_the_encoder_for_retry(void) {
  TEST_ASSERT_EQUAL(HAL_OK, jh_hal_rgb_led_prepare_transport(kRgbPin, true));
  const uint8_t pixels[3] = {1u, 2u, 3u};
  TEST_ASSERT_TRUE(
      jh_hal_rgb_led_write_pixels(pixels, 3u, true, kRgbPin, nullptr));

  fake_idf::fail_next("rmt_del_encoder", ESP_FAIL);
  jh_hal_rgb_led_release_transport();
  TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)fake_idf::rmt_live_channels());
  TEST_ASSERT_EQUAL_UINT32(1u, (uint32_t)fake_idf::rmt_live_encoders());

  jh_hal_rgb_led_release_transport();
  TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)fake_idf::rmt_live_encoders());
}

/* ---- Fault handlers ------------------------------------------------------ */

static void test_fault_init_retries_the_other_core_after_an_ipc_failure(void) {
  fake_idf::set_core(0);
  fake_idf::fail_next("esp_ipc_call_blocking", ESP_ERR_TIMEOUT);
  jh_esp32_fault_init();
  const size_t core0 = fake_idf::exception_handlers_installed(0);
  TEST_ASSERT_TRUE(core0 > 0u);
  TEST_ASSERT_EQUAL_UINT32(0u,
                           (uint32_t)fake_idf::exception_handlers_installed(1));

  jh_esp32_fault_init();
  TEST_ASSERT_EQUAL_UINT32((uint32_t)core0,
                           (uint32_t)fake_idf::exception_handlers_installed(1));
  TEST_ASSERT_EQUAL_UINT32((uint32_t)core0,
                           (uint32_t)fake_idf::exception_handlers_installed(0));

  jh_esp32_fault_init();
  TEST_ASSERT_EQUAL_UINT32(
      2u, (uint32_t)fake_idf::count_calls("esp_ipc_call_blocking"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_GUARDED(test_i2c_events_raised_before_the_worker_runs_are_all_served);
  RUN_GUARDED(test_i2c_stale_fifo_is_dropped_before_the_new_snapshot);
  RUN_GUARDED(test_i2c_failed_fifo_reset_defers_the_snapshot);
  RUN_GUARDED(test_i2c_partial_write_continues_where_the_fifo_stopped);
  RUN_GUARDED(test_i2c_pointer_write_during_a_snapshot_wins);
  RUN_GUARDED(test_i2c_pointer_past_the_map_writes_nothing);
  RUN_GUARDED(test_i2c_master_write_updates_registers);
  RUN_GUARDED(test_i2c_deinit_keeps_a_valid_isr_context);
  RUN_GUARDED(test_i2c_teardown_stops_the_worker_before_freeing_resources);
  RUN_GUARDED(test_i2c_failed_driver_delete_does_not_retain_the_handle);
  RUN_GUARDED(test_ledc_full_scale_holds_the_output_high);
  RUN_GUARDED(test_ledc_failed_release_keeps_the_channel_owned);
  RUN_GUARDED(test_pwm_resolution_change_waits_for_a_clean_release);
  RUN_GUARDED(test_pwm_freq_failed_destroy_keeps_the_channel);
  RUN_GUARDED(test_rmt_failed_channel_delete_keeps_the_handle_for_retry);
  RUN_GUARDED(test_rmt_failed_encoder_delete_keeps_the_encoder_for_retry);
  RUN_GUARDED(test_fault_init_retries_the_other_core_after_an_ipc_failure);
  return UNITY_END();
}
