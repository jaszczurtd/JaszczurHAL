#include "utils/unity.h"
#include "hal/hal_modem_at.h"
#include "hal/hal_uart.h"
#include "hal/impl/.mock/hal_mock.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static hal_uart_t      s_uart   = NULL;
static hal_modem_at_t  s_modem  = NULL;
static char            s_rxbuf[512];

static void push_str(const char *s) {
    hal_mock_uart_push(s_uart, (const uint8_t *)s, (int)strlen(s));
}

void setUp(void) {
    s_uart = hal_uart_create(HAL_UART_PORT_2, 5, 4);
    hal_mock_uart_reset(s_uart);

    memset(s_rxbuf, 0, sizeof(s_rxbuf));
    hal_modem_at_config_t cfg = {0};
    cfg.uart               = s_uart;
    cfg.rx_buf             = s_rxbuf;
    cfg.rx_buf_size        = sizeof(s_rxbuf);
    cfg.default_timeout_ms = 200;
    cfg.quiet_window_ms    = 50;
    s_modem = hal_modem_at_create(&cfg);
}

void tearDown(void) {
    hal_modem_at_destroy(s_modem);
    s_modem = NULL;
    hal_uart_destroy(s_uart);
    s_uart = NULL;
}

/* ── create / destroy ─────────────────────────────────────────────────── */

void test_create_rejects_null_cfg(void) {
    TEST_ASSERT_NULL(hal_modem_at_create(NULL));
}

void test_create_rejects_missing_uart(void) {
    hal_modem_at_config_t cfg = {0};
    cfg.rx_buf = s_rxbuf;
    cfg.rx_buf_size = sizeof(s_rxbuf);
    TEST_ASSERT_NULL(hal_modem_at_create(&cfg));
}

void test_create_rejects_tiny_buffer(void) {
    char tiny[16];
    hal_modem_at_config_t cfg = {0};
    cfg.uart = s_uart;
    cfg.rx_buf = tiny;
    cfg.rx_buf_size = sizeof(tiny);
    TEST_ASSERT_NULL(hal_modem_at_create(&cfg));
}

void test_destroy_null_handle_safe(void) {
    hal_modem_at_destroy(NULL); /* must not crash */
}

/* ── send: OK / ERROR / expected / timeout ───────────────────────────── */

void test_send_ok(void) {
    push_str("\r\nOK\r\n");
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_OK, hal_modem_at_send(s_modem, "AT", NULL, 100));
    TEST_ASSERT_EQUAL_STRING("AT\r\n", hal_mock_uart_last_write(s_uart));
}

void test_send_error(void) {
    push_str("\r\nERROR\r\n");
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_ERROR, hal_modem_at_send(s_modem, "AT+FAIL", NULL, 100));
}

void test_send_cme_error_classified_as_error(void) {
    push_str("\r\n+CME ERROR: 100\r\n");
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_ERROR, hal_modem_at_send(s_modem, "AT+CPIN?", NULL, 100));
}

void test_send_expected_substring_matches_before_ok(void) {
    push_str("\r\n+CSQ: 21,99\r\n");
    /* No OK present, but expected matches early. */
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_OK, hal_modem_at_send(s_modem, "AT+CSQ", "+CSQ:", 100));
    const char *resp = hal_modem_at_last_response(s_modem);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_NOT_NULL(strstr(resp, "+CSQ: 21,99"));
}

void test_send_timeout_when_no_terminator(void) {
    push_str("\r\nrandom noise\r\n");
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_TIMEOUT,
                      hal_modem_at_send(s_modem, "AT", NULL, 50));
}

void test_send_invalid_args(void) {
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_INVALID_ARG,
                      hal_modem_at_send(NULL, "AT", NULL, 0));
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_INVALID_ARG,
                      hal_modem_at_send(s_modem, NULL, NULL, 0));
}

void test_send_drains_stale_urcs_before_command(void) {
    /* Stale URC sitting in UART before the command is issued. */
    push_str("\r\n+CREG: 1\r\n");
    push_str("\r\nOK\r\n");
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_OK, hal_modem_at_send(s_modem, "AT", NULL, 100));
}

