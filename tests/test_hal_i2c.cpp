#include "hal/i2c/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

void setUp(void) {
  hal_i2c_deinit();
  hal_i2c_deinit_bus(1);
  hal_i2c_init(4, 5, 400000);
  hal_mock_i2c_set_busy(false);
  hal_mock_i2c_set_busy_bus(1, false);
}

void tearDown(void) {}

static unsigned s_scan_callback_count = 0u;

static void scan_progress_callback(void) { s_scan_callback_count++; }

void test_begin_transmission_sets_last_address(void) {
  hal_i2c_begin_transmission(0x3C);
  TEST_ASSERT_EQUAL_UINT8(0x3C, hal_mock_i2c_get_last_addr());
}

void test_request_from_and_read_sequence(void) {
  const uint8_t rx[] = {0x10, 0x20, 0x30};
  hal_mock_i2c_inject_rx(rx, 3);

  TEST_ASSERT_EQUAL_UINT8(3, hal_i2c_request_from(0x48, 3));
  TEST_ASSERT_EQUAL_INT(3, hal_i2c_available());
  TEST_ASSERT_EQUAL_INT(0x10, hal_i2c_read());
  TEST_ASSERT_EQUAL_INT(0x20, hal_i2c_read());
  TEST_ASSERT_EQUAL_INT(0x30, hal_i2c_read());
  TEST_ASSERT_EQUAL_INT(0, hal_i2c_available());
  TEST_ASSERT_EQUAL_INT(-1, hal_i2c_read());
}

void test_write_and_end_transmission_return_success(void) {
  hal_i2c_begin_transmission(0x50);
  TEST_ASSERT_EQUAL_UINT(1, hal_i2c_write(0xAB));
  TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_end_transmission());
}

void test_is_busy_reflects_mock_state(void) {
  hal_mock_i2c_set_busy(false);
  TEST_ASSERT_FALSE(hal_i2c_is_busy(0x67));

  hal_mock_i2c_set_busy(true);
  TEST_ASSERT_TRUE(hal_i2c_is_busy(0x67));
}

void test_bus1_api_independent_state(void) {
  hal_i2c_init_bus(1, 6, 7, 100000);
  hal_i2c_begin_transmission(0x3C);
  TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_end_transmission());
  hal_i2c_begin_transmission_bus(1, 0x52);
  TEST_ASSERT_EQUAL_UINT8(0x52, hal_mock_i2c_get_last_addr_bus(1));
  TEST_ASSERT_EQUAL_UINT8(0x3C, hal_mock_i2c_get_last_addr_bus(0));

  const uint8_t rx1[] = {0xAA, 0xBB};
  hal_mock_i2c_inject_rx_bus(1, rx1, 2);
  TEST_ASSERT_EQUAL_UINT8(2, hal_i2c_request_from_bus(1, 0x52, 2));
  TEST_ASSERT_EQUAL_INT(2, hal_i2c_available_bus(1));
  TEST_ASSERT_EQUAL_INT(0xAA, hal_i2c_read_bus(1));
  TEST_ASSERT_EQUAL_INT(0xBB, hal_i2c_read_bus(1));
  TEST_ASSERT_EQUAL_INT(-1, hal_i2c_read_bus(1));

  hal_mock_i2c_set_busy_bus(1, true);
  TEST_ASSERT_TRUE(hal_i2c_is_busy_bus(1, 0x52));
  hal_mock_i2c_set_busy_bus(1, false);
  TEST_ASSERT_FALSE(hal_i2c_is_busy_bus(1, 0x52));
}

void test_init_records_clock_per_bus(void) {
  TEST_ASSERT_EQUAL_UINT32(400000u, hal_mock_i2c_get_clock_hz());

  hal_i2c_init_bus(1, 6, 7, 100000);
  TEST_ASSERT_EQUAL_UINT32(100000u, hal_mock_i2c_get_clock_hz_bus(1));
  TEST_ASSERT_EQUAL_UINT32(400000u, hal_mock_i2c_get_clock_hz_bus(0));
}

void test_set_clock_updates_default_bus(void) {
  hal_i2c_set_clock(HAL_I2C_CLOCK_FAST_PLUS_HZ);
  TEST_ASSERT_EQUAL_UINT32(HAL_I2C_CLOCK_FAST_PLUS_HZ,
                           hal_mock_i2c_get_clock_hz());
}

void test_set_clock_bus_keeps_buses_independent(void) {
  hal_i2c_init_bus(1, 6, 7, HAL_I2C_CLOCK_STANDARD_HZ);

  hal_i2c_set_clock_bus(1, HAL_I2C_CLOCK_HIGH_SPEED_HZ);

  TEST_ASSERT_EQUAL_UINT32(HAL_I2C_CLOCK_FAST_HZ,
                           hal_mock_i2c_get_clock_hz_bus(0));
  TEST_ASSERT_EQUAL_UINT32(HAL_I2C_CLOCK_HIGH_SPEED_HZ,
                           hal_mock_i2c_get_clock_hz_bus(1));
}

