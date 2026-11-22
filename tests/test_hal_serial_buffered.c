#include "hal/serial/hal_serial.h"
#include "hal/serial/hal_serial_frame.h"
#include "hal/serial/hal_serial_session.h"
#include "support/serial_mock_c.h"
#include "utils/unity.h"
#include <string.h>

static uint8_t s_storage[32];

void setUp(void) {
  hal_debug_init_default();
  hal_debug_set_muted(false);
  hal_debug_set_module_prefix("TX");
  jh_test_serial_reset();
  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_serial_buffered_init(s_storage, sizeof(s_storage)));
}

void tearDown(void) {
  jh_test_serial_tx_limit(SIZE_MAX);
  for (unsigned i = 0U; i < 32U; ++i) {
    size_t written;
    TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(32U, &written));
    if (written == 0U) {
      break;
    }
  }
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_init(NULL, 0U));
}

static void assertStatus(size_t bytes, uint32_t drops) {
  size_t pending = SIZE_MAX;
  uint32_t dropped = UINT32_MAX;
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_status(&pending, &dropped));
  TEST_ASSERT_EQUAL_size_t(bytes, pending);
  TEST_ASSERT_EQUAL_UINT32(drops, dropped);
}

static void injectRequest(uint16_t sequence, const char *payload) {
  char frame[HAL_SERIAL_FRAME_LINE_MAX];
  size_t length = 0U;
  TEST_ASSERT_TRUE(hal_serial_frame_encode(sequence, payload, frame,
                                           sizeof(frame) - 1U, &length));
  frame[length++] = '\n';
  frame[length] = '\0';
  jh_test_serial_inject(frame);
}

static void
test_stalled_and_partial_writes_resume_without_loss_or_repetition(void) {
  hal_serial_println("abcdef");
  TEST_ASSERT_EQUAL_STRING("", jh_test_serial_output());
  assertStatus(11U, 0U);
  size_t written = 999U;
  jh_test_serial_tx_limit(0U);
  TEST_ASSERT_EQUAL(HAL_EAGAIN, hal_serial_buffered_poll(20U, &written));
  TEST_ASSERT_EQUAL_size_t(0U, written);
  assertStatus(11U, 0U);
  jh_test_serial_tx_limit(2U);
  TEST_ASSERT_EQUAL(HAL_EAGAIN, hal_serial_buffered_poll(20U, &written));
  TEST_ASSERT_EQUAL_size_t(2U, written);
  TEST_ASSERT_EQUAL_STRING("ab", jh_test_serial_output());
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(1U, &written));
  TEST_ASSERT_EQUAL_size_t(1U, written);
  TEST_ASSERT_EQUAL_STRING("abc", jh_test_serial_output());
  jh_test_serial_tx_limit(SIZE_MAX);
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(20U, &written));
  TEST_ASSERT_EQUAL_size_t(4U, written);
  TEST_ASSERT_EQUAL_STRING("abcdef\n", jh_test_serial_output());
  assertStatus(0U, 0U);
}

static void test_overflow_discards_whole_messages_and_preserves_fifo(void) {
  hal_serial_println("12345678");
  hal_serial_println("abcdefgh");
  hal_serial_println("XYZ");
  assertStatus(26U, 1U);
  size_t written;
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(100U, &written));
  TEST_ASSERT_EQUAL_STRING("12345678\n", jh_test_serial_output());
  assertStatus(13U, 1U);
  /* The payload crosses the physical end of the buffer. */
  hal_serial_println("wrap!");
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(100U, &written));
  TEST_ASSERT_EQUAL_STRING("abcdefgh\n", jh_test_serial_output());
  for (unsigned i = 0U; i < 2U; ++i) {
    TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(100U, &written));
  }
  TEST_ASSERT_EQUAL_STRING("wrap!\n", jh_test_serial_output());
  assertStatus(0U, 1U);

  /* Move the write cursor from 4 to 30, then wrap the next record header. */
  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_serial_buffered_write("1234567890123456789012", 22U));
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(100U, &written));
  TEST_ASSERT_EQUAL_size_t(22U, written);
  jh_test_serial_reset();
  hal_deb("h");
  jh_test_serial_tx_limit(2U);
  TEST_ASSERT_EQUAL(HAL_EAGAIN, hal_serial_buffered_poll(100U, &written));
  TEST_ASSERT_EQUAL_STRING("TX", jh_test_serial_debug());
  jh_test_serial_tx_limit(SIZE_MAX);
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(100U, &written));
  TEST_ASSERT_EQUAL_STRING("TX: h\n", jh_test_serial_debug());
  assertStatus(0U, 1U);
}

