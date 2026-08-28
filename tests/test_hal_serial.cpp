#include "hal/debug/hal_debug_format.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_system.h"
#include "utils/unity.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool fixed_ts_hook(char *out, size_t out_size, void *user) {
  (void)user;
  if (!out || out_size == 0) {
    return false;
  }
  snprintf(out, out_size, "T+1.234s");
  return true;
}

static bool rejecting_ts_hook(char *out, size_t out_size, void *user) {
  bool *invoked = (bool *)user;
  if (invoked != NULL) {
    *invoked = true;
  }
  if (out != NULL && out_size > 0u) {
    snprintf(out, out_size, "ignored timestamp");
  }
  return false;
}

static bool s_ts_hook_invoked = false;
static bool tracking_ts_hook(char *out, size_t out_size, void *user) {
  (void)user;
  s_ts_hook_invoked = true;
  if (!out || out_size == 0) {
    return false;
  }
  snprintf(out, out_size, "TS");
  return true;
}

typedef struct {
  char *data;
  size_t size;
  size_t len;
} capture_buffer_t;

static void capture_write(void *ctx, const char *data, size_t len) {
  capture_buffer_t *capture = (capture_buffer_t *)ctx;
  if (!capture || !capture->data || capture->size == 0u || !data || len == 0u) {
    return;
  }

  size_t room = capture->size - 1u - capture->len;
  size_t copy_len = len < room ? len : room;
  memcpy(capture->data + capture->len, data, copy_len);
  capture->len += copy_len;
  capture->data[capture->len] = '\0';
}

static void capture_format(capture_buffer_t *capture, const char *format, ...) {
  va_list args;
  va_start(args, format);
  hal_debug_vformat(capture_write, capture, format, args);
  va_end(args);
}

typedef struct {
  FILE *tmp;
  int saved_fd;
} stdout_capture_t;

static bool stdout_capture_begin(stdout_capture_t *capture) {
  if (capture == NULL) {
    return false;
  }

  capture->tmp = tmpfile();
  capture->saved_fd = -1;
  if (capture->tmp == NULL) {
    return false;
  }

  fflush(stdout);
  capture->saved_fd = dup(fileno(stdout));
  if (capture->saved_fd < 0) {
    fclose(capture->tmp);
    capture->tmp = NULL;
    return false;
  }

  if (dup2(fileno(capture->tmp), fileno(stdout)) < 0) {
    dup2(capture->saved_fd, fileno(stdout));
    close(capture->saved_fd);
    fclose(capture->tmp);
    capture->tmp = NULL;
    capture->saved_fd = -1;
    return false;
  }

  return true;
}

static size_t stdout_capture_end(stdout_capture_t *capture, char *out,
                                 size_t out_size) {
  if (out != NULL && out_size > 0u) {
    out[0] = '\0';
  }
  if (capture == NULL || capture->tmp == NULL || capture->saved_fd < 0) {
    return 0u;
  }

  fflush(stdout);
  dup2(capture->saved_fd, fileno(stdout));
  close(capture->saved_fd);
  capture->saved_fd = -1;

  size_t n = 0u;
  if (out != NULL && out_size > 0u) {
    rewind(capture->tmp);
    n = fread(out, 1u, out_size - 1u, capture->tmp);
    out[n] = '\0';
  }

  fclose(capture->tmp);
  capture->tmp = NULL;
  return n;
}

static void fill_payload(char *payload, size_t size, char ch) {
  if (payload == NULL || size == 0u) {
    return;
  }
  memset(payload, ch, size - 1u);
  payload[size - 1u] = '\0';
}

void setUp(void) {
  hal_mock_set_millis(0u);
  hal_mock_set_micros(0u);
  hal_debug_init(115200);
  hal_mock_serial_reset();
  hal_debug_set_muted(false);
  hal_debug_set_timestamp_hook(NULL, NULL);
  hal_mock_set_in_isr(false);
  hal_mock_debug_isr_restore_default_ring();
  hal_deb_set_prefix("");
  s_ts_hook_invoked = false;
}

void tearDown(void) {
  /* Always leave the mock in a sane state for the next test. */
  hal_mock_set_in_isr(false);
  hal_mock_debug_isr_restore_default_ring();
  hal_mock_debug_serial_full_reset();
  /* stdout_capture_end() closes a pipe fd through the intercepted close(),
   * which lazily creates the BSD sockets fd-table mutex as a side effect. */
  hal_mock_bsd_sockets_reset();
}

void test_println_captured(void) {
  hal_serial_println("hello");
  TEST_ASSERT_EQUAL_STRING("hello", hal_mock_serial_last_line());
}

