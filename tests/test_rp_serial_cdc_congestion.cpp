// The RP debug serial port compiled against a fake hal_usb CDC: a host that
// keeps DTR asserted but stops reading costs exactly one bounded write
// timeout, after which debug writes drop instead of blocking, and blocking
// resumes once the host drains data or releases DTR.

#include "hal/debug/jh_serial_port.h"
#include "hal/usb/hal_usb.h"
#include "utils/unity.h"

#include <stddef.h>
#include <stdint.h>

namespace {

constexpr size_t kLargeFifo = 4096u;

bool s_connected = true;
size_t s_fifo_free = kLargeFifo;
uint64_t s_blocked_ms_total = 0u;
uint32_t s_blocked_ms_single_max = 0u;
uint32_t s_last_write_timeout_ms = UINT32_MAX;
uint32_t s_last_flush_timeout_ms = UINT32_MAX;

} // namespace

extern "C" {

hal_status_t hal_usb_init(void) { return HAL_OK; }

hal_status_t hal_usb_task(void) { return HAL_OK; }

hal_status_t hal_usb_cdc_available(size_t *out_available) {
  *out_available = 0u;
  return HAL_OK;
}

hal_status_t hal_usb_cdc_read(uint8_t *data, size_t capacity,
                              size_t *out_read) {
  (void)data;
  (void)capacity;
  *out_read = 0u;
  return HAL_EAGAIN;
}

hal_status_t hal_usb_cdc_flush(uint32_t timeout_ms) {
  s_last_flush_timeout_ms = timeout_ms;
  return s_connected ? HAL_OK : HAL_EAGAIN;
}

// Mirrors the real backend's outcomes: EAGAIN without DTR, HAL_OK when the
// FIFO takes everything, and HAL_ETIMEOUT after timeout_ms without progress.
// The blocked counters record the wall time a caller would have lost.
hal_status_t hal_usb_cdc_write(const uint8_t *data, size_t length,
                               uint32_t timeout_ms, size_t *out_written) {
  (void)data;
  s_last_write_timeout_ms = timeout_ms;
  *out_written = 0u;
  if (!s_connected) {
    return HAL_EAGAIN;
  }
  const size_t accepted = length < s_fifo_free ? length : s_fifo_free;
  s_fifo_free -= accepted;
  *out_written = accepted;
  if (accepted == length) {
    return HAL_OK;
  }
  s_blocked_ms_total += timeout_ms;
  if (timeout_ms > s_blocked_ms_single_max) {
    s_blocked_ms_single_max = timeout_ms;
  }
  return HAL_ETIMEOUT;
}

} // extern "C"

static void write_bytes(size_t count) {
  static const char filler[64] = "0123456789abcdef0123456789abcdef"
                                 "0123456789abcdef0123456789abcde";
  while (count > 0u) {
    const size_t chunk = count < sizeof(filler) ? count : sizeof(filler);
    jh_serial_port_write(filler, chunk);
    count -= chunk;
  }
}

static void make_congested(void) {
  s_fifo_free = 0u;
  write_bytes(8u);
  TEST_ASSERT_EQUAL_UINT32(HAL_USB_CDC_WRITE_TIMEOUT_MS,
                           s_last_write_timeout_ms);
}

void setUp(void) {
  // The backend keeps its congestion flag across tests; a fully accepted
  // write is the documented way back to the blocking state.
  s_connected = true;
  s_fifo_free = kLargeFifo;
  write_bytes(1u);
  s_fifo_free = kLargeFifo;
  s_blocked_ms_total = 0u;
  s_blocked_ms_single_max = 0u;
  s_last_write_timeout_ms = UINT32_MAX;
  s_last_flush_timeout_ms = UINT32_MAX;
}

void tearDown(void) { jh_serial_port_set_flush(false); }

void test_ready_host_gets_bounded_blocking_writes(void) {
  write_bytes(11u);
  TEST_ASSERT_EQUAL_UINT32(HAL_USB_CDC_WRITE_TIMEOUT_MS,
                           s_last_write_timeout_ms);
  TEST_ASSERT_EQUAL_UINT64(0u, s_blocked_ms_total);
}