void test_send_expected_waits_past_early_ok(void) {
    /* Regression: SimCom CMQTT* and similar reply with "OK" first and
       emit the actual result code as an asynchronous URC ("+CMQTTSUB:
       0,0" etc.) a moment later.  When the caller passes an `expected`
       substring, the engine MUST wait for that substring and NOT
       short-circuit on the bare "\r\nOK\r\n" - otherwise it races the
       URC and fires the next command while the modem is still busy. */
    push_str("\r\nOK\r\n");
    /* expected is given but not yet present -> must time out, not OK. */
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_TIMEOUT,
                      hal_modem_at_send(s_modem, "AT+CMQTTSUB=0",
                                        "+CMQTTSUB: 0,0", 50));
}

void test_send_expected_drains_trailing_ok(void) {
    /* Regression: when `expected` is a payload substring that arrives
       BEFORE the OK terminator (e.g. "+CCLK:" preceding "\r\nOK\r\n"),
       send() must not return the instant it sees the payload - it has
       to keep draining until OK arrives, otherwise the trailing bytes
       (rest of the payload line + "\r\nOK\r\n") leak into the UART FIFO
       and corrupt the next command's RX buffer.

       Simulate: payload arrives first, then OK arrives a poll later. */
    push_str("\r\n+CCLK: \"26/06/02,20:40:25+08\"\r\n\r\nOK\r\n");
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_OK,
                      hal_modem_at_send(s_modem, "AT+CCLK?", "+CCLK:", 500));
    /* Issue a follow-up command - its RX buffer must NOT contain the
       trailing "OK" of the previous response. */
    push_str("\r\nOK\r\n");
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_OK,
                      hal_modem_at_send(s_modem, "AT", NULL, 100));
}

/* ── send_with_data: prompt + payload + OK ───────────────────────────── */

void test_send_with_data_happy_path(void) {
    /* Modem replies with '>' prompt, then OK after payload. */
    push_str("> \r\nOK\r\n");
    const uint8_t payload[] = "hello";
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_OK,
                      hal_modem_at_send_with_data(s_modem,
                                                  "AT+CMQTTPAYLOAD=0,5",
                                                  payload, 5,
                                                  100, 100));
    /* TX captured was the payload (last_write overwritten by hal_uart_write). */
    const char *last = hal_mock_uart_last_write(s_uart);
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_STRING_LEN("hello", last, 5);
}

void test_send_with_data_no_prompt(void) {
    push_str("\r\nERROR\r\n");
    const uint8_t payload[] = "x";
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_NO_PROMPT,
                      hal_modem_at_send_with_data(s_modem,
                                                  "AT+CMQTTPAYLOAD=0,1",
                                                  payload, 1,
                                                  50, 50));
}

void test_send_with_data_invalid_payload_pointer(void) {
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_INVALID_ARG,
                      hal_modem_at_send_with_data(s_modem,
                                                  "AT+X", NULL, 5,
                                                  100, 100));
}

/* ── listen_until: predicate + quiet window ──────────────────────────── */

static bool ready_contains_pb_done(const char *buf, size_t len, void *user) {
    (void)len; (void)user;
    return strstr(buf, "PB DONE") != NULL;
}

void test_listen_until_predicate_fires(void) {
    push_str("\r\n*ATREADY: 1\r\n+CPIN: READY\r\nSMS DONE\r\nPB DONE\r\n");
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_OK,
                      hal_modem_at_listen_until(s_modem,
                                                ready_contains_pb_done,
                                                NULL, 500));
}

void test_listen_until_settles_when_no_predicate(void) {
    push_str("\r\nsome boot noise\r\n");
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_OK,
                      hal_modem_at_listen_until(s_modem, NULL, NULL, 500));
}

void test_listen_until_times_out_when_predicate_never_fires(void) {
    push_str("\r\nirrelevant\r\n");
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_TIMEOUT,
                      hal_modem_at_listen_until(s_modem,
                                                ready_contains_pb_done,
                                                NULL, 100));
}

/* ── URC router ──────────────────────────────────────────────────────── */

static int s_urc_calls = 0;
static char s_last_urc[128];

static void capture_urc(const char *line, void *user) {
    (void)user;
    s_urc_calls++;
    strncpy(s_last_urc, line, sizeof(s_last_urc) - 1);
    s_last_urc[sizeof(s_last_urc) - 1] = '\0';
}