void test_manual_lock_unlock_tracks_depth_per_bus(void) {
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(0));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(1));

  hal_i2c_lock();
  hal_i2c_lock_bus(1);
  hal_i2c_lock_bus(1);

  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_lock_depth_bus(0));
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_mutex_depth_bus(0));
  TEST_ASSERT_EQUAL_INT(2, hal_mock_i2c_get_lock_depth_bus(1));
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_mutex_depth_bus(1));

  hal_i2c_unlock_bus(1);
  hal_i2c_unlock();
  hal_i2c_unlock_bus(1);
  hal_i2c_unlock_bus(1); // underflow-safe no-op

  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(0));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_mutex_depth_bus(0));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(1));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_mutex_depth_bus(1));
}

void test_transmission_calls_balance_lock_depth(void) {
  hal_i2c_begin_transmission(0x3C);
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_lock_depth_bus(0));
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_mutex_depth_bus(0));
  TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_end_transmission());
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(0));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_mutex_depth_bus(0));

  hal_i2c_begin_transmission_bus(1, 0x52);
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_lock_depth_bus(1));
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_mutex_depth_bus(1));
  TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_end_transmission_bus(1));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(1));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_mutex_depth_bus(1));
}

void test_manual_lock_can_wrap_transmission_without_recursive_mutex_take(void) {
  hal_i2c_lock();
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_lock_depth());
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_mutex_depth());

  hal_i2c_begin_transmission(0x50);
  TEST_ASSERT_EQUAL_UINT8(0x50, hal_mock_i2c_get_last_addr());
  TEST_ASSERT_EQUAL_INT(2, hal_mock_i2c_get_lock_depth());
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_mutex_depth());

  TEST_ASSERT_EQUAL_UINT(1, hal_i2c_write(0xAB));
  TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_end_transmission());
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_lock_depth());
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_mutex_depth());

  hal_i2c_unlock();
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth());
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_mutex_depth());
}

void test_manual_lock_wrapped_transmission_takes_physical_mutex_only_once(
    void) {
  TEST_ASSERT_EQUAL_UINT32(0, hal_mock_i2c_get_mutex_take_count());
  TEST_ASSERT_EQUAL_UINT32(0, hal_mock_i2c_get_mutex_give_count());

  hal_i2c_lock();
  TEST_ASSERT_EQUAL_UINT32(1, hal_mock_i2c_get_mutex_take_count());
  TEST_ASSERT_EQUAL_UINT32(0, hal_mock_i2c_get_mutex_give_count());

  hal_i2c_begin_transmission(0x50);
  TEST_ASSERT_EQUAL_UINT32(1, hal_mock_i2c_get_mutex_take_count());
  TEST_ASSERT_EQUAL_INT(2, hal_mock_i2c_get_lock_depth());
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_mutex_depth());

  TEST_ASSERT_EQUAL_UINT(1, hal_i2c_write(0xAB));
  TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_end_transmission());
  TEST_ASSERT_EQUAL_UINT32(1, hal_mock_i2c_get_mutex_take_count());
  TEST_ASSERT_EQUAL_UINT32(0, hal_mock_i2c_get_mutex_give_count());
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_lock_depth());
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_mutex_depth());

  hal_i2c_unlock();
  TEST_ASSERT_EQUAL_UINT32(1, hal_mock_i2c_get_mutex_take_count());
  TEST_ASSERT_EQUAL_UINT32(1, hal_mock_i2c_get_mutex_give_count());
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_mutex_depth());
}

void test_request_from_balances_lock_depth(void) {
  const uint8_t rx0[] = {0x01};
  const uint8_t rx1[] = {0xAA};
  hal_mock_i2c_inject_rx(rx0, 1);
  hal_mock_i2c_inject_rx_bus(1, rx1, 1);

  TEST_ASSERT_EQUAL_UINT8(1, hal_i2c_request_from(0x48, 1));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(0));

  TEST_ASSERT_EQUAL_UINT8(1, hal_i2c_request_from_bus(1, 0x49, 1));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(1));
}

void test_manual_lock_can_wrap_request_from_without_releasing_outer_lock(void) {
  const uint8_t rx[] = {0x10, 0x20};
  hal_mock_i2c_inject_rx(rx, 2);

  hal_i2c_lock();
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_lock_depth());
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_mutex_depth());

  TEST_ASSERT_EQUAL_UINT8(2, hal_i2c_request_from(0x48, 2));
  TEST_ASSERT_EQUAL_INT(2, hal_i2c_available());
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_lock_depth());
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_mutex_depth());

  hal_i2c_unlock();
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth());
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_mutex_depth());
}