void test_print_appends_and_println_starts_new_captured_line(void) {
  hal_serial_print("hel");
  hal_serial_print("lo");
  TEST_ASSERT_EQUAL_STRING("hello", hal_mock_serial_last_line());

  hal_serial_println("fresh");
  TEST_ASSERT_EQUAL_STRING("fresh", hal_mock_serial_last_line());
}

void test_serial_print_and_println_preserve_wire_message_boundaries(void) {
  char out[32];
  stdout_capture_t capture;
  TEST_ASSERT_TRUE(stdout_capture_begin(&capture));

  hal_serial_print("left");
  hal_serial_println("-right");

  size_t n = stdout_capture_end(&capture, out, sizeof(out));
  TEST_ASSERT_EQUAL_size_t(strlen("left-right\n"), n);
  TEST_ASSERT_EQUAL_STRING("left-right\n", out);
  TEST_ASSERT_EQUAL_STRING("-right", hal_mock_serial_last_line());
}

void test_serial_rx_preserves_binary_bytes_and_empty_read_contract(void) {
  const char rx[] = {'A', '\0', (char)0xff};
  hal_mock_serial_inject_rx(rx, (int)sizeof(rx));

  TEST_ASSERT_EQUAL_INT(3, hal_serial_available());
  TEST_ASSERT_EQUAL_INT('A', hal_serial_read());
  TEST_ASSERT_EQUAL_INT(2, hal_serial_available());
  TEST_ASSERT_EQUAL_INT(0, hal_serial_read());
  TEST_ASSERT_EQUAL_INT(0xff, hal_serial_read());
  TEST_ASSERT_EQUAL_INT(0, hal_serial_available());
  TEST_ASSERT_EQUAL_INT(-1, hal_serial_read());
}

void test_serial_null_strings_are_treated_as_empty(void) {
  hal_serial_print(NULL);
  TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());

  hal_serial_println(NULL);
  TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());
}

void test_serial_set_flush_is_portable_noop_on_mock(void) {
  hal_serial_set_flush(false);
  hal_serial_println("without flush");
  TEST_ASSERT_EQUAL_STRING("without flush", hal_mock_serial_last_line());

  hal_serial_set_flush(true);
  hal_serial_println("with flush");
  TEST_ASSERT_EQUAL_STRING("with flush", hal_mock_serial_last_line());
}

void test_println_overwrites_previous(void) {
  hal_serial_println("first");
  hal_serial_println("second");
  TEST_ASSERT_EQUAL_STRING("second", hal_mock_serial_last_line());
}

void test_reset_clears_last_line(void) {
  hal_serial_println("something");
  hal_mock_serial_reset();
  TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());
}

void test_deb_captured(void) {
  hal_deb("deb_test");
  TEST_ASSERT_EQUAL_STRING("deb_test", hal_mock_deb_last_line());
}

void test_task_debug_prefix_is_applied_with_single_separator(void) {
  hal_deb_set_prefix("[task]");
  hal_deb("value=%d", 7);

  TEST_ASSERT_EQUAL_STRING("[task] value=7", hal_mock_deb_last_line());
}

void test_derr_without_timestamp_hook(void) {
  hal_derr("plain error");
  const char *line = hal_mock_serial_last_line();
  TEST_ASSERT_NOT_NULL(strstr(line, "plain error"));
  TEST_ASSERT_NOT_NULL(strstr(line, "ERROR!"));
}

void test_derr_with_timestamp_hook(void) {
  hal_debug_set_timestamp_hook(fixed_ts_hook, NULL);
  hal_derr("hooked error");
  const char *line = hal_mock_serial_last_line();
  TEST_ASSERT_NOT_NULL(strstr(line, "[T+1.234s]"));
  TEST_ASSERT_NOT_NULL(strstr(line, "hooked error"));
}

void test_derr_omits_timestamp_when_hook_rejects_it(void) {
  bool invoked = false;
  hal_debug_set_timestamp_hook(rejecting_ts_hook, &invoked);

  hal_derr("untimestamped error");

  const char *line = hal_mock_serial_last_line();
  TEST_ASSERT_TRUE(invoked);
  TEST_ASSERT_NULL(strstr(line, "ignored timestamp"));
  TEST_ASSERT_NOT_NULL(strstr(line, "ERROR!"));
  TEST_ASSERT_NOT_NULL(strstr(line, "untimestamped error"));
}