static void test_formatter_overflow_rolls_back_every_fragment(void) {
  hal_deb("%s%s", "12345678901234567890", "abcdefghijklmnopqrst");
  assertStatus(0U, 1U);
  TEST_ASSERT_EQUAL_STRING("", jh_test_serial_debug());
  hal_deb("value:%d", 42);
  size_t written;
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(100U, &written));
  TEST_ASSERT_EQUAL_STRING("TX: value:42\n", jh_test_serial_debug());
  assertStatus(0U, 1U);
}

static void
test_explicit_enqueue_returns_backpressure_without_counting_a_drop(void) {
  TEST_ASSERT_EQUAL(
      HAL_OK, hal_serial_buffered_write("1234567890123456789012345678", 28U));
  assertStatus(32U, 0U);
  TEST_ASSERT_EQUAL(HAL_EAGAIN, hal_serial_buffered_write("x", 1U));
  const char tooLarge[33] = {0};
  TEST_ASSERT_EQUAL(HAL_EOVERFLOW,
                    hal_serial_buffered_write(tooLarge, sizeof(tooLarge)));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_serial_buffered_write(NULL, 1U));
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_write(NULL, 0U));
  assertStatus(32U, 0U);
  size_t written;
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(0U, &written));
  TEST_ASSERT_EQUAL_size_t(0U, written);
  TEST_ASSERT_EQUAL_STRING("", jh_test_serial_output());
  TEST_ASSERT_EQUAL(HAL_EBUSY, hal_serial_buffered_init(NULL, 0U));
  TEST_ASSERT_EQUAL(HAL_EINVAL, hal_serial_buffered_init(s_storage, 4U));
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(100U, &written));
  TEST_ASSERT_EQUAL_size_t(28U, written);
  assertStatus(0U, 0U);
}

static void test_session_retains_reply_under_backpressure(void) {
  static uint8_t storage[512];
  static char full[508];
  hal_serial_session_t session;
  memset(full, 'x', sizeof(full));
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_init(storage, sizeof(storage)));
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_write(full, sizeof(full)));
  hal_serial_session_init(&session, "ECU", "224", "test");
  injectRequest(42U, "HELLO");
  jh_test_serial_tx_limit(0U);
  hal_serial_session_poll(&session);
  TEST_ASSERT_EQUAL_UINT32(1U, session.hello_counter);
  assertStatus(512U, 0U);
  /* A callback sending again must not overwrite its retained first reply. */
  session.in_request = true;
  uint16_t sequence = 0U;
  TEST_ASSERT_EQUAL(HAL_EAGAIN,
                    hal_serial_session_println_ex(&session, "SECOND"));
  TEST_ASSERT_EQUAL(
      HAL_OK, hal_serial_session_current_request_seq(&session, &sequence));
  TEST_ASSERT_EQUAL_UINT16(42U, sequence);
  session.in_request = false;
  /* A second request must wait until the retained reply is accepted. */
  injectRequest(43U, "HELLO");
  hal_serial_session_poll(&session);
  TEST_ASSERT_EQUAL_UINT32(1U, session.hello_counter);
  jh_test_serial_tx_limit(SIZE_MAX);
  size_t written = 0U;
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(512U, &written));
  TEST_ASSERT_EQUAL_size_t(508U, written);
  hal_serial_session_poll(&session);
  TEST_ASSERT_EQUAL_UINT32(2U, session.hello_counter);
  /* Ring wrap can split each frame into two chunks. */
  for (unsigned i = 0U; i < 2U; ++i) {
    TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(512U, &written));
  }
  TEST_ASSERT_NOT_NULL(strstr(jh_test_serial_output(), "$SC,42,"));
  for (unsigned i = 0U; i < 2U; ++i) {
    TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(512U, &written));
  }
  TEST_ASSERT_NOT_NULL(strstr(jh_test_serial_output(), "$SC,43,"));
  assertStatus(0U, 0U);
}