void test_urc_dispatch_during_send(void) {
    s_urc_calls = 0;
    s_last_urc[0] = '\0';

    TEST_ASSERT_TRUE(hal_modem_at_urc_register(s_modem, "+CMQTTRXSTART:",
                                               capture_urc, NULL));

    push_str("\r\n+CMQTTRXSTART: 0,5,3\r\nOK\r\n");
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_OK, hal_modem_at_send(s_modem, "AT+POLL", NULL, 100));
    TEST_ASSERT_EQUAL_INT(1, s_urc_calls);
    TEST_ASSERT_EQUAL_STRING("+CMQTTRXSTART: 0,5,3", s_last_urc);
}

void test_urc_register_rejects_invalid(void) {
    TEST_ASSERT_FALSE(hal_modem_at_urc_register(NULL, "+X", capture_urc, NULL));
    TEST_ASSERT_FALSE(hal_modem_at_urc_register(s_modem, NULL, capture_urc, NULL));
    TEST_ASSERT_FALSE(hal_modem_at_urc_register(s_modem, "", capture_urc, NULL));
}

void test_urc_register_unregister_with_null_cb(void) {
    s_urc_calls = 0;
    TEST_ASSERT_TRUE(hal_modem_at_urc_register(s_modem, "+CREG:", capture_urc, NULL));
    TEST_ASSERT_TRUE(hal_modem_at_urc_register(s_modem, "+CREG:", NULL, NULL));
    push_str("\r\n+CREG: 1\r\nOK\r\n");
    (void)hal_modem_at_send(s_modem, "AT", NULL, 100);
    TEST_ASSERT_EQUAL_INT(0, s_urc_calls);
}

void test_urc_poll_drains_unsolicited(void) {
    s_urc_calls = 0;
    TEST_ASSERT_TRUE(hal_modem_at_urc_register(s_modem, "+CMQTTRXEND:",
                                               capture_urc, NULL));
    push_str("\r\n+CMQTTRXEND: 0\r\n");
    int n = hal_modem_at_urc_poll(s_modem);
    TEST_ASSERT_TRUE(n >= 1);
    TEST_ASSERT_EQUAL_INT(1, s_urc_calls);
}

void test_urc_unknown_prefix_does_not_fire(void) {
    s_urc_calls = 0;
    TEST_ASSERT_TRUE(hal_modem_at_urc_register(s_modem, "+CMQTTRXSTART:",
                                               capture_urc, NULL));
    push_str("\r\n+CSQ: 21,99\r\nOK\r\n");
    (void)hal_modem_at_send(s_modem, "AT+CSQ", NULL, 100);
    TEST_ASSERT_EQUAL_INT(0, s_urc_calls);
}

/* ── log filter ──────────────────────────────────────────────────────── */

void test_log_filter_install_and_clear(void) {
    static const char *secrets[] = { "supersecret", "topsecret" };
    hal_modem_at_set_log_filter(s_modem, secrets, 2);
    /* No public observable other than absence of crash; the redaction is
       inside log_filtered() and only emits when hal_deb is initialised.
       Smoke test: a send that exercises the log path must still succeed. */
    push_str("\r\nOK\r\n");
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_OK,
                      hal_modem_at_send(s_modem, "AT+CMQTTACCQ=0,supersecret", NULL, 100));
    /* Clearing must also be safe. */
    hal_modem_at_set_log_filter(s_modem, NULL, 0);
}

/* ── last_response ───────────────────────────────────────────────────── */

void test_last_response_returns_buffer(void) {
    push_str("\r\nOK\r\n");
    (void)hal_modem_at_send(s_modem, "AT", NULL, 100);
    const char *r = hal_modem_at_last_response(s_modem);
    TEST_ASSERT_EQUAL_PTR(s_rxbuf, r);
}

void test_last_response_null_handle(void) {
    TEST_ASSERT_NULL(hal_modem_at_last_response(NULL));
}

/* ── tick callback ───────────────────────────────────────────────────── */

static int s_tick_count = 0;
static void *s_tick_user_seen = (void *)0xDEAD;
static void tick_cb(void *user) { s_tick_count++; s_tick_user_seen = user; }