void test_manual_lock_wrapped_request_from_does_not_unlock_outer_mutex(void) {
  const uint8_t rx[] = {0x5A};
  hal_mock_i2c_inject_rx(rx, 1);

  hal_i2c_lock();
  const uint32_t takes_after_outer_lock = hal_mock_i2c_get_mutex_take_count();
  const uint32_t gives_after_outer_lock = hal_mock_i2c_get_mutex_give_count();

  TEST_ASSERT_EQUAL_UINT8(1, hal_i2c_request_from(0x48, 1));
  TEST_ASSERT_EQUAL_UINT32(takes_after_outer_lock,
                           hal_mock_i2c_get_mutex_take_count());
  TEST_ASSERT_EQUAL_UINT32(gives_after_outer_lock,
                           hal_mock_i2c_get_mutex_give_count());
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_lock_depth());
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_mutex_depth());

  TEST_ASSERT_EQUAL_INT(0x5A, hal_i2c_read());
  hal_i2c_unlock();
  TEST_ASSERT_EQUAL_UINT32(gives_after_outer_lock + 1u,
                           hal_mock_i2c_get_mutex_give_count());
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth());
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_mutex_depth());
}

void test_deinit_marks_bus_uninitialized(void) {
  TEST_ASSERT_TRUE(hal_mock_i2c_is_initialized());
  TEST_ASSERT_FALSE(hal_mock_i2c_is_initialized_bus(1));

  hal_i2c_init_bus(1, 6, 7, 100000);
  TEST_ASSERT_TRUE(hal_mock_i2c_is_initialized_bus(1));

  hal_i2c_deinit();
  TEST_ASSERT_FALSE(hal_mock_i2c_is_initialized());

  hal_i2c_deinit_bus(1);
  TEST_ASSERT_FALSE(hal_mock_i2c_is_initialized_bus(1));
}

/* ── Transaction count ───────────────────────────────────────────────────────
 */

void test_transaction_count_starts_at_zero(void) {
  TEST_ASSERT_EQUAL_UINT32(0, hal_i2c_get_transaction_count());
}

void test_transaction_count_increments_on_write(void) {
  hal_i2c_begin_transmission(0x50);
  hal_i2c_write(0xAA);
  hal_i2c_end_transmission();
  TEST_ASSERT_EQUAL_UINT32(1, hal_i2c_get_transaction_count());
  hal_i2c_begin_transmission(0x50);
  hal_i2c_end_transmission();
  TEST_ASSERT_EQUAL_UINT32(2, hal_i2c_get_transaction_count());
}

void test_transaction_count_increments_on_request(void) {
  const uint8_t rx[] = {0x01};
  hal_mock_i2c_inject_rx(rx, 1);
  hal_i2c_request_from(0x48, 1);
  TEST_ASSERT_EQUAL_UINT32(1, hal_i2c_get_transaction_count());
}

void test_transaction_count_resets_on_init(void) {
  hal_i2c_begin_transmission(0x50);
  hal_i2c_end_transmission();
  TEST_ASSERT_TRUE(hal_i2c_get_transaction_count() > 0);
  hal_i2c_init(4, 5, 400000);
  TEST_ASSERT_EQUAL_UINT32(0, hal_i2c_get_transaction_count());
}

void test_transaction_count_bus_independence(void) {
  hal_i2c_init_bus(1, 6, 7, 100000);
  hal_i2c_begin_transmission(0x50);
  hal_i2c_end_transmission(); /* bus 0 */
  TEST_ASSERT_EQUAL_UINT32(1, hal_i2c_get_transaction_count());
  TEST_ASSERT_EQUAL_UINT32(0, hal_i2c_get_transaction_count_bus(1));
}

/* ── Bus clear ───────────────────────────────────────────────────────────────
 */

void test_bus_clear_increments_count(void) {
  TEST_ASSERT_EQUAL_UINT32(0, hal_mock_i2c_get_bus_clear_count());
  hal_i2c_bus_clear(4, 5);
  TEST_ASSERT_EQUAL_UINT32(1, hal_mock_i2c_get_bus_clear_count());
  hal_i2c_bus_clear(4, 5);
  TEST_ASSERT_EQUAL_UINT32(2, hal_mock_i2c_get_bus_clear_count());
}

void test_bus_clear_count_resets_on_init(void) {
  hal_i2c_bus_clear(4, 5);
  TEST_ASSERT_TRUE(hal_mock_i2c_get_bus_clear_count() > 0);
  hal_i2c_init(4, 5, 400000);
  TEST_ASSERT_EQUAL_UINT32(0, hal_mock_i2c_get_bus_clear_count());
}

void test_bus_clear_bus_independence(void) {
  hal_i2c_init_bus(1, 6, 7, 100000);
  hal_i2c_bus_clear(4, 5);
  hal_i2c_bus_clear_bus(1, 6, 7);
  hal_i2c_bus_clear_bus(1, 6, 7);
  TEST_ASSERT_EQUAL_UINT32(1, hal_mock_i2c_get_bus_clear_count());
  TEST_ASSERT_EQUAL_UINT32(2, hal_mock_i2c_get_bus_clear_count_bus(1));
}

// ── hal_i2c_write_byte convenience helper ───────────────────────────────────