void test_rate_limit_config_defaults_zero_sanitization_and_custom_values(void) {
  hal_debug_rate_limit_t defaults = hal_debug_rate_limit_defaults();
  TEST_ASSERT_EQUAL_UINT16(5u, defaults.full_logs_limit);
  TEST_ASSERT_EQUAL_UINT32(1000u, defaults.min_gap_ms);
  TEST_ASSERT_EQUAL_UINT32(30000u, defaults.summary_every_ms);

  const hal_debug_rate_limit_t zeros = {0u, 0u, 0u};
  hal_debug_init(115200u, &zeros);
  const hal_debug_rate_limit_t *active = hal_debug_get_rate_limit();
  TEST_ASSERT_NOT_NULL(active);
  TEST_ASSERT_EQUAL_UINT16(defaults.full_logs_limit, active->full_logs_limit);
  TEST_ASSERT_EQUAL_UINT32(defaults.min_gap_ms, active->min_gap_ms);
  TEST_ASSERT_EQUAL_UINT32(defaults.summary_every_ms, active->summary_every_ms);

  const hal_debug_rate_limit_t custom = {2u, 123u, 456u};
  hal_debug_init(115200u, &custom);
  active = hal_debug_get_rate_limit();
  TEST_ASSERT_EQUAL_UINT16(custom.full_logs_limit, active->full_logs_limit);
  TEST_ASSERT_EQUAL_UINT32(custom.min_gap_ms, active->min_gap_ms);
  TEST_ASSERT_EQUAL_UINT32(custom.summary_every_ms, active->summary_every_ms);
}

void test_rate_limiter_emits_notice_silence_and_periodic_summary(void) {
  const hal_debug_rate_limit_t config = {1u, 100u, 1000u};
  hal_debug_init(115200u, &config);

  hal_derr_limited("gps", "first failure");
  TEST_ASSERT_NOT_NULL(strstr(hal_mock_serial_last_line(), "[gps]"));
  TEST_ASSERT_NOT_NULL(strstr(hal_mock_serial_last_line(), "first failure"));

  hal_derr_limited("gps", "second failure");
  TEST_ASSERT_NOT_NULL(strstr(hal_mock_serial_last_line(),
                              "repeated errors are being suppressed"));

  char out[16];
  stdout_capture_t capture;
  TEST_ASSERT_TRUE(stdout_capture_begin(&capture));
  hal_derr_limited("gps", "third failure");
  size_t n = stdout_capture_end(&capture, out, sizeof(out));
  TEST_ASSERT_EQUAL_size_t(0u, n);

  hal_mock_advance_millis(1000u);
  hal_derr_limited("gps", "fourth failure");
  const char *summary = hal_mock_serial_last_line();
  TEST_ASSERT_NOT_NULL(strstr(summary, "[gps]"));
  TEST_ASSERT_NOT_NULL(strstr(summary, "suppressed 3 repeated errors"));
  TEST_ASSERT_NOT_NULL(strstr(summary, "last 1000 ms"));
}

void test_rate_limiter_tracks_sources_independently(void) {
  const hal_debug_rate_limit_t config = {1u, 1000u, 30000u};
  hal_debug_init(115200u, &config);

  hal_derr_limited("source-a", "A1");
  hal_derr_limited("source-a", "A2");
  hal_derr_limited("source-b", "B1");

  const char *line = hal_mock_serial_last_line();
  TEST_ASSERT_NOT_NULL(strstr(line, "[source-b]"));
  TEST_ASSERT_NOT_NULL(strstr(line, "B1"));
  TEST_ASSERT_NULL(strstr(line, "suppressed"));
}

void test_debug_muting_suppresses_debug_logs_but_not_serial_protocol_output(
    void) {
  hal_debug_set_muted(true);
  TEST_ASSERT_TRUE(hal_debug_is_muted());

  hal_deb("muted deb");
  hal_derr("muted err");
  TEST_ASSERT_EQUAL_STRING("", hal_mock_deb_last_line());
  TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());

  hal_serial_println("proto line");
  TEST_ASSERT_EQUAL_STRING("proto line", hal_mock_serial_last_line());

  hal_debug_set_muted(false);
  TEST_ASSERT_FALSE(hal_debug_is_muted());
  hal_deb("after unmute");
  TEST_ASSERT_EQUAL_STRING("after unmute", hal_mock_deb_last_line());
}

void test_debug_formatter_streams_beyond_hal_debug_buf_size(void) {
  char payload[HAL_DEBUG_BUF_SIZE + 257u];
  memset(payload, 'A', sizeof(payload) - 1u);
  payload[sizeof(payload) - 1u] = '\0';

  char out[sizeof(payload) + 32u];
  capture_buffer_t capture = {out, sizeof(out), 0u};
  out[0] = '\0';

  capture_format(&capture, "prefix:%s:suffix", payload);

  TEST_ASSERT_EQUAL_size_t(
      strlen("prefix:") + strlen(payload) + strlen(":suffix"), capture.len);
  TEST_ASSERT_NOT_NULL(strstr(out, "prefix:"));
  TEST_ASSERT_NOT_NULL(strstr(out, ":suffix"));
}