static void test_reboot_waits_for_queued_ack_to_reach_port(void) {
  static uint8_t storage[512];
  static char full[508];
  static const hal_serial_session_vocabulary_t vocab = {
      .cmd_reboot_bootloader = "REBOOT", .reply_reboot_ok = "OK REBOOT"};
  hal_serial_session_t session;
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_init(storage, sizeof(storage)));
  memset(full, 'x', sizeof(full));
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_write(full, sizeof(full)));
  hal_serial_session_init_with_vocabulary(&session, "ECU", "224", "test",
                                          &vocab);
  session.active = true;
  session.authenticated = true;
  jh_test_bootloader_reset();
  injectRequest(44U, "REBOOT");
  jh_test_serial_tx_limit(0U);
  hal_serial_session_poll(&session);
  TEST_ASSERT_FALSE(jh_test_bootloader_requested());
  TEST_ASSERT_EQUAL(HAL_EAGAIN, hal_serial_session_tx_status(&session));
  size_t written = 0U;
  jh_test_serial_tx_limit(SIZE_MAX);
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(512U, &written));
  hal_serial_session_poll(&session);
  TEST_ASSERT_FALSE(jh_test_bootloader_requested());
  TEST_ASSERT_EQUAL(HAL_EAGAIN, hal_serial_session_tx_status(&session));
  for (unsigned i = 0U; i < 2U; ++i) {
    TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(512U, &written));
  }
  TEST_ASSERT_NOT_NULL(strstr(jh_test_serial_output(), "$SC,44,OK REBOOT*"));
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_session_tx_status(&session));
  hal_serial_session_poll(&session);
  TEST_ASSERT_TRUE(jh_test_bootloader_requested());
  assertStatus(0U, 0U);
}

static void test_reboot_proceeds_when_tx_never_drains(void) {
  static uint8_t storage[512];
  static const hal_serial_session_vocabulary_t vocab = {
      .cmd_reboot_bootloader = "REBOOT", .reply_reboot_ok = "OK REBOOT"};
  hal_serial_session_t session;
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_init(storage, sizeof(storage)));
  hal_serial_session_init_with_vocabulary(&session, "ECU", "224", "test",
                                          &vocab);
  session.active = true;
  session.authenticated = true;
  jh_test_bootloader_reset();
  jh_test_set_millis(1000U);
  injectRequest(45U, "REBOOT");
  hal_serial_session_poll(&session);
  size_t written = 0U;
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_poll(512U, &written));
  TEST_ASSERT_NOT_NULL(strstr(jh_test_serial_output(), "$SC,45,OK REBOOT*"));
  /* Later output stays queued because the host stopped reading. */
  TEST_ASSERT_EQUAL(HAL_OK, hal_serial_buffered_write("LOG\n", 4U));
  jh_test_serial_tx_limit(0U);
  jh_test_set_millis(1499U);
  hal_serial_session_poll(&session);
  TEST_ASSERT_FALSE(jh_test_bootloader_requested());
  jh_test_set_millis(1500U);
  hal_serial_session_poll(&session);
  TEST_ASSERT_TRUE(jh_test_bootloader_requested());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_stalled_and_partial_writes_resume_without_loss_or_repetition);
  RUN_TEST(test_overflow_discards_whole_messages_and_preserves_fifo);
  RUN_TEST(test_formatter_overflow_rolls_back_every_fragment);
  RUN_TEST(test_explicit_enqueue_returns_backpressure_without_counting_a_drop);
  RUN_TEST(test_session_retains_reply_under_backpressure);
  RUN_TEST(test_reboot_waits_for_queued_ack_to_reach_port);
  RUN_TEST(test_reboot_proceeds_when_tx_never_drains);
  return UNITY_END();
}