void test_write_byte_performs_begin_write_end_sequence(void) {
  hal_mock_i2c_set_busy(false);
  bool writeOk = false;
  uint8_t status = hal_i2c_write_byte(0x42, 0xA5, &writeOk);
  TEST_ASSERT_EQUAL_UINT8(0, status);
  TEST_ASSERT_TRUE(writeOk);
  TEST_ASSERT_EQUAL_UINT8(0x42, hal_mock_i2c_get_last_addr());
}

void test_write_byte_returns_end_tx_error_when_bus_busy(void) {
  hal_mock_i2c_set_busy(true);
  bool writeOk = false;
  uint8_t status = hal_i2c_write_byte(0x55, 0x11, &writeOk);
  TEST_ASSERT_NOT_EQUAL(0, status);
  TEST_ASSERT_TRUE(writeOk); // the queue-one-byte step itself succeeded
  hal_mock_i2c_set_busy(false);
}

void test_write_byte_accepts_null_out_flag(void) {
  hal_mock_i2c_set_busy(false);
  uint8_t status = hal_i2c_write_byte(0x21, 0x00, NULL);
  TEST_ASSERT_EQUAL_UINT8(0, status);
  TEST_ASSERT_EQUAL_UINT8(0x21, hal_mock_i2c_get_last_addr());
}

void test_write_byte_balances_lock_depth(void) {
  hal_mock_i2c_set_busy(false);
  int before = hal_mock_i2c_get_lock_depth();
  hal_i2c_write_byte(0x3C, 0x7F, NULL);
  TEST_ASSERT_EQUAL_INT(before, hal_mock_i2c_get_lock_depth());
}

void test_write_byte_bus_routes_to_selected_controller(void) {
  hal_i2c_init_bus(1, 6, 7, 100000);
  hal_mock_i2c_set_busy_bus(1, false);

  bool writeOk = false;
  uint8_t status = hal_i2c_write_byte_bus(1, 0x68, 0x5A, &writeOk);

  TEST_ASSERT_EQUAL_UINT8(0, status);
  TEST_ASSERT_TRUE(writeOk);
  TEST_ASSERT_EQUAL_UINT8(0x68, hal_mock_i2c_get_last_addr_bus(1));
  // The helper must not touch bus 0.
  TEST_ASSERT_NOT_EQUAL(0x68, hal_mock_i2c_get_last_addr_bus(0));
}

void test_write_byte_increments_transaction_count(void) {
  hal_mock_i2c_set_busy(false);
  uint32_t before = hal_i2c_get_transaction_count();
  hal_i2c_write_byte(0x30, 0x01, NULL);
  TEST_ASSERT_EQUAL_UINT32(before + 1, hal_i2c_get_transaction_count());
}

// ── hal_i2c_read_byte convenience helper ────────────────────────────────────

void test_read_byte_returns_injected_value(void) {
  const uint8_t rx[] = {0xA5};
  hal_mock_i2c_inject_rx(rx, 1);

  bool readOk = false;
  uint8_t value = hal_i2c_read_byte(0x48, &readOk);

  TEST_ASSERT_EQUAL_UINT8(0xA5, value);
  TEST_ASSERT_TRUE(readOk);
}

void test_read_byte_preserves_zero_value(void) {
  // Verify a genuine 0x00 is reported as success (not confused with failure).
  const uint8_t rx[] = {0x00};
  hal_mock_i2c_inject_rx(rx, 1);

  bool readOk = false;
  uint8_t value = hal_i2c_read_byte(0x22, &readOk);

  TEST_ASSERT_EQUAL_UINT8(0x00, value);
  TEST_ASSERT_TRUE(readOk);
}

void test_read_byte_accepts_null_out_flag(void) {
  const uint8_t rx[] = {0x7E};
  hal_mock_i2c_inject_rx(rx, 1);

  uint8_t value = hal_i2c_read_byte(0x3C, NULL);
  TEST_ASSERT_EQUAL_UINT8(0x7E, value);
}

void test_read_byte_balances_lock_depth(void) {
  const uint8_t rx[] = {0x42};
  hal_mock_i2c_inject_rx(rx, 1);
  int before = hal_mock_i2c_get_lock_depth();
  (void)hal_i2c_read_byte(0x55, NULL);
  TEST_ASSERT_EQUAL_INT(before, hal_mock_i2c_get_lock_depth());
}

void test_read_byte_holds_lock_during_byte_read(void) {
  const uint8_t rx[] = {0x33};
  hal_mock_i2c_inject_rx(rx, 1);

  bool readOk = false;
  uint8_t value = hal_i2c_read_byte(0x55, &readOk);

  TEST_ASSERT_TRUE(readOk);
  TEST_ASSERT_EQUAL_UINT8(0x33, value);
  TEST_ASSERT_TRUE(hal_mock_i2c_get_read_byte_lock_depth() > 0);
}