void test_debug_formatter_handles_common_printf_conversions(void) {
  char out[128];
  capture_buffer_t capture = {out, sizeof(out), 0u};
  out[0] = '\0';

  capture_format(&capture,
                 "n=%d hex=%04x str=%8.3s dyn=%*.*s ch=%c float=%.2f pct=%%",
                 -42, 0x2a, "abcdef", 6, 2, "wxyz", 'Z', 25.25);

  TEST_ASSERT_EQUAL_STRING(
      "n=-42 hex=002a str=     abc dyn=    wx ch=Z float=25.25 pct=%", out);
}

void test_hal_deb_streams_payload_beyond_mock_capture_buffer(void) {
  char payload[HAL_DEBUG_BUF_SIZE + 257u];
  fill_payload(payload, sizeof(payload), 'B');

  char out[sizeof(payload) + 96u];
  stdout_capture_t capture;
  TEST_ASSERT_TRUE(stdout_capture_begin(&capture));
  hal_deb("begin:%s:tail", payload);
  size_t n = stdout_capture_end(&capture, out, sizeof(out));

  TEST_ASSERT_TRUE(n > HAL_DEBUG_BUF_SIZE);
  TEST_ASSERT_NOT_NULL(strstr(out, "begin:"));
  TEST_ASSERT_NOT_NULL(strstr(out, ":tail\n"));
  TEST_ASSERT_EQUAL_size_t(HAL_DEBUG_BUF_SIZE - 1u,
                           strlen(hal_mock_deb_last_line()));
  TEST_ASSERT_NULL(strstr(hal_mock_deb_last_line(), ":tail"));
}

void test_hal_derr_streams_payload_beyond_mock_capture_buffer(void) {
  char payload[HAL_DEBUG_BUF_SIZE + 257u];
  fill_payload(payload, sizeof(payload), 'E');

  char out[sizeof(payload) + 160u];
  stdout_capture_t capture;
  hal_debug_set_timestamp_hook(fixed_ts_hook, NULL);
  TEST_ASSERT_TRUE(stdout_capture_begin(&capture));
  hal_derr("err:%s:tail", payload);
  size_t n = stdout_capture_end(&capture, out, sizeof(out));

  TEST_ASSERT_TRUE(n > HAL_DEBUG_BUF_SIZE);
  TEST_ASSERT_NOT_NULL(strstr(out, "[T+1.234s]"));
  TEST_ASSERT_NOT_NULL(strstr(out, "ERROR!"));
  TEST_ASSERT_NOT_NULL(strstr(out, "err:"));
  TEST_ASSERT_NOT_NULL(strstr(out, ":tail\n"));
  TEST_ASSERT_EQUAL_size_t(HAL_DEBUG_BUF_SIZE - 1u,
                           strlen(hal_mock_serial_last_line()));
  TEST_ASSERT_NULL(strstr(hal_mock_serial_last_line(), ":tail"));
}

void test_hal_derr_limited_streams_full_log_beyond_mock_capture_buffer(void) {
  char payload[HAL_DEBUG_BUF_SIZE + 257u];
  fill_payload(payload, sizeof(payload), 'L');

  char out[sizeof(payload) + 160u];
  stdout_capture_t capture;
  TEST_ASSERT_TRUE(stdout_capture_begin(&capture));
  hal_derr_limited("serial-long", "limited:%s:tail", payload);
  size_t n = stdout_capture_end(&capture, out, sizeof(out));

  TEST_ASSERT_TRUE(n > HAL_DEBUG_BUF_SIZE);
  TEST_ASSERT_NOT_NULL(strstr(out, "ERROR!"));
  TEST_ASSERT_NOT_NULL(strstr(out, "[serial-long]"));
  TEST_ASSERT_NOT_NULL(strstr(out, "limited:"));
  TEST_ASSERT_NOT_NULL(strstr(out, ":tail\n"));
  TEST_ASSERT_EQUAL_size_t(HAL_DEBUG_BUF_SIZE - 1u,
                           strlen(hal_mock_serial_last_line()));
  TEST_ASSERT_NULL(strstr(hal_mock_serial_last_line(), ":tail"));
}

// ── ISR-deferred debug ring tests ────────────────────────────────────────────

void test_in_isr_mock_default_is_false(void) {
  TEST_ASSERT_FALSE(hal_in_isr());
}

void test_in_isr_mock_toggle(void) {
  hal_mock_set_in_isr(true);
  TEST_ASSERT_TRUE(hal_in_isr());
  hal_mock_set_in_isr(false);
  TEST_ASSERT_FALSE(hal_in_isr());
}

void test_default_ring_capacity_matches_compile_time_define(void) {
  TEST_ASSERT_EQUAL_size_t(HAL_DEBUG_ISR_SLOT_COUNT,
                           hal_mock_debug_isr_capacity());
}