void test_tick_fires_during_send_timeout(void) {
    s_tick_count = 0;
    s_tick_user_seen = NULL;
    int dummy = 42;
    hal_modem_at_set_tick_callback(s_modem, tick_cb, &dummy);
    /* No response -> send loops in 2 ms poll for the full 100 ms. */
    TEST_ASSERT_EQUAL(HAL_MODEM_AT_TIMEOUT,
                      hal_modem_at_send(s_modem, "AT", NULL, 100));
    /* At ~2 ms cadence we expect well over 10 ticks in 100 ms. */
    TEST_ASSERT_GREATER_THAN(10, s_tick_count);
    TEST_ASSERT_EQUAL_PTR(&dummy, s_tick_user_seen);
}

void test_tick_unregister_disables_callback(void) {
    hal_modem_at_set_tick_callback(s_modem, tick_cb, NULL);
    hal_modem_at_set_tick_callback(s_modem, NULL, NULL);
    s_tick_count = 0;
    (void)hal_modem_at_send(s_modem, "AT", NULL, 30);
    TEST_ASSERT_EQUAL_INT(0, s_tick_count);
}

void test_sleep_ms_without_tick_is_plain_delay(void) {
    /* No tick installed -> must still sleep ~ the requested duration
       (we only check it does not hang forever and returns reasonably). */
    uint32_t before = hal_millis();
    hal_modem_at_sleep_ms(s_modem, 20);
    uint32_t elapsed = hal_millis() - before;
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(15u, elapsed);
}

void test_sleep_ms_fires_tick(void) {
    s_tick_count = 0;
    hal_modem_at_set_tick_callback(s_modem, tick_cb, NULL);
    hal_modem_at_sleep_ms(s_modem, 50);
    /* Slice size capped at 20 ms -> expect at least 2 ticks. */
    TEST_ASSERT_GREATER_OR_EQUAL_INT(2, s_tick_count);
}

void test_sleep_ms_null_handle_safe(void) {
    /* Must degrade to a plain delay, no crash. */
    hal_modem_at_sleep_ms(NULL, 5);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_create_rejects_null_cfg);
    RUN_TEST(test_create_rejects_missing_uart);
    RUN_TEST(test_create_rejects_tiny_buffer);
    RUN_TEST(test_destroy_null_handle_safe);

    RUN_TEST(test_send_ok);
    RUN_TEST(test_send_error);
    RUN_TEST(test_send_cme_error_classified_as_error);
    RUN_TEST(test_send_expected_substring_matches_before_ok);
    RUN_TEST(test_send_timeout_when_no_terminator);
    RUN_TEST(test_send_invalid_args);
    RUN_TEST(test_send_drains_stale_urcs_before_command);
    RUN_TEST(test_send_expected_waits_past_early_ok);
    RUN_TEST(test_send_expected_drains_trailing_ok);

    RUN_TEST(test_send_with_data_happy_path);
    RUN_TEST(test_send_with_data_no_prompt);
    RUN_TEST(test_send_with_data_invalid_payload_pointer);

    RUN_TEST(test_listen_until_predicate_fires);
    RUN_TEST(test_listen_until_settles_when_no_predicate);
    RUN_TEST(test_listen_until_times_out_when_predicate_never_fires);

    RUN_TEST(test_urc_dispatch_during_send);
    RUN_TEST(test_urc_register_rejects_invalid);
    RUN_TEST(test_urc_register_unregister_with_null_cb);
    RUN_TEST(test_urc_poll_drains_unsolicited);
    RUN_TEST(test_urc_unknown_prefix_does_not_fire);

    RUN_TEST(test_log_filter_install_and_clear);

    RUN_TEST(test_last_response_returns_buffer);
    RUN_TEST(test_last_response_null_handle);

    RUN_TEST(test_tick_fires_during_send_timeout);
    RUN_TEST(test_tick_unregister_disables_callback);
    RUN_TEST(test_sleep_ms_without_tick_is_plain_delay);
    RUN_TEST(test_sleep_ms_fires_tick);
    RUN_TEST(test_sleep_ms_null_handle_safe);
    return UNITY_END();
}