void test_read_byte_bus_routes_to_selected_controller(void) {
  hal_i2c_init_bus(1, 6, 7, 100000);
  // Inject different bytes on each bus so a misrouted read returns the
  // wrong value. hal_i2c_request_from_bus() does not update cur_addr, so
  // we can't assert via hal_mock_i2c_get_last_addr_bus() here.
  const uint8_t rx0[] = {0x11};
  const uint8_t rx1[] = {0x9F};
  hal_mock_i2c_inject_rx(rx0, 1);
  hal_mock_i2c_inject_rx_bus(1, rx1, 1);

  bool readOk = false;
  uint8_t value = hal_i2c_read_byte_bus(1, 0x68, &readOk);

  TEST_ASSERT_EQUAL_UINT8(0x9F, value);
  TEST_ASSERT_TRUE(readOk);
}

void test_read_byte_increments_transaction_count(void) {
  const uint8_t rx[] = {0x01};
  hal_mock_i2c_inject_rx(rx, 1);
  uint32_t before = hal_i2c_get_transaction_count();
  (void)hal_i2c_read_byte(0x30, NULL);
  TEST_ASSERT_EQUAL_UINT32(before + 1, hal_i2c_get_transaction_count());
}

void test_read_bytes_copies_exact_rx_sequence_atomically(void) {
  const uint8_t rx[] = {0x12, 0x34, 0x56};
  uint8_t out[3] = {};
  hal_mock_i2c_inject_rx(rx, 3);

  uint32_t before = hal_i2c_get_transaction_count();
  TEST_ASSERT_TRUE(hal_i2c_read_bytes(0x23, out, sizeof(out)));

  TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out, 3);
  TEST_ASSERT_EQUAL_UINT32(before + 1, hal_i2c_get_transaction_count());
  TEST_ASSERT_TRUE(hal_mock_i2c_get_read_byte_lock_depth() > 0);
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth());
}

void test_read_bytes_bus_routes_to_selected_controller(void) {
  hal_i2c_init_bus(1, 6, 7, HAL_I2C_CLOCK_STANDARD_HZ);
  const uint8_t rx0[] = {0x11, 0x22};
  const uint8_t rx1[] = {0xAA, 0xBB};
  uint8_t out[2] = {};
  hal_mock_i2c_inject_rx(rx0, 2);
  hal_mock_i2c_inject_rx_bus(1, rx1, 2);

  TEST_ASSERT_TRUE(hal_i2c_read_bytes_bus(1, 0x5C, out, sizeof(out)));

  TEST_ASSERT_EQUAL_UINT8_ARRAY(rx1, out, 2);
}

void test_read_bytes_rejects_invalid_arguments(void) {
  TEST_ASSERT_FALSE(hal_i2c_read_bytes(0x23, NULL, 2));
  TEST_ASSERT_TRUE(hal_i2c_read_bytes(0x23, NULL, 0));
}

void test_write_read_writes_register_and_consumes_rx_sequence(void) {
  const uint8_t rx[] = {0xAA, 0xBB, 0xCC};
  const uint8_t reg = 0x20;
  uint8_t out[2] = {};
  hal_mock_i2c_inject_rx(rx, 3);
  hal_mock_i2c_reset_write_log();

  uint32_t before = hal_i2c_get_transaction_count();
  TEST_ASSERT_TRUE(hal_i2c_write_read(0x67, &reg, 1, out, 2));

  TEST_ASSERT_EQUAL_UINT8(0xAA, out[0]);
  TEST_ASSERT_EQUAL_UINT8(0xBB, out[1]);
  TEST_ASSERT_EQUAL_UINT32(before + 2, hal_i2c_get_transaction_count());
  TEST_ASSERT_TRUE(hal_mock_i2c_get_read_byte_lock_depth() > 0);

  uint8_t frame[4] = {};
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_write_frame_count());
  TEST_ASSERT_EQUAL_INT(1,
                        hal_mock_i2c_get_write_frame(0, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8(0x20, frame[0]);

  TEST_ASSERT_EQUAL_UINT8(1, hal_i2c_request_from(0x67, 1));
  TEST_ASSERT_EQUAL_INT(0xCC, hal_i2c_read());
}

void test_status_init_and_clock_helpers_report_errors(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_init(4, 5, HAL_I2C_CLOCK_FAST_HZ));
  TEST_ASSERT_EQUAL_UINT32(HAL_I2C_CLOCK_FAST_HZ, hal_mock_i2c_get_clock_hz());

  uint32_t clock_hz = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_get_clock(&clock_hz));
  TEST_ASSERT_EQUAL_UINT32(HAL_I2C_CLOCK_FAST_HZ, clock_hz);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_set_clock(HAL_I2C_CLOCK_STANDARD_HZ));
  TEST_ASSERT_EQUAL_UINT32(HAL_I2C_CLOCK_STANDARD_HZ,
                           hal_mock_i2c_get_clock_hz());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_get_clock(&clock_hz));
  TEST_ASSERT_EQUAL_UINT32(HAL_I2C_CLOCK_STANDARD_HZ, clock_hz);

  /* 0 must normalize to the standard-mode default, consistently with every
   * other backend. */
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_set_clock(0));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_get_clock(&clock_hz));
  TEST_ASSERT_EQUAL_UINT32(HAL_I2C_CLOCK_STANDARD_HZ, clock_hz);

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_i2c_init_bus(9, 4, 5, HAL_I2C_CLOCK_STANDARD_HZ));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_i2c_set_clock_bus(9, HAL_I2C_CLOCK_STANDARD_HZ));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_i2c_get_clock_bus(9, &clock_hz));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_i2c_get_clock_bus(0, NULL));
}