void test_ring_starts_empty(void) {
  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_debug_isr_used_slots());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_debug_isr_dropped());
}

void test_isr_deb_does_not_emit_immediately(void) {
  hal_mock_set_in_isr(true);
  hal_deb("from isr deb");
  /* Nothing must reach the serial transport while we are in ISR. */
  TEST_ASSERT_EQUAL_STRING("", hal_mock_deb_last_line());
  TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());
  /* But the ring must contain exactly one pending record. */
  TEST_ASSERT_EQUAL_size_t(1u, hal_mock_debug_isr_used_slots());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_debug_isr_dropped());
}

void test_isr_derr_does_not_emit_immediately(void) {
  hal_mock_set_in_isr(true);
  hal_derr("from isr err");
  TEST_ASSERT_EQUAL_STRING("", hal_mock_deb_last_line());
  TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());
  TEST_ASSERT_EQUAL_size_t(1u, hal_mock_debug_isr_used_slots());
}

void test_isr_derr_limited_does_not_emit_immediately(void) {
  hal_mock_set_in_isr(true);
  hal_derr_limited("src", "limited from isr");
  TEST_ASSERT_EQUAL_STRING("", hal_mock_deb_last_line());
  TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());
  TEST_ASSERT_EQUAL_size_t(1u, hal_mock_debug_isr_used_slots());
}

void test_debug_loop_when_empty_is_noop(void) {
  hal_debug_loop();
  TEST_ASSERT_EQUAL_STRING("", hal_mock_deb_last_line());
  TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());
}

void test_debug_loop_drains_deb_record(void) {
  hal_mock_set_in_isr(true);
  hal_deb("hello %d", 42);
  TEST_ASSERT_EQUAL_size_t(1u, hal_mock_debug_isr_used_slots());

  hal_mock_set_in_isr(false);
  hal_debug_loop();

  /* Ring must be empty after drain. */
  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_debug_isr_used_slots());
  /* Drained deb record updates the mock's last-deb-line capture. */
  const char *line = hal_mock_deb_last_line();
  TEST_ASSERT_NOT_NULL(strstr(line, "[ISR ts="));
  TEST_ASSERT_NOT_NULL(strstr(line, "hello 42"));
}

void test_debug_loop_drains_derr_record_with_error_prefix(void) {
  hal_mock_set_in_isr(true);
  hal_derr("bad thing %s", "X");
  hal_mock_set_in_isr(false);
  hal_debug_loop();

  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_debug_isr_used_slots());
  const char *line = hal_mock_serial_last_line();
  TEST_ASSERT_NOT_NULL(strstr(line, "ERROR!"));
  TEST_ASSERT_NOT_NULL(strstr(line, "[ISR ts="));
  TEST_ASSERT_NOT_NULL(strstr(line, "bad thing X"));
}

void test_debug_loop_applies_user_prefix_to_deb(void) {
  hal_deb_set_prefix("[ECU]");
  hal_mock_set_in_isr(true);
  hal_deb("payload");
  hal_mock_set_in_isr(false);
  hal_debug_loop();

  const char *line = hal_mock_deb_last_line();
  TEST_ASSERT_NOT_NULL(strstr(line, "[ECU]"));
  TEST_ASSERT_NOT_NULL(strstr(line, "[ISR ts="));
  TEST_ASSERT_NOT_NULL(strstr(line, "payload"));
}

void test_debug_loop_preserves_fifo_order(void) {
  hal_mock_set_in_isr(true);
  hal_deb("one");
  hal_deb("two");
  hal_deb("three");
  TEST_ASSERT_EQUAL_size_t(3u, hal_mock_debug_isr_used_slots());

  hal_mock_set_in_isr(false);
  hal_debug_loop();
  /* The last emitted line must be the last pushed record. */
  const char *line = hal_mock_deb_last_line();
  TEST_ASSERT_NOT_NULL(strstr(line, "three"));
  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_debug_isr_used_slots());
}

void test_debug_loop_in_isr_is_noop(void) {
  /* Push something first from ISR... */
  hal_mock_set_in_isr(true);
  hal_deb("queued");
  /* ...then call drain while still in ISR. Drain must refuse to run
   * so the ring stays full and the serial path is untouched. */
  hal_debug_loop();
  TEST_ASSERT_EQUAL_size_t(1u, hal_mock_debug_isr_used_slots());
  TEST_ASSERT_EQUAL_STRING("", hal_mock_deb_last_line());
}

void test_isr_derr_limited_bakes_source_into_text(void) {
  hal_mock_set_in_isr(true);
  hal_derr_limited("gps", "ublox fix lost");
  hal_mock_set_in_isr(false);
  hal_debug_loop();

  const char *line = hal_mock_serial_last_line();
  TEST_ASSERT_NOT_NULL(strstr(line, "ERROR!"));
  TEST_ASSERT_NOT_NULL(strstr(line, "[gps]"));
  TEST_ASSERT_NOT_NULL(strstr(line, "ublox fix lost"));
}