void test_stuck_host_pays_one_timeout_then_drops(void) {
  make_congested();
  TEST_ASSERT_EQUAL_UINT64(HAL_USB_CDC_WRITE_TIMEOUT_MS, s_blocked_ms_total);

  for (int i = 0; i < 100; ++i) {
    write_bytes(32u);
    TEST_ASSERT_EQUAL_UINT32(0u, s_last_write_timeout_ms);
  }
  TEST_ASSERT_EQUAL_UINT64(HAL_USB_CDC_WRITE_TIMEOUT_MS, s_blocked_ms_total);
  TEST_ASSERT_EQUAL_UINT32(HAL_USB_CDC_WRITE_TIMEOUT_MS,
                           s_blocked_ms_single_max);
}

void test_partial_progress_keeps_dropping(void) {
  make_congested();
  s_fifo_free = 3u;
  write_bytes(8u);
  TEST_ASSERT_EQUAL_UINT32(0u, s_last_write_timeout_ms);
  s_fifo_free = 0u;
  write_bytes(8u);
  TEST_ASSERT_EQUAL_UINT32(0u, s_last_write_timeout_ms);
  TEST_ASSERT_EQUAL_UINT64(HAL_USB_CDC_WRITE_TIMEOUT_MS, s_blocked_ms_total);
}

void test_host_drain_restores_blocking_writes(void) {
  make_congested();
  s_fifo_free = kLargeFifo;
  write_bytes(5u);
  TEST_ASSERT_EQUAL_UINT32(0u, s_last_write_timeout_ms);
  write_bytes(5u);
  TEST_ASSERT_EQUAL_UINT32(HAL_USB_CDC_WRITE_TIMEOUT_MS,
                           s_last_write_timeout_ms);
}

void test_dtr_release_restores_blocking_writes(void) {
  make_congested();
  s_connected = false;
  write_bytes(4u);
  TEST_ASSERT_EQUAL_UINT32(0u, s_last_write_timeout_ms);

  s_connected = true;
  s_fifo_free = kLargeFifo;
  write_bytes(4u);
  TEST_ASSERT_EQUAL_UINT32(HAL_USB_CDC_WRITE_TIMEOUT_MS,
                           s_last_write_timeout_ms);
}

void test_absent_host_never_blocks_or_latches(void) {
  s_connected = false;
  s_fifo_free = 0u;
  for (int i = 0; i < 50; ++i) {
    write_bytes(16u);
  }
  TEST_ASSERT_EQUAL_UINT64(0u, s_blocked_ms_total);

  s_connected = true;
  s_fifo_free = kLargeFifo;
  write_bytes(4u);
  TEST_ASSERT_EQUAL_UINT32(HAL_USB_CDC_WRITE_TIMEOUT_MS,
                           s_last_write_timeout_ms);
}

void test_flush_follows_congestion_state(void) {
  jh_serial_port_set_flush(true);
  jh_serial_port_flush();
  TEST_ASSERT_EQUAL_UINT32(HAL_USB_CDC_WRITE_TIMEOUT_MS,
                           s_last_flush_timeout_ms);

  make_congested();
  jh_serial_port_flush();
  TEST_ASSERT_EQUAL_UINT32(0u, s_last_flush_timeout_ms);
}

// A minute of debug chatter against a stuck host: the device FIFO absorbs the
// first bytes, then every line drops. The whole episode may cost at most one
// write timeout of wall time, so a watchdog fed between lines never starves.
void test_watchdog_budget_over_a_minute_of_stuck_host(void) {
  s_fifo_free = 512u;
  for (int line = 0; line < 600; ++line) {
    write_bytes(24u);
    write_bytes(40u);
    char line_ending[2];
    (void)jh_serial_port_finish_line(line_ending);
  }
  TEST_ASSERT_EQUAL_UINT64(HAL_USB_CDC_WRITE_TIMEOUT_MS, s_blocked_ms_total);
  TEST_ASSERT_EQUAL_UINT32(HAL_USB_CDC_WRITE_TIMEOUT_MS,
                           s_blocked_ms_single_max);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ready_host_gets_bounded_blocking_writes);
  RUN_TEST(test_stuck_host_pays_one_timeout_then_drops);
  RUN_TEST(test_partial_progress_keeps_dropping);
  RUN_TEST(test_host_drain_restores_blocking_writes);
  RUN_TEST(test_dtr_release_restores_blocking_writes);
  RUN_TEST(test_absent_host_never_blocks_or_latches);
  RUN_TEST(test_flush_follows_congestion_state);
  RUN_TEST(test_watchdog_budget_over_a_minute_of_stuck_host);
  return UNITY_END();
}