void test_status_end_transmission_maps_legacy_result(void) {
  hal_i2c_begin_transmission(0x50);
  TEST_ASSERT_EQUAL_UINT(1, hal_i2c_write(0xAA));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_end_transmission_ex());

  hal_mock_i2c_set_busy(true);
  hal_i2c_begin_transmission(0x50);
  TEST_ASSERT_EQUAL_INT(HAL_EBUS, hal_i2c_end_transmission_ex());
  hal_mock_i2c_set_busy(false);

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_i2c_end_transmission_bus_ex(9));
}

void test_status_write_byte_reports_queue_and_bus_errors(void) {
  bool write_ok = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_write_byte_ex(0x42, 0xA5, &write_ok));
  TEST_ASSERT_TRUE(write_ok);

  hal_mock_i2c_set_busy(true);
  write_ok = false;
  TEST_ASSERT_EQUAL_INT(HAL_EBUS, hal_i2c_write_byte_ex(0x42, 0xA5, &write_ok));
  TEST_ASSERT_TRUE(write_ok);
  hal_mock_i2c_set_busy(false);

  write_ok = true;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_i2c_write_byte_bus_ex(9, 0x42, 0xA5, &write_ok));
  TEST_ASSERT_FALSE(write_ok);
}

void test_status_read_byte_returns_value_and_rejects_null(void) {
  const uint8_t rx[] = {0x5A};
  uint8_t value = 0u;
  hal_mock_i2c_inject_rx(rx, 1);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_read_byte_ex(0x48, &value));
  TEST_ASSERT_EQUAL_UINT8(0x5A, value);

  value = 0xFFu;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_i2c_read_byte_ex(0x48, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_i2c_read_byte_bus_ex(9, 0x48, &value));
  TEST_ASSERT_EQUAL_UINT8(0u, value);
}

void test_status_read_bytes_and_write_read_validate_arguments(void) {
  const uint8_t rx[] = {0x10, 0x20};
  const uint8_t reg = 0x01;
  uint8_t out[2] = {};
  hal_mock_i2c_inject_rx(rx, 2);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_read_bytes_ex(0x48, out, sizeof(out)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out, 2);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_read_bytes_ex(0x48, NULL, 0));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_i2c_read_bytes_ex(0x48, NULL, 1));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_i2c_read_bytes_bus_ex(9, 0x48, out, 1));

  hal_mock_i2c_inject_rx(rx, 2);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_i2c_write_read_ex(0x48, &reg, 1, out, sizeof(out)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out, 2);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_i2c_write_read_ex(0x48, NULL, 1, out, 1));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_i2c_write_read_ex(0x48, &reg, 1, NULL, 1));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_i2c_write_read_bus_ex(9, 0x48, &reg, 1, out, 1));
}

void test_status_request_from_returns_count(void) {
  const uint8_t rx[] = {0xAA, 0xBB};
  uint8_t received = 0u;
  hal_mock_i2c_inject_rx(rx, 2);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_request_from_ex(0x48, 2, &received));
  TEST_ASSERT_EQUAL_UINT8(2, received);
  TEST_ASSERT_EQUAL_INT(2, hal_i2c_available());
  TEST_ASSERT_EQUAL_INT(0xAA, hal_i2c_read());
  TEST_ASSERT_EQUAL_INT(0xBB, hal_i2c_read());

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_i2c_request_from_ex(0x48, 1, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_i2c_request_from_bus_ex(9, 0x48, 1, &received));
}

void test_status_bus_clear_reports_selected_bus(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_bus_clear(4, 5));
  TEST_ASSERT_EQUAL_UINT32(1, hal_mock_i2c_get_bus_clear_count());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_bus_clear_bus(1, 6, 7));
  TEST_ASSERT_EQUAL_UINT32(1, hal_mock_i2c_get_bus_clear_count_bus(1));

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_i2c_bus_clear_bus(9, 6, 7));
}

/* ── 10-bit addressing (HAL_ENABLE_I2C_10BIT) ────────────────────────────── */
#ifdef HAL_ENABLE_I2C_10BIT