void test_isr_derr_limited_null_source_becomes_global(void) {
  hal_mock_set_in_isr(true);
  hal_derr_limited(NULL, "no source");
  hal_mock_set_in_isr(false);
  hal_debug_loop();

  const char *line = hal_mock_serial_last_line();
  TEST_ASSERT_NOT_NULL(strstr(line, "[global]"));
  TEST_ASSERT_NOT_NULL(strstr(line, "no source"));
}

void test_isr_derr_does_not_invoke_timestamp_hook(void) {
  hal_debug_set_timestamp_hook(tracking_ts_hook, NULL);
  hal_mock_set_in_isr(true);
  hal_derr("from isr");
  TEST_ASSERT_FALSE(s_ts_hook_invoked);

  /* Drain runs the underlying path but our ISR-deferred drain bakes
   * its own [ISR ts=..] marker and intentionally does NOT call the
   * hook (would emit a misleading "current time" for a past event). */
  hal_mock_set_in_isr(false);
  s_ts_hook_invoked = false;
  hal_debug_loop();
  TEST_ASSERT_FALSE(s_ts_hook_invoked);
}

void test_isr_long_text_is_truncated_not_overflowed(void) {
  /* Build a payload that overflows HAL_DEBUG_ISR_TEXT_MAX. */
  char big[HAL_DEBUG_ISR_TEXT_MAX * 2];
  memset(big, 'A', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';

  hal_mock_set_in_isr(true);
  hal_deb("%s", big);
  hal_mock_set_in_isr(false);
  hal_debug_loop();

  /* Drain must not crash and the captured line is bounded by the
   * mock's HAL_DEBUG_BUF_SIZE. We just verify some 'A's made it. */
  const char *line = hal_mock_deb_last_line();
  TEST_ASSERT_TRUE(strlen(line) > 16u);
  TEST_ASSERT_NOT_NULL(strchr(line, 'A'));
}

void test_isr_path_does_not_trigger_lazy_init_after_reset(void) {
  /* This test exercises the "no lazy init in ISR" rule. We cannot
   * un-initialise the debug subsystem (no public API), so we assert
   * the related invariant: pushing from ISR never touches the rate
   * limiter mutex (it would if it took the non-ISR path) and never
   * emits to serial. */
  hal_mock_set_in_isr(true);
  for (int i = 0; i < 3; ++i) {
    hal_derr_limited("isolated", "msg %d", i);
  }
  TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());
  /* All 3 enqueued: rate limiter is bypassed in ISR. */
  TEST_ASSERT_EQUAL_size_t(3u, hal_mock_debug_isr_used_slots());
}

void test_muted_in_isr_drops_silently_no_drop_counter(void) {
  hal_debug_set_muted(true);
  hal_mock_set_in_isr(true);
  hal_deb("muted isr");
  hal_derr("muted isr err");
  hal_derr_limited("src", "muted isr limited");

  /* Muted ISR calls must NOT touch the ring at all (and especially
   * must not bump the dropped counter - that's reserved for overflow). */
  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_debug_isr_used_slots());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_debug_isr_dropped());
}

void test_muted_drain_swallows_pending_and_clears_dropped(void) {
  /* Queue some real records first... */
  hal_mock_set_in_isr(true);
  hal_deb("a");
  hal_deb("b");
  hal_mock_set_in_isr(false);
  /* ...then mute and drain. Drain must discard them silently and
   * also clear the dropped counter so the next loop iteration is
   * clean. */
  hal_debug_set_muted(true);
  hal_debug_loop();
  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_debug_isr_used_slots());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_debug_isr_dropped());
  TEST_ASSERT_EQUAL_STRING("", hal_mock_deb_last_line());
}

void test_ring_overflow_drops_and_increments_counter(void) {
  /* Shrink to capacity = 4 (effective = 3). */
  hal_mock_debug_isr_set_test_capacity(4u);
  TEST_ASSERT_EQUAL_size_t(4u, hal_mock_debug_isr_capacity());

  hal_mock_set_in_isr(true);
  /* Fill 3 effective slots... */
  hal_deb("rec0");
  hal_deb("rec1");
  hal_deb("rec2");
  TEST_ASSERT_EQUAL_size_t(3u, hal_mock_debug_isr_used_slots());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_debug_isr_dropped());

  /* ...then push 2 more - both must be dropped. */
  hal_deb("rec3-dropped");
  hal_deb("rec4-dropped");
  TEST_ASSERT_EQUAL_size_t(3u, hal_mock_debug_isr_used_slots());
  TEST_ASSERT_EQUAL_UINT32(2u, hal_mock_debug_isr_dropped());
}