void test_10bit_init_selects_addressing_mode(void) {
  TEST_ASSERT_EQUAL_INT(HAL_I2C_ADDR_MODE_7BIT, hal_i2c_get_addr_mode());
  TEST_ASSERT_FALSE(hal_mock_i2c_is_10bit());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_init_10bit(4, 5, 400000));
  TEST_ASSERT_EQUAL_INT(HAL_I2C_ADDR_MODE_10BIT, hal_i2c_get_addr_mode());
  TEST_ASSERT_TRUE(hal_mock_i2c_is_10bit());

  /* bus 1 stays independent: initialising it in 7-bit mode must not affect
   * the already-10-bit bus 0. */
  hal_i2c_init_bus(1, 6, 7, 100000);
  TEST_ASSERT_EQUAL_INT(HAL_I2C_ADDR_MODE_7BIT, hal_i2c_get_addr_mode_bus(1));
  TEST_ASSERT_EQUAL_INT(HAL_I2C_ADDR_MODE_10BIT, hal_i2c_get_addr_mode_bus(0));
}

void test_10bit_reinit_switches_mode_and_resets_state(void) {
  hal_i2c_init_10bit(4, 5, 400000);
  TEST_ASSERT_TRUE(hal_mock_i2c_is_10bit());
  bool write_ok = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_write_byte_ex(0x150, 0xAA, &write_ok));

  /* Explicit re-init back to 7-bit must go through the normal init cycle
   * and invalidate the 10-bit-only address used above. */
  hal_i2c_init(4, 5, 400000);
  TEST_ASSERT_FALSE(hal_mock_i2c_is_10bit());
  TEST_ASSERT_EQUAL_INT(HAL_I2C_ADDR_MODE_7BIT, hal_i2c_get_addr_mode());
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_i2c_write_byte_ex(0x150, 0xAA, &write_ok));
}

void test_10bit_address_range_validation_depends_on_mode(void) {
  /* 0x090 (144) exceeds the 7-bit range but is a valid 10-bit address: the
   * same numeric value is interpreted differently depending on which init
   * variant configured the controller. */
  bool write_ok = false;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_i2c_write_byte_ex(0x090, 0xAA, &write_ok));

  hal_i2c_init_10bit(4, 5, 400000);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_write_byte_ex(0x090, 0xAA, &write_ok));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_write_byte_ex(0x3FF, 0xAA, &write_ok));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_i2c_write_byte_ex(0x400, 0xAA, &write_ok));
}

void test_10bit_atomic_and_buffered_transfers_succeed(void) {
  hal_i2c_init_10bit(4, 5, 400000);
  const hal_i2c_address_t address = 0x150;

  bool write_ok = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_i2c_write_byte_ex(address, 0xA5, &write_ok));
  TEST_ASSERT_TRUE(write_ok);
  TEST_ASSERT_EQUAL_UINT16(address, hal_mock_i2c_get_last_addr());

  const uint8_t rx[] = {0x11, 0x22};
  hal_mock_i2c_inject_rx(rx, 2);
  uint8_t out[2] = {};
  const uint8_t reg = 0x01;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_i2c_write_read_ex(address, &reg, 1, out, sizeof(out)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out, 2);

  hal_mock_i2c_inject_rx(rx, 2);
  uint8_t direct[2] = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_i2c_read_bytes_ex(address, direct, sizeof(direct)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, direct, 2);
}

void test_10bit_scan_is_unsupported(void) {
  hal_i2c_init_10bit(4, 5, 400000);
  uint8_t addresses[2] = {};
  size_t found = 123u;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_i2c_scan(addresses, 2u, &found, NULL));
  TEST_ASSERT_EQUAL_UINT(0u, found);
}

void test_10bit_lock_depth_balances_like_7bit(void) {
  hal_i2c_init_10bit(4, 5, 400000);
  hal_i2c_begin_transmission(0x150);
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_lock_depth());
  TEST_ASSERT_EQUAL_UINT8(0, hal_i2c_end_transmission());
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth());
}

#endif /* HAL_ENABLE_I2C_10BIT */

void test_scan_returns_present_devices_and_calls_progress_callback(void) {
  uint8_t addresses[4] = {};
  size_t found = 0u;
  s_scan_callback_count = 0u;
  hal_mock_i2c_set_device_present(0x08u, true);
  hal_mock_i2c_set_device_present(0x3Cu, true);
  hal_mock_i2c_set_device_present(0x77u, true);

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_i2c_scan(addresses, 4u, &found, scan_progress_callback));

  TEST_ASSERT_EQUAL_UINT(3u, found);
  TEST_ASSERT_EQUAL_UINT8(0x08u, addresses[0]);
  TEST_ASSERT_EQUAL_UINT8(0x3Cu, addresses[1]);
  TEST_ASSERT_EQUAL_UINT8(0x77u, addresses[2]);
  TEST_ASSERT_EQUAL_UINT(HAL_I2C_SCAN_ADDRESS_COUNT, s_scan_callback_count);
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth());
}

void test_scan_reports_output_overflow_but_counts_all_devices(void) {
  uint8_t addresses[1] = {};
  size_t found = 0u;
  hal_mock_i2c_set_device_present(0x20u, true);
  hal_mock_i2c_set_device_present(0x21u, true);

  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        hal_i2c_scan(addresses, 1u, &found, NULL));
  TEST_ASSERT_EQUAL_UINT(2u, found);
  TEST_ASSERT_EQUAL_UINT8(0x20u, addresses[0]);

  found = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_scan(NULL, 0u, &found, NULL));
  TEST_ASSERT_EQUAL_UINT(2u, found);
}