void test_drain_emits_dropped_summary_and_clears_counter(void) {
  hal_mock_debug_isr_set_test_capacity(3u); // effective = 2

  hal_mock_set_in_isr(true);
  hal_deb("kept0");
  hal_deb("kept1");
  hal_deb("dropped0");
  hal_deb("dropped1");
  hal_deb("dropped2");
  TEST_ASSERT_EQUAL_UINT32(3u, hal_mock_debug_isr_dropped());

  hal_mock_set_in_isr(false);
  hal_debug_loop();

  /* After drain the counter must be zero (consumed and reported). */
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_debug_isr_dropped());
  /* And the very last emitted serial line must be the drop summary. */
  const char *line = hal_mock_serial_last_line();
  TEST_ASSERT_NOT_NULL(strstr(line, "ERROR!"));
  TEST_ASSERT_NOT_NULL(strstr(line, "[ISR]"));
  TEST_ASSERT_NOT_NULL(strstr(line, "dropped 3"));
}

void test_drain_without_drops_does_not_emit_summary(void) {
  hal_mock_set_in_isr(true);
  hal_deb("only");
  hal_mock_set_in_isr(false);
  hal_debug_loop();
  /* Last serial line must be the drained record, not a drop summary. */
  const char *line = hal_mock_serial_last_line();
  TEST_ASSERT_NULL(strstr(line, "dropped"));
}

void test_ring_recovers_after_drain_following_overflow(void) {
  hal_mock_debug_isr_set_test_capacity(3u); // effective = 2

  hal_mock_set_in_isr(true);
  hal_deb("a");
  hal_deb("b");
  hal_deb("c-drop");
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_debug_isr_dropped());

  hal_mock_set_in_isr(false);
  hal_debug_loop();
  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_debug_isr_used_slots());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_debug_isr_dropped());

  /* Ring usable again. */
  hal_mock_set_in_isr(true);
  hal_deb("after");
  hal_deb("drain");
  TEST_ASSERT_EQUAL_size_t(2u, hal_mock_debug_isr_used_slots());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_debug_isr_dropped());
}

void test_ring_reset_clears_pending(void) {
  hal_mock_set_in_isr(true);
  hal_deb("a");
  hal_deb("b");
  TEST_ASSERT_EQUAL_size_t(2u, hal_mock_debug_isr_used_slots());

  hal_mock_debug_isr_reset();
  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_debug_isr_used_slots());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_debug_isr_dropped());
}

void test_ring_wraps_around_and_keeps_fifo(void) {
  hal_mock_debug_isr_set_test_capacity(4u); // effective = 3

  hal_mock_set_in_isr(true);
  /* Push 2, drain 2 (advances tail) so subsequent pushes will wrap. */
  hal_deb("first0");
  hal_deb("first1");
  hal_mock_set_in_isr(false);
  hal_debug_loop();
  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_debug_isr_used_slots());

  /* Now push 3 more (head will pass the buffer end). */
  hal_mock_set_in_isr(true);
  hal_deb("wrap0");
  hal_deb("wrap1");
  hal_deb("wrap2");
  TEST_ASSERT_EQUAL_size_t(3u, hal_mock_debug_isr_used_slots());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_debug_isr_dropped());

  hal_mock_set_in_isr(false);
  hal_debug_loop();
  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_debug_isr_used_slots());
  const char *line = hal_mock_deb_last_line();
  TEST_ASSERT_NOT_NULL(strstr(line, "wrap2"));
}

void test_test_capacity_clamps_to_minimum(void) {
  hal_mock_debug_isr_set_test_capacity(0u);
  TEST_ASSERT_EQUAL_size_t(2u, hal_mock_debug_isr_capacity());
}

void test_restore_default_ring_brings_back_full_capacity(void) {
  hal_mock_debug_isr_set_test_capacity(2u);
  TEST_ASSERT_EQUAL_size_t(2u, hal_mock_debug_isr_capacity());
  hal_mock_debug_isr_restore_default_ring();
  TEST_ASSERT_EQUAL_size_t(HAL_DEBUG_ISR_SLOT_COUNT,
                           hal_mock_debug_isr_capacity());
}

void test_non_isr_path_does_not_touch_ring(void) {
  /* Sanity: when not in ISR, the normal path is taken and the ring
   * must stay completely untouched. */
  hal_mock_set_in_isr(false);
  hal_deb("normal");
  hal_derr("normal err");
  hal_derr_limited("src", "normal limited");
  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_debug_isr_used_slots());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_mock_debug_isr_dropped());
}