void test_scan_validates_arguments_bus_and_initialization(void) {
  uint8_t addresses[2] = {};
  size_t found = 123u;

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_i2c_scan(addresses, 2u, NULL, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_i2c_scan(NULL, 1u, &found, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_i2c_scan_bus(9u, addresses, 2u, &found, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT,
                        hal_i2c_scan_bus(1u, addresses, 2u, &found, NULL));
  TEST_ASSERT_EQUAL_UINT(0u, found);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_begin_transmission_sets_last_address);
  RUN_TEST(test_request_from_and_read_sequence);
  RUN_TEST(test_write_and_end_transmission_return_success);
  RUN_TEST(test_is_busy_reflects_mock_state);
  RUN_TEST(test_bus1_api_independent_state);
  RUN_TEST(test_init_records_clock_per_bus);
  RUN_TEST(test_set_clock_updates_default_bus);
  RUN_TEST(test_set_clock_bus_keeps_buses_independent);
  RUN_TEST(test_manual_lock_unlock_tracks_depth_per_bus);
  RUN_TEST(test_transmission_calls_balance_lock_depth);
  RUN_TEST(test_manual_lock_can_wrap_transmission_without_recursive_mutex_take);
  RUN_TEST(
      test_manual_lock_wrapped_transmission_takes_physical_mutex_only_once);
  RUN_TEST(test_request_from_balances_lock_depth);
  RUN_TEST(test_manual_lock_can_wrap_request_from_without_releasing_outer_lock);
  RUN_TEST(test_manual_lock_wrapped_request_from_does_not_unlock_outer_mutex);
  RUN_TEST(test_deinit_marks_bus_uninitialized);
  RUN_TEST(test_transaction_count_starts_at_zero);
  RUN_TEST(test_transaction_count_increments_on_write);
  RUN_TEST(test_transaction_count_increments_on_request);
  RUN_TEST(test_transaction_count_resets_on_init);
  RUN_TEST(test_transaction_count_bus_independence);
  RUN_TEST(test_bus_clear_increments_count);
  RUN_TEST(test_bus_clear_count_resets_on_init);
  RUN_TEST(test_bus_clear_bus_independence);
  RUN_TEST(test_write_byte_performs_begin_write_end_sequence);
  RUN_TEST(test_write_byte_returns_end_tx_error_when_bus_busy);
  RUN_TEST(test_write_byte_accepts_null_out_flag);
  RUN_TEST(test_write_byte_balances_lock_depth);
  RUN_TEST(test_write_byte_bus_routes_to_selected_controller);
  RUN_TEST(test_write_byte_increments_transaction_count);
  RUN_TEST(test_read_byte_returns_injected_value);
  RUN_TEST(test_read_byte_preserves_zero_value);
  RUN_TEST(test_read_byte_accepts_null_out_flag);
  RUN_TEST(test_read_byte_balances_lock_depth);
  RUN_TEST(test_read_byte_holds_lock_during_byte_read);
  RUN_TEST(test_read_byte_bus_routes_to_selected_controller);
  RUN_TEST(test_read_byte_increments_transaction_count);
  RUN_TEST(test_read_bytes_copies_exact_rx_sequence_atomically);
  RUN_TEST(test_read_bytes_bus_routes_to_selected_controller);
  RUN_TEST(test_read_bytes_rejects_invalid_arguments);
  RUN_TEST(test_write_read_writes_register_and_consumes_rx_sequence);
  RUN_TEST(test_status_init_and_clock_helpers_report_errors);
  RUN_TEST(test_status_end_transmission_maps_legacy_result);
  RUN_TEST(test_status_write_byte_reports_queue_and_bus_errors);
  RUN_TEST(test_status_read_byte_returns_value_and_rejects_null);
  RUN_TEST(test_status_read_bytes_and_write_read_validate_arguments);
  RUN_TEST(test_status_request_from_returns_count);
  RUN_TEST(test_status_bus_clear_reports_selected_bus);
#ifdef HAL_ENABLE_I2C_10BIT
  RUN_TEST(test_10bit_init_selects_addressing_mode);
  RUN_TEST(test_10bit_reinit_switches_mode_and_resets_state);
  RUN_TEST(test_10bit_address_range_validation_depends_on_mode);
  RUN_TEST(test_10bit_atomic_and_buffered_transfers_succeed);
  RUN_TEST(test_10bit_scan_is_unsupported);
  RUN_TEST(test_10bit_lock_depth_balances_like_7bit);
#endif
  RUN_TEST(test_scan_returns_present_devices_and_calls_progress_callback);
  RUN_TEST(test_scan_reports_output_overflow_but_counts_all_devices);
  RUN_TEST(test_scan_validates_arguments_bus_and_initialization);
  return UNITY_END();
}