void test_mixed_isr_and_non_isr_calls_keep_ring_isolated(void) {
  /* Non-ISR call must emit directly. */
  hal_deb("direct");
  TEST_ASSERT_EQUAL_STRING("direct", hal_mock_deb_last_line());
  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_debug_isr_used_slots());

  /* ISR call only goes to the ring. */
  hal_mock_set_in_isr(true);
  hal_deb("queued");
  TEST_ASSERT_EQUAL_size_t(1u, hal_mock_debug_isr_used_slots());
  /* Direct line is still the last captured deb-line until drain. */
  TEST_ASSERT_EQUAL_STRING("direct", hal_mock_deb_last_line());

  hal_mock_set_in_isr(false);
  hal_debug_loop();
  TEST_ASSERT_NOT_NULL(strstr(hal_mock_deb_last_line(), "queued"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_println_captured);
  RUN_TEST(test_print_appends_and_println_starts_new_captured_line);
  RUN_TEST(test_serial_print_and_println_preserve_wire_message_boundaries);
  RUN_TEST(test_serial_rx_preserves_binary_bytes_and_empty_read_contract);
  RUN_TEST(test_serial_null_strings_are_treated_as_empty);
  RUN_TEST(test_serial_set_flush_is_portable_noop_on_mock);
  RUN_TEST(test_println_overwrites_previous);
  RUN_TEST(test_reset_clears_last_line);
  RUN_TEST(test_deb_captured);
  RUN_TEST(test_task_debug_prefix_is_applied_with_single_separator);
  RUN_TEST(test_derr_without_timestamp_hook);
  RUN_TEST(test_derr_with_timestamp_hook);
  RUN_TEST(test_derr_omits_timestamp_when_hook_rejects_it);
  RUN_TEST(test_rate_limit_config_defaults_zero_sanitization_and_custom_values);
  RUN_TEST(test_rate_limiter_emits_notice_silence_and_periodic_summary);
  RUN_TEST(test_rate_limiter_tracks_sources_independently);
  RUN_TEST(
      test_debug_muting_suppresses_debug_logs_but_not_serial_protocol_output);
  RUN_TEST(test_debug_formatter_streams_beyond_hal_debug_buf_size);
  RUN_TEST(test_debug_formatter_handles_common_printf_conversions);
  RUN_TEST(test_hal_deb_streams_payload_beyond_mock_capture_buffer);
  RUN_TEST(test_hal_derr_streams_payload_beyond_mock_capture_buffer);
  RUN_TEST(test_hal_derr_limited_streams_full_log_beyond_mock_capture_buffer);

  /* ISR-deferred debug ring */
  RUN_TEST(test_in_isr_mock_default_is_false);
  RUN_TEST(test_in_isr_mock_toggle);
  RUN_TEST(test_default_ring_capacity_matches_compile_time_define);
  RUN_TEST(test_ring_starts_empty);
  RUN_TEST(test_isr_deb_does_not_emit_immediately);
  RUN_TEST(test_isr_derr_does_not_emit_immediately);
  RUN_TEST(test_isr_derr_limited_does_not_emit_immediately);
  RUN_TEST(test_debug_loop_when_empty_is_noop);
  RUN_TEST(test_debug_loop_drains_deb_record);
  RUN_TEST(test_debug_loop_drains_derr_record_with_error_prefix);
  RUN_TEST(test_debug_loop_applies_user_prefix_to_deb);
  RUN_TEST(test_debug_loop_preserves_fifo_order);
  RUN_TEST(test_debug_loop_in_isr_is_noop);
  RUN_TEST(test_isr_derr_limited_bakes_source_into_text);
  RUN_TEST(test_isr_derr_limited_null_source_becomes_global);
  RUN_TEST(test_isr_derr_does_not_invoke_timestamp_hook);
  RUN_TEST(test_isr_long_text_is_truncated_not_overflowed);
  RUN_TEST(test_isr_path_does_not_trigger_lazy_init_after_reset);
  RUN_TEST(test_muted_in_isr_drops_silently_no_drop_counter);
  RUN_TEST(test_muted_drain_swallows_pending_and_clears_dropped);
  RUN_TEST(test_ring_overflow_drops_and_increments_counter);
  RUN_TEST(test_drain_emits_dropped_summary_and_clears_counter);
  RUN_TEST(test_drain_without_drops_does_not_emit_summary);
  RUN_TEST(test_ring_recovers_after_drain_following_overflow);
  RUN_TEST(test_ring_reset_clears_pending);
  RUN_TEST(test_ring_wraps_around_and_keeps_fifo);
  RUN_TEST(test_test_capacity_clamps_to_minimum);
  RUN_TEST(test_restore_default_ring_brings_back_full_capacity);
  RUN_TEST(test_non_isr_path_does_not_touch_ring);
  RUN_TEST(test_mixed_isr_and_non_isr_calls_keep_ring_isolated);
  return UNITY_END();
}
