#include "hal/impl/.mock/hal_mock.h"
#include "hal/modem/hal_modem_at.h"
#include "hal/modem/hal_simcom_a76xx.h"
#include "hal/serial/hal_serial.h"
#include "hal/serial/hal_uart.h"
#include "utils/unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

/* ── Test fixture ─────────────────────────────────────────────────────── */

static hal_uart_t s_uart = NULL;
static hal_simcom_a76xx_t s_modem = NULL;
static char s_rxbuf[1024];

/* Scripted-reply queue: every TX line consumes the next reply slot and
   pushes it into the mock UART RX, so the engine sees the response on
   the very next drain. */
typedef struct {
  char tx_match[64]; /* substring that must appear in TX, "" = match any */
  char reply[256];   /* bytes to push as RX */
  int consumed;
} script_entry_t;

#define SCRIPT_MAX 32
static script_entry_t s_script[SCRIPT_MAX];
static int s_script_count;
static int s_script_pos;
static int s_tx_count;

static void script_reset(void) {
  memset(s_script, 0, sizeof(s_script));
  s_script_count = 0;
  s_script_pos = 0;
  s_tx_count = 0;
}

static void script_push(const char *tx_match, const char *reply) {
  if (s_script_count >= SCRIPT_MAX)
    return;
  script_entry_t *e = &s_script[s_script_count++];
  snprintf(e->tx_match, sizeof(e->tx_match), "%s", tx_match ? tx_match : "");
  snprintf(e->reply, sizeof(e->reply), "%s", reply ? reply : "");
}

static void on_tx(hal_uart_t h, const char *text, void *user) {
  (void)user;
  s_tx_count++;
  if (s_script_pos >= s_script_count)
    return;
  script_entry_t *e = &s_script[s_script_pos];
  /* Empty match string matches any TX. */
  if (e->tx_match[0] != '\0' && strstr(text, e->tx_match) == NULL) {
    return; /* keep slot for a later matching write */
  }
  if (e->reply[0] != '\0') {
    hal_mock_uart_push(h, (const uint8_t *)e->reply, (int)strlen(e->reply));
  }
  e->consumed = 1;
  s_script_pos++;
}

void setUp(void) {
  hal_mock_serial_reset();
  s_uart = hal_uart_create(HAL_UART_PORT_2, 5, 4);
  hal_mock_uart_reset(s_uart);

  script_reset();
  hal_mock_uart_set_write_callback(s_uart, on_tx, NULL);

  memset(s_rxbuf, 0, sizeof(s_rxbuf));
  hal_simcom_a76xx_config_t cfg = {0};
  cfg.uart = s_uart;
  cfg.pwr_pin = -1; /* GPIO disabled in unit tests */
  cfg.rx_buf = s_rxbuf;
  cfg.rx_buf_size = sizeof(s_rxbuf);
  cfg.default_at_timeout_ms = 200;
  s_modem = hal_simcom_a76xx_create(&cfg);
}

void tearDown(void) {
  hal_simcom_a76xx_destroy(s_modem);
  s_modem = NULL;
  hal_mock_uart_set_write_callback(s_uart, NULL, NULL);
  hal_uart_destroy(s_uart);
  s_uart = NULL;
}

/* ── create / destroy ─────────────────────────────────────────────────── */

void test_create_rejects_null(void) {
  TEST_ASSERT_NULL(hal_simcom_a76xx_create(NULL));
}

void test_create_rejects_missing_uart(void) {
  hal_simcom_a76xx_config_t cfg = {0};
  cfg.rx_buf = s_rxbuf;
  cfg.rx_buf_size = sizeof(s_rxbuf);
  TEST_ASSERT_NULL(hal_simcom_a76xx_create(&cfg));
}

void test_create_rejects_small_buffer(void) {
  char tiny[64];
  hal_simcom_a76xx_config_t cfg = {0};
  cfg.uart = s_uart;
  cfg.rx_buf = tiny;
  cfg.rx_buf_size = sizeof(tiny);
  TEST_ASSERT_NULL(hal_simcom_a76xx_create(&cfg));
}

void test_destroy_null_safe(void) { hal_simcom_a76xx_destroy(NULL); }

void test_get_at_returns_non_null(void) {
  TEST_ASSERT_NOT_NULL(hal_simcom_a76xx_get_at(s_modem));
}

/* ── Power ────────────────────────────────────────────────────────────── */

void test_power_toggle_no_pin_is_ok(void) {
  /* pwr_pin == -1 in fixture; must succeed without touching GPIO. */
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_power_toggle(s_modem, 50));
}

void test_hard_reset_no_pin_is_ok(void) {
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK, hal_simcom_a76xx_hard_reset(s_modem));
}

void test_power_toggle_invalid_handle(void) {
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG,
                    hal_simcom_a76xx_power_toggle(NULL, 10));
}

/* ── Boot ─────────────────────────────────────────────────────────────── */

void test_wait_boot_completes_on_pb_done(void) {
  const char boot_urcs[] =
      "\r\n*ATREADY\r\n+CPIN: READY\r\nSMS DONE\r\nPB DONE\r\n";
  hal_mock_uart_push(s_uart, (const uint8_t *)boot_urcs,
                     (int)(sizeof(boot_urcs) - 1u));
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_wait_boot(s_modem, 1000));
}

void test_wait_boot_invalid_handle(void) {
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG,
                    hal_simcom_a76xx_wait_boot(NULL, 100));
}

/* ── Init ─────────────────────────────────────────────────────────────── */

void test_init_handshake_succeeds(void) {
  /* AT/OK on first try, then ATE0, CLTS, CEREG each return OK. */
  script_push("AT", "\r\nOK\r\n");
  script_push("ATE0", "\r\nOK\r\n");
  script_push("AT+CLTS=1", "\r\nOK\r\n");
  script_push("AT+CEREG=0", "\r\nOK\r\n");

  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK, hal_simcom_a76xx_init(s_modem));
}

void test_init_invalid_handle(void) {
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG, hal_simcom_a76xx_init(NULL));
}

/* ── SIM ──────────────────────────────────────────────────────────────── */

void test_wait_sim_ready_succeeds(void) {
  script_push("AT+CPIN?", "\r\n+CPIN: READY\r\n\r\nOK\r\n");
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_wait_sim_ready(s_modem, 1000));
}

void test_wait_sim_ready_times_out_when_silent(void) {
  /* No reply scripted -> CPIN? always times out. */
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_TIMEOUT,
                    hal_simcom_a76xx_wait_sim_ready(s_modem, 200));
}

void test_wait_sim_ready_polls_until_ready(void) {
  script_push("AT+CPIN?", "\r\n+CPIN: SIM PIN\r\n\r\nOK\r\n");
  script_push("AT+CPIN?", "\r\n+CPIN: READY\r\n\r\nOK\r\n");
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_wait_sim_ready(s_modem, 5000));
  TEST_ASSERT_EQUAL_INT(2, s_script_pos);
}

void test_get_sim_state_parses_codes(void) {
  static const struct {
    const char *reply;
    hal_simcom_a76xx_sim_state_t state;
  } cases[] = {
      {"\r\n+CPIN: READY\r\n\r\nOK\r\n", HAL_SIMCOM_A76XX_SIM_READY},
      {"\r\n+CPIN: SIM PIN\r\n\r\nOK\r\n", HAL_SIMCOM_A76XX_SIM_PIN},
      {"\r\n+CPIN: SIM PUK\r\n\r\nOK\r\n", HAL_SIMCOM_A76XX_SIM_PUK},
      {"\r\n+CPIN: SIM PIN2\r\n\r\nOK\r\n", HAL_SIMCOM_A76XX_SIM_OTHER},
      {"\r\n+CPIN: PH-SIM PIN\r\n\r\nOK\r\n", HAL_SIMCOM_A76XX_SIM_OTHER},
      /* Stale line before the reply: the last one wins. */
      {"\r\n+CPIN: SIM PIN\r\n\r\n+CPIN: READY\r\n\r\nOK\r\n",
       HAL_SIMCOM_A76XX_SIM_READY},
  };
  for (size_t i = 0; i < COUNTOF(cases); i++) {
    script_reset();
    script_push("AT+CPIN?", cases[i].reply);
    hal_simcom_a76xx_sim_state_t state = HAL_SIMCOM_A76XX_SIM_OTHER;
    if (cases[i].state == HAL_SIMCOM_A76XX_SIM_OTHER)
      state = HAL_SIMCOM_A76XX_SIM_READY;
    TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                      hal_simcom_a76xx_get_sim_state(s_modem, &state));
    TEST_ASSERT_EQUAL(cases[i].state, state);
  }
}

void test_get_sim_state_errors_leave_state_untouched(void) {
  hal_simcom_a76xx_sim_state_t state = HAL_SIMCOM_A76XX_SIM_PUK;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG,
                    hal_simcom_a76xx_get_sim_state(NULL, &state));
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG,
                    hal_simcom_a76xx_get_sim_state(s_modem, NULL));
  TEST_ASSERT_EQUAL_INT(0, s_tx_count);

  script_push("AT+CPIN?", "\r\n+CME ERROR: SIM not inserted\r\n");
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_ERROR,
                    hal_simcom_a76xx_get_sim_state(s_modem, &state));
  script_push("AT+CPIN?", "\r\nOK\r\n");
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_PARSE,
                    hal_simcom_a76xx_get_sim_state(s_modem, &state));
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_TIMEOUT,
                    hal_simcom_a76xx_get_sim_state(s_modem, &state));
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_SIM_PUK, state);
}

void test_set_pin_accepts_4_and_8_digits(void) {
  script_push("AT+CPIN=\"1234\"", "\r\nOK\r\n");
  script_push("AT+CPIN=\"12345678\"", "\r\nOK\r\n");
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_set_pin(s_modem, "1234"));
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_set_pin(s_modem, "12345678"));
  TEST_ASSERT_EQUAL_INT(2, s_script_pos);
}

void test_set_pin_rejects_invalid_args(void) {
  static const char *const bad[] = {"",     "123",   "123456789",
                                    "12a4", "12\"4", "1234\r"};
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG,
                    hal_simcom_a76xx_set_pin(NULL, "1234"));
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG,
                    hal_simcom_a76xx_set_pin(s_modem, NULL));
  for (size_t i = 0; i < COUNTOF(bad); i++) {
    TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG,
                      hal_simcom_a76xx_set_pin(s_modem, bad[i]));
  }
  TEST_ASSERT_EQUAL_INT(0, s_tx_count);
}

void test_set_pin_wrong_pin_is_error(void) {
  script_push("AT+CPIN=\"0000\"", "\r\n+CME ERROR: incorrect password\r\n");
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_ERROR,
                    hal_simcom_a76xx_set_pin(s_modem, "0000"));
}

void test_set_pin_times_out_and_hides_pin_in_log(void) {
  hal_debug_init_default();
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_TIMEOUT,
                    hal_simcom_a76xx_set_pin(s_modem, "4321"));
  TEST_ASSERT_NOT_NULL(strstr(hal_mock_deb_last_line(), "AT+CPIN=\"***\""));
  TEST_ASSERT_NULL(strstr(hal_mock_deb_last_line(), "4321"));
}

/* ── Network ──────────────────────────────────────────────────────────── */

void test_wait_network_registered_home(void) {
  script_push("AT+CREG?", "\r\n+CREG: 0,1\r\n\r\nOK\r\n");
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_wait_network_registered(s_modem, 1000));
}

void test_wait_network_registered_roaming(void) {
  script_push("AT+CREG?", "\r\n+CREG: 0,5\r\n\r\nOK\r\n");
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_wait_network_registered(s_modem, 1000));
}

void test_wait_network_registered_searching_eventually_times_out(void) {
  /* "0,2" = searching - hal returns OK but no home/roaming match. */
  for (int i = 0; i < 4; i++) {
    script_push("AT+CREG?", "\r\n+CREG: 0,2\r\n\r\nOK\r\n");
  }
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_TIMEOUT,
                    hal_simcom_a76xx_wait_network_registered(s_modem, 300));
}

/* ── PDP ──────────────────────────────────────────────────────────────── */

void test_attach_pdp_happy_path(void) {
  script_push("AT+CGDCONT=1,\"IP\",\"internet\"", "\r\nOK\r\n");
  script_push("AT+CGACT=1,1", "\r\nOK\r\n");
  hal_simcom_a76xx_apn_t apn = {"internet", NULL, NULL};
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_attach_pdp(s_modem, &apn));
}

void test_attach_pdp_invalid_apn(void) {
  hal_simcom_a76xx_apn_t apn = {NULL, NULL, NULL};
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG,
                    hal_simcom_a76xx_attach_pdp(s_modem, &apn));
}

void test_attach_pdp_cgact_error(void) {
  script_push("AT+CGDCONT", "\r\nOK\r\n");
  script_push("AT+CGACT", "\r\nERROR\r\n");
  hal_simcom_a76xx_apn_t apn = {"internet", NULL, NULL};
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_ERROR,
                    hal_simcom_a76xx_attach_pdp(s_modem, &apn));
}

/* ── Network time ─────────────────────────────────────────────────────── */

void test_get_network_time_iso8601_basic(void) {
  /* +CCLK: "24/03/21,14:30:00+08"  (UTC+02:00 = 8 * 15 min) */
  script_push("AT+CCLK?", "\r\n+CCLK: \"24/03/21,14:30:00+08\"\r\n\r\nOK\r\n");
  char out[40];
  TEST_ASSERT_EQUAL(
      HAL_SIMCOM_A76XX_OK,
      hal_simcom_a76xx_get_network_time_iso8601(s_modem, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("2024-03-21T14:30:00+02:00", out);
}

void test_get_network_time_negative_tz(void) {
  script_push("AT+CCLK?", "\r\n+CCLK: \"24/03/21,14:30:00-20\"\r\n\r\nOK\r\n");
  char out[40];
  TEST_ASSERT_EQUAL(
      HAL_SIMCOM_A76XX_OK,
      hal_simcom_a76xx_get_network_time_iso8601(s_modem, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("2024-03-21T14:30:00-05:00", out);
}

void test_get_network_time_parse_fail(void) {
  script_push("AT+CCLK?", "\r\n+CCLK: \"garbage\"\r\n\r\nOK\r\n");
  char out[40];
  TEST_ASSERT_EQUAL(
      HAL_SIMCOM_A76XX_PARSE,
      hal_simcom_a76xx_get_network_time_iso8601(s_modem, out, sizeof(out)));
}

void test_get_network_time_buffer_too_small(void) {
  char out[10];
  TEST_ASSERT_EQUAL(
      HAL_SIMCOM_A76XX_INVALID_ARG,
      hal_simcom_a76xx_get_network_time_iso8601(s_modem, out, sizeof(out)));
}

/* ── Cellular location (LBS) ─────────────────────────────────────────── */

void test_get_cell_location_happy_path(void) {
  script_push("AT+CLBS=1,1", "\r\n+CLBS: 0,52.2297,21.0122,1200\r\n\r\nOK\r\n");

  hal_mock_set_millis(10000);

  hal_simcom_a76xx_cell_location_t loc = {0};
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_get_cell_location(s_modem, &loc, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 52.2297f, loc.latitude_deg);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 21.0122f, loc.longitude_deg);
  TEST_ASSERT_EQUAL_INT(1200, loc.accuracy_m);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.0f, loc.speed_kmh);
}

void test_get_cell_location_happy_path_without_accuracy(void) {
  script_push("AT+CLBS=1,1", "\r\n+CLBS: 0,50.274372,19.124077\r\n\r\nOK\r\n");

  hal_mock_set_millis(10000);

  hal_simcom_a76xx_cell_location_t loc = {0};
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_get_cell_location(s_modem, &loc, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 50.274372f, loc.latitude_deg);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 19.124077f, loc.longitude_deg);
  TEST_ASSERT_EQUAL_INT(-1, loc.accuracy_m);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.0f, loc.speed_kmh);
}

void test_get_cell_location_second_fix_reports_speed(void) {
  script_push("AT+CLBS=1,1",
              "\r\n+CLBS: 0,50.274372,19.124077,550\r\n\r\nOK\r\n");
  script_push("AT+CLBS=1,1",
              "\r\n+CLBS: 0,50.274372,19.125077,550\r\n\r\nOK\r\n");

  hal_simcom_a76xx_cell_location_t loc1 = {0};
  hal_simcom_a76xx_cell_location_t loc2 = {0};

  hal_mock_set_millis(10000);
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_get_cell_location(s_modem, &loc1, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.0f, loc1.speed_kmh);

  hal_mock_set_millis(30000);
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_get_cell_location(s_modem, &loc2, 0));
  TEST_ASSERT_TRUE(loc2.speed_kmh > 0.0f);
}

void test_get_cell_location_parse_fail_truncated_lon(void) {
  script_push("AT+CLBS=1,1", "\r\n+CLBS: 0,50.274372,19.124");

  hal_simcom_a76xx_cell_location_t loc = {0};
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_PARSE,
                    hal_simcom_a76xx_get_cell_location(s_modem, &loc, 0));
}

void test_get_cell_location_parse_fail_nonzero_status(void) {
  script_push("AT+CLBS=1,1", "\r\n+CLBS: 2,0.0000,0.0000,0\r\n\r\nOK\r\n");

  hal_simcom_a76xx_cell_location_t loc = {0};
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_PARSE,
                    hal_simcom_a76xx_get_cell_location(s_modem, &loc, 0));
}

void test_get_cell_location_payload_split_mid_number(void) {
  /* Real-world capture: modem fragments the URC across UART writes
     and a CRLF lands in the middle of the latitude. Without the
     fragment-stitching parser the call would return PARSE. */
  script_push("AT+CLBS=1,1",
              "\r\nOK\r\n+CLBS: 0,50.2743\r\n72,19.124077,550\r\n");

  hal_mock_set_millis(10000);

  hal_simcom_a76xx_cell_location_t loc = {0};
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_get_cell_location(s_modem, &loc, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 50.274372f, loc.latitude_deg);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 19.124077f, loc.longitude_deg);
  TEST_ASSERT_EQUAL_INT(550, loc.accuracy_m);
}

void test_get_cell_location_invalid_args(void) {
  hal_simcom_a76xx_cell_location_t loc = {0};
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG,
                    hal_simcom_a76xx_get_cell_location(NULL, &loc, 0));
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG,
                    hal_simcom_a76xx_get_cell_location(s_modem, NULL, 0));
}

/* ── GNSS location ───────────────────────────────────────────────────── */

void test_gnss_location_init_sets_sentinels(void) {
  hal_simcom_a76xx_gnss_location_t loc;
  memset(&loc, 0x5A, sizeof(loc));

  hal_simcom_a76xx_gnss_location_init(&loc);

  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.0f, (float)loc.altitude_m);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.0f, (float)loc.speed_kmh);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.0f, (float)loc.course_deg);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.0f, (float)loc.hdop);
  TEST_ASSERT_EQUAL_INT(-1, loc.satellites_used);
  TEST_ASSERT_EQUAL_INT(-1, loc.satellites_view);
  TEST_ASSERT_EQUAL_INT(-1, loc.fix_mode);
  TEST_ASSERT_EQUAL_STRING("", loc.utc);

  hal_simcom_a76xx_gnss_location_init(NULL);
}

void test_gnss_power_on_happy_path(void) {
  script_push("AT+CGNSSPWR=1", "\r\nOK\r\n");

  TEST_ASSERT_FALSE(hal_simcom_a76xx_gnss_is_powered(s_modem));
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_gnss_power_on(s_modem, 0));
  TEST_ASSERT_TRUE(hal_simcom_a76xx_gnss_is_powered(s_modem));
}

void test_get_gnss_location_cgnssinfo_happy_path(void) {
  script_push("AT+CGNSSPWR=1", "\r\nOK\r\n");
  script_push("AT+CGNSSINFO",
              "\r\n+CGNSSINFO: "
              "3,10,,00,00,50.2737541,N,19.1138878,E,140626,122340.00,306.8,"
              "0.000,89.13,3.18,1.77,2.6\r\n\r\nOK\r\n");

  hal_simcom_a76xx_gnss_location_t loc;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_get_gnss_location(s_modem, &loc, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 50.273754f, (float)loc.latitude_deg);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 19.113888f, (float)loc.longitude_deg);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 306.8f, (float)loc.altitude_m);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, (float)loc.speed_kmh);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 89.13f, (float)loc.course_deg);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.77f, (float)loc.hdop);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.18f, (float)loc.pdop);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.6f, (float)loc.vdop);
  TEST_ASSERT_EQUAL_INT(10, loc.satellites_used);
  TEST_ASSERT_EQUAL_INT(10, loc.satellites_view);
  TEST_ASSERT_EQUAL_INT(3, loc.fix_mode);
  TEST_ASSERT_EQUAL_STRING("140626T122340.00Z", loc.utc);
}

void test_get_gnss_location_empty_fix_is_not_ready(void) {
  script_push("AT+CGNSSPWR=1", "\r\nOK\r\n");
  script_push("AT+CGNSSINFO", "\r\n+CGNSSINFO: ,,,,,,,,\r\n\r\nOK\r\n");

  hal_simcom_a76xx_gnss_location_t loc;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_NOT_READY,
                    hal_simcom_a76xx_get_gnss_location(s_modem, &loc, 0));
}

void test_get_gnss_location_cgnsinf_fallback_happy_path(void) {
  script_push("AT+CGNSSPWR=1", "\r\nOK\r\n");
  script_push("AT+CGNSSINFO", "\r\nERROR\r\n");
  script_push("AT+CGNSINF", "\r\n+CGNSINF: "
                            "1,1,20260614133958.000,50.274372,19.124077,260.0,"
                            "5.5,123.4,3,,0.8,1.2,1.5,,10,7\r\n\r\nOK\r\n");

  hal_simcom_a76xx_gnss_location_t loc;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_get_gnss_location(s_modem, &loc, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 50.274372f, (float)loc.latitude_deg);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 19.124077f, (float)loc.longitude_deg);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 260.0f, (float)loc.altitude_m);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 5.5f, (float)loc.speed_kmh);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 123.4f, (float)loc.course_deg);
  TEST_ASSERT_EQUAL_INT(10, loc.satellites_view);
  TEST_ASSERT_EQUAL_INT(7, loc.satellites_used);
  TEST_ASSERT_EQUAL_STRING("20260614133958.000", loc.utc);
}

void test_get_gnss_location_cgpsinfo_fallback_happy_path(void) {
  script_push("AT+CGNSSPWR=1", "\r\nOK\r\n");
  script_push("AT+CGNSSINFO", "\r\nERROR\r\n");
  script_push("AT+CGNSINF", "\r\nERROR\r\n");
  script_push("AT+CGPSINFO", "\r\n+CGPSINFO: "
                             "5016.4623,N,01907.4446,E,140626,133958.0,260.0,"
                             "12.3,100.0\r\n\r\nOK\r\n");

  hal_simcom_a76xx_gnss_location_t loc;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_get_gnss_location(s_modem, &loc, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 50.274372f, (float)loc.latitude_deg);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 19.124077f, (float)loc.longitude_deg);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 260.0f, (float)loc.altitude_m);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 22.7796f, (float)loc.speed_kmh);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 100.0f, (float)loc.course_deg);
  TEST_ASSERT_EQUAL_STRING("140626T133958.0Z", loc.utc);
}

void test_get_gnss_location_invalid_args(void) {
  hal_simcom_a76xx_gnss_location_t loc;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG,
                    hal_simcom_a76xx_gnss_power_on(NULL, 0));
  TEST_ASSERT_FALSE(hal_simcom_a76xx_gnss_is_powered(NULL));
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG,
                    hal_simcom_a76xx_get_gnss_location(NULL, &loc, 0));
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG,
                    hal_simcom_a76xx_get_gnss_location(s_modem, NULL, 0));
}

/* ── MQTT connect / disconnect ────────────────────────────────────────── */

static void script_mqtt_connect_plain(int ci) {
  char buf[32];
  snprintf(buf, sizeof(buf), "AT+CMQTTDISC=%d", ci);
  script_push(buf, "\r\nOK\r\n");
  snprintf(buf, sizeof(buf), "AT+CMQTTREL=%d", ci);
  script_push(buf, "\r\nOK\r\n");
  script_push("AT+CMQTTSTOP", "\r\nOK\r\n");
  script_push("AT+CMQTTSTART", "\r\n+CMQTTSTART: 0\r\n\r\nOK\r\n");
  snprintf(buf, sizeof(buf), "AT+CMQTTACCQ=%d", ci);
  script_push(buf, "\r\nOK\r\n");
  char expected[32];
  snprintf(expected, sizeof(expected), "\r\n+CMQTTCONNECT: %d,0\r\n\r\nOK\r\n",
           ci);
  snprintf(buf, sizeof(buf), "AT+CMQTTCONNECT=%d", ci);
  script_push(buf, expected);
}

void test_mqtt_connect_happy_path(void) {
  script_mqtt_connect_plain(0);
  hal_simcom_a76xx_mqtt_config_t cfg = {0};
  cfg.broker_host = "mqtt.example.com";
  cfg.broker_port = 1883;
  cfg.client_id = "client-1";
  cfg.keepalive_s = 60;
  cfg.clean_session = true;
  cfg.client_index = 0;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_mqtt_connect(s_modem, &cfg));
  TEST_ASSERT_TRUE(hal_simcom_a76xx_mqtt_is_connected(s_modem, 0));
  TEST_ASSERT_FALSE(hal_simcom_a76xx_mqtt_is_connected(s_modem, 1));
  TEST_ASSERT_EQUAL_INT(0,
                        hal_simcom_a76xx_mqtt_last_connect_result(s_modem, 0));
  TEST_ASSERT_NOT_NULL(
      strstr(hal_mock_serial_last_line(), "connected successfully"));
  TEST_ASSERT_NOT_NULL(strstr(hal_mock_serial_last_line(), "code=0"));
}

void test_mqtt_connect_invalid_args(void) {
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG,
                    hal_simcom_a76xx_mqtt_connect(s_modem, NULL));
  hal_simcom_a76xx_mqtt_config_t cfg = {0};
  cfg.broker_host = "h";
  cfg.client_id = "c";
  cfg.client_index = 5;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG,
                    hal_simcom_a76xx_mqtt_connect(s_modem, &cfg));
}

void test_mqtt_connect_start_fail(void) {
  char buf[32];
  snprintf(buf, sizeof(buf), "AT+CMQTTDISC=%d", 0);
  script_push(buf, "\r\nOK\r\n");
  snprintf(buf, sizeof(buf), "AT+CMQTTREL=%d", 0);
  script_push(buf, "\r\nOK\r\n");
  script_push("AT+CMQTTSTOP", "\r\nOK\r\n");
  script_push("AT+CMQTTSTART", "\r\nERROR\r\n");

  hal_simcom_a76xx_mqtt_config_t cfg = {0};
  cfg.broker_host = "h";
  cfg.broker_port = 1883;
  cfg.client_id = "c";
  cfg.client_index = 0;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_ERROR,
                    hal_simcom_a76xx_mqtt_connect(s_modem, &cfg));
  TEST_ASSERT_FALSE(hal_simcom_a76xx_mqtt_is_connected(s_modem, 0));
}

void test_mqtt_connect_failure_decodes_and_logs_result(void) {
  script_push("AT+CMQTTDISC=0", "\r\nOK\r\n");
  script_push("AT+CMQTTREL=0", "\r\nOK\r\n");
  script_push("AT+CMQTTSTOP", "\r\nOK\r\n");
  script_push("AT+CMQTTSTART", "\r\n+CMQTTSTART: 0\r\n\r\nOK\r\n");
  script_push("AT+CMQTTACCQ=0", "\r\nOK\r\n");
  script_push("AT+CMQTTCONNECT=0", "\r\n+CMQTTCONNECT: 0,3\r\n\r\nERROR\r\n");

  hal_simcom_a76xx_mqtt_config_t cfg = {0};
  cfg.broker_host = "h";
  cfg.broker_port = 8883;
  cfg.client_id = "c";
  cfg.client_index = 0;

  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_ERROR,
                    hal_simcom_a76xx_mqtt_connect(s_modem, &cfg));
  TEST_ASSERT_FALSE(hal_simcom_a76xx_mqtt_is_connected(s_modem, 0));
  TEST_ASSERT_EQUAL_INT(3,
                        hal_simcom_a76xx_mqtt_last_connect_result(s_modem, 0));
  TEST_ASSERT_NOT_NULL(
      strstr(hal_mock_serial_last_line(), "socket connect failed"));
  TEST_ASSERT_NOT_NULL(strstr(hal_mock_serial_last_line(), "code=3"));
}

void test_mqtt_result_strings_cover_auth_tls_and_unknown_codes(void) {
  TEST_ASSERT_EQUAL_STRING("socket connect failed",
                           hal_simcom_a76xx_mqtt_result_string(3));
  TEST_ASSERT_EQUAL_STRING("connection refused: bad username or password",
                           hal_simcom_a76xx_mqtt_result_string(30));
  TEST_ASSERT_EQUAL_STRING("connection refused: not authorized",
                           hal_simcom_a76xx_mqtt_result_string(31));
  TEST_ASSERT_EQUAL_STRING("TLS handshake failed",
                           hal_simcom_a76xx_mqtt_result_string(32));
  TEST_ASSERT_EQUAL_STRING("unknown MQTT result",
                           hal_simcom_a76xx_mqtt_result_string(999));
  TEST_ASSERT_EQUAL_INT(HAL_SIMCOM_A76XX_MQTT_RESULT_UNKNOWN,
                        hal_simcom_a76xx_mqtt_last_connect_result(NULL, 0));
}

void test_mqtt_disconnect_clears_flag(void) {
  script_mqtt_connect_plain(0);
  hal_simcom_a76xx_mqtt_config_t cfg = {0};
  cfg.broker_host = "h";
  cfg.broker_port = 1883;
  cfg.client_id = "c";
  cfg.client_index = 0;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_mqtt_connect(s_modem, &cfg));
  script_push("AT+CMQTTDISC=0", "\r\nOK\r\n");
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_mqtt_disconnect(s_modem, 0));
  TEST_ASSERT_FALSE(hal_simcom_a76xx_mqtt_is_connected(s_modem, 0));
}

/* ── MQTT publish ─────────────────────────────────────────────────────── */

void test_mqtt_publish_requires_connection(void) {
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_NOT_READY,
                    hal_simcom_a76xx_mqtt_publish(s_modem, 0, "t", "p", 1, 0));
}

void test_mqtt_publish_happy_path(void) {
  script_mqtt_connect_plain(0);
  hal_simcom_a76xx_mqtt_config_t cfg = {0};
  cfg.broker_host = "h";
  cfg.broker_port = 1883;
  cfg.client_id = "c";
  cfg.client_index = 0;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_mqtt_connect(s_modem, &cfg));

  script_push("AT+CMQTTTOPIC=0", ">\r\nOK\r\n");
  script_push("AT+CMQTTPAYLOAD=0", ">\r\nOK\r\n");
  script_push("AT+CMQTTPUB=0,1,60", "\r\n+CMQTTPUB: 0,0\r\n\r\nOK\r\n");

  TEST_ASSERT_EQUAL(
      HAL_SIMCOM_A76XX_OK,
      hal_simcom_a76xx_mqtt_publish(s_modem, 0, "test/topic", "hello", 5, 1));
}

void test_mqtt_publish_invalid_qos(void) {
  /* Even without connection the input validation should fire first. */
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_INVALID_ARG,
                    hal_simcom_a76xx_mqtt_publish(s_modem, 0, "t", "p", 1, 9));
}

/* ── MQTT subscribe + URC reassembly ──────────────────────────────────── */

static int s_msg_calls;
static int s_msg_client_index;
static char s_msg_topic[64];
static char s_msg_payload[64];
static size_t s_msg_payload_len;

static void on_msg(int client_index, const char *topic, const uint8_t *payload,
                   size_t payload_len, void *user) {
  (void)user;
  s_msg_calls++;
  s_msg_client_index = client_index;
  snprintf(s_msg_topic, sizeof(s_msg_topic), "%s", topic ? topic : "");
  size_t n = payload_len < sizeof(s_msg_payload) - 1
                 ? payload_len
                 : sizeof(s_msg_payload) - 1;
  if (payload && n > 0)
    memcpy(s_msg_payload, payload, n);
  s_msg_payload[n] = '\0';
  s_msg_payload_len = payload_len;
}

void test_mqtt_subscribe_requires_connection(void) {
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_NOT_READY,
                    hal_simcom_a76xx_mqtt_subscribe(s_modem, 0, "t", 0));
}

void test_mqtt_subscribe_happy_path(void) {
  script_mqtt_connect_plain(0);
  hal_simcom_a76xx_mqtt_config_t cfg = {0};
  cfg.broker_host = "h";
  cfg.broker_port = 1883;
  cfg.client_id = "c";
  cfg.client_index = 0;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_mqtt_connect(s_modem, &cfg));

  script_push("AT+CMQTTSUBTOPIC=0", ">\r\nOK\r\n");
  script_push("AT+CMQTTSUB=0", "\r\n+CMQTTSUB: 0,0\r\n\r\nOK\r\n");

  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK, hal_simcom_a76xx_mqtt_subscribe(
                                             s_modem, 0, "test/topic", 1));
}

void test_mqtt_unsubscribe_happy_path(void) {
  script_mqtt_connect_plain(0);
  hal_simcom_a76xx_mqtt_config_t cfg = {0};
  cfg.broker_host = "h";
  cfg.broker_port = 1883;
  cfg.client_id = "c";
  cfg.client_index = 0;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_mqtt_connect(s_modem, &cfg));

  script_push("AT+CMQTTUNSUBTOPIC=0", ">\r\nOK\r\n");
  script_push("AT+CMQTTUNSUB=0", "\r\n+CMQTTUNSUB: 0,0\r\n\r\nOK\r\n");

  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK, hal_simcom_a76xx_mqtt_unsubscribe(
                                             s_modem, 0, "test/topic"));
}

void test_mqtt_set_message_callback_null_handle(void) {
  TEST_ASSERT_EQUAL(
      HAL_SIMCOM_A76XX_INVALID_ARG,
      hal_simcom_a76xx_mqtt_set_message_callback(NULL, NULL, NULL));
}

void test_mqtt_rx_reassembly_dispatches_message(void) {
  s_msg_calls = 0;
  s_msg_topic[0] = '\0';
  s_msg_payload[0] = '\0';
  TEST_ASSERT_EQUAL(
      HAL_SIMCOM_A76XX_OK,
      hal_simcom_a76xx_mqtt_set_message_callback(s_modem, on_msg, NULL));

  /* Simulate an incoming MQTT message on client 0:
       +CMQTTRXSTART: 0,10,5
       +CMQTTRXTOPIC: 0,10
       test/topic
       +CMQTTRXPAYLOAD: 0,5
       hello
       +CMQTTRXEND: 0
     Driver is in disconnected state, but the URC pipeline is wired
     at construction so the reassembly path runs regardless. */
  const char *rx = "\r\n+CMQTTRXSTART: 0,10,5\r\n"
                   "+CMQTTRXTOPIC: 0,10\r\n"
                   "test/topic\r\n"
                   "+CMQTTRXPAYLOAD: 0,5\r\n"
                   "hello\r\n"
                   "+CMQTTRXEND: 0\r\n";
  hal_mock_uart_push(s_uart, (const uint8_t *)rx, (int)strlen(rx));

  int n = hal_simcom_a76xx_mqtt_poll(s_modem);
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_INT(1, s_msg_calls);
  TEST_ASSERT_EQUAL_INT(0, s_msg_client_index);
  TEST_ASSERT_EQUAL_STRING("test/topic", s_msg_topic);
  TEST_ASSERT_EQUAL_STRING("hello", s_msg_payload);
  TEST_ASSERT_EQUAL_size_t(5, s_msg_payload_len);
}

/* ── MQTT receive: byte counts, queue, callback context (audit F07) ──── */

struct rx_record_t {
  int client_index;
  std::string topic;
  std::string payload;
};
static std::vector<rx_record_t> s_rx;

static void record_msg(int client_index, const char *topic,
                       const uint8_t *payload, size_t payload_len, void *user) {
  (void)user;
  s_rx.push_back(
      {client_index, topic, std::string((const char *)payload, payload_len)});
}

/* The CMQTTRX URC sequence of one message, topic and payload in one packet
   each, as the A76xx sends it. */
static std::string rx_message(const std::string &topic,
                              const std::string &payload) {
  return "\r\n+CMQTTRXSTART: 0," + std::to_string(topic.size()) + "," +
         std::to_string(payload.size()) + "\r\n+CMQTTRXTOPIC: 0," +
         std::to_string(topic.size()) + "\r\n" + topic +
         "\r\n+CMQTTRXPAYLOAD: 0," + std::to_string(payload.size()) + "\r\n" +
         payload + "\r\n+CMQTTRXEND: 0\r\n";
}

/* Push @p bytes in pieces of @p chunk bytes, polling after each piece. */
static int feed_and_poll(const std::string &bytes, size_t chunk) {
  int delivered = 0;
  for (size_t pos = 0u; pos < bytes.size(); pos += chunk) {
    const size_t n = bytes.size() - pos < chunk ? bytes.size() - pos : chunk;
    hal_mock_uart_push(s_uart, (const uint8_t *)bytes.data() + pos, (int)n);
    delivered += hal_simcom_a76xx_mqtt_poll(s_modem);
  }
  return delivered;
}

static void start_recording(void) {
  s_rx.clear();
  TEST_ASSERT_EQUAL(
      HAL_SIMCOM_A76XX_OK,
      hal_simcom_a76xx_mqtt_set_message_callback(s_modem, record_msg, NULL));
}

/* Audit F07: two messages waiting in one poll gave one callback with the
   second topic only. */
void test_mqtt_rx_burst_delivers_every_message(void) {
  start_recording();
  TEST_ASSERT_EQUAL_INT(
      2, feed_and_poll(rx_message("first", "A") + rx_message("second", "B"),
                       256u));
  TEST_ASSERT_EQUAL_UINT(2u, s_rx.size());
  TEST_ASSERT_EQUAL_STRING("first", s_rx[0].topic.c_str());
  TEST_ASSERT_EQUAL_STRING("A", s_rx[0].payload.c_str());
  TEST_ASSERT_EQUAL_STRING("second", s_rx[1].topic.c_str());
  TEST_ASSERT_EQUAL_STRING("B", s_rx[1].payload.c_str());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_simcom_a76xx_mqtt_rx_dropped(s_modem));
}

/* Audit F07: a payload with a newline lost everything after it and NUL cut
   the length. The payload is read by count, so every byte value survives,
   also text that looks like a URC or a result code. */
void test_mqtt_rx_payload_keeps_every_byte(void) {
  start_recording();
  const std::string payloads[] = {"line1\nline2", std::string("A\0B", 3),
                                  "+CMQTTRXEND: 0", "\r\nOK\r\n"};
  for (const std::string &payload : payloads) {
    s_rx.clear();
    TEST_ASSERT_EQUAL_INT(1, feed_and_poll(rx_message("t", payload), 256u));
    TEST_ASSERT_EQUAL_UINT(1u, s_rx.size());
    TEST_ASSERT_EQUAL_UINT(payload.size(), s_rx[0].payload.size());
    TEST_ASSERT_EQUAL_MEMORY(payload.data(), s_rx[0].payload.data(),
                             payload.size());
  }
}

/* The URC sequence may reach the UART in any pieces. */
void test_mqtt_rx_split_at_every_byte(void) {
  start_recording();
  const std::string payload("x\r\n+y\0z", 7);
  TEST_ASSERT_EQUAL_INT(1, feed_and_poll(rx_message("a/b", payload), 1u));
  TEST_ASSERT_EQUAL_UINT(1u, s_rx.size());
  TEST_ASSERT_EQUAL_STRING("a/b", s_rx[0].topic.c_str());
  TEST_ASSERT_EQUAL_UINT(payload.size(), s_rx[0].payload.size());
  TEST_ASSERT_EQUAL_MEMORY(payload.data(), s_rx[0].payload.data(),
                           payload.size());
}

/* Long topics and payloads come in several RXTOPIC / RXPAYLOAD packets. */
void test_mqtt_rx_joins_topic_and_payload_packets(void) {
  start_recording();
  const std::string rx = "\r\n+CMQTTRXSTART: 1,7,11\r\n"
                         "+CMQTTRXTOPIC: 1,4\r\ndpf/\r\n"
                         "+CMQTTRXTOPIC: 1,3\r\ncmd\r\n"
                         "+CMQTTRXPAYLOAD: 1,6\r\nhello \r\n"
                         "+CMQTTRXPAYLOAD: 1,5\r\nworld\r\n"
                         "+CMQTTRXEND: 1\r\n";
  TEST_ASSERT_EQUAL_INT(1, feed_and_poll(rx, 256u));
  TEST_ASSERT_EQUAL_INT(1, s_rx[0].client_index);
  TEST_ASSERT_EQUAL_STRING("dpf/cmd", s_rx[0].topic.c_str());
  TEST_ASSERT_EQUAL_STRING("hello world", s_rx[0].payload.c_str());
}

/* Exactly full buffers are delivered; one byte more drops the message,
   counts it and leaves the next message intact. */
void test_mqtt_rx_size_limits_drop_and_count(void) {
  start_recording();
  const std::string max_topic(HAL_SIMCOM_A76XX_TOPIC_MAX - 1, 't');
  const std::string max_payload(HAL_SIMCOM_A76XX_PAYLOAD_MAX, 'p');
  TEST_ASSERT_EQUAL_INT(1, feed_and_poll(rx_message(max_topic, "x"), 200u));
  TEST_ASSERT_EQUAL_INT(1, feed_and_poll(rx_message("t", max_payload), 200u));
  TEST_ASSERT_EQUAL_UINT(2u, s_rx.size());
  TEST_ASSERT_TRUE(s_rx[0].topic == max_topic);
  TEST_ASSERT_TRUE(s_rx[1].payload == max_payload);

  TEST_ASSERT_EQUAL_INT(0,
                        feed_and_poll(rx_message(max_topic + "t", "x"), 200u));
  TEST_ASSERT_EQUAL_INT(
      0, feed_and_poll(rx_message("t", max_payload + "p"), 200u));
  TEST_ASSERT_EQUAL_UINT32(2u, hal_simcom_a76xx_mqtt_rx_dropped(s_modem));
  TEST_ASSERT_EQUAL_INT(1, feed_and_poll(rx_message("next", "ok"), 200u));
  TEST_ASSERT_EQUAL_STRING("ok", s_rx.back().payload.c_str());
}

/* A burst longer than the queue keeps the oldest messages and counts the
   rest; nothing is overwritten. */
void test_mqtt_rx_full_queue_drops_newest_and_counts(void) {
  start_recording();
  std::string burst;
  for (int i = 0; i <= HAL_SIMCOM_A76XX_RX_QUEUE_DEPTH; ++i) {
    burst += rx_message("q", std::to_string(i));
  }
  TEST_ASSERT_EQUAL_INT(HAL_SIMCOM_A76XX_RX_QUEUE_DEPTH,
                        feed_and_poll(burst, 400u));
  for (int i = 0; i < HAL_SIMCOM_A76XX_RX_QUEUE_DEPTH; ++i) {
    TEST_ASSERT_TRUE(s_rx[(size_t)i].payload == std::to_string(i));
  }
  TEST_ASSERT_EQUAL_UINT32(1u, hal_simcom_a76xx_mqtt_rx_dropped(s_modem));
}

/* A message that arrives inside another command's response is queued for
   the next poll, and its payload cannot end that command. */
void test_mqtt_rx_during_command_is_queued(void) {
  start_recording();
  static std::string reply;
  reply = rx_message("in/cmd", "\r\nOK\r\n") + "\r\n+CPIN: READY\r\n\r\nOK\r\n";
  script_push("AT+CPIN?", reply.c_str());
  hal_simcom_a76xx_sim_state_t state = HAL_SIMCOM_A76XX_SIM_OTHER;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_get_sim_state(s_modem, &state));
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_SIM_READY, state);
  TEST_ASSERT_TRUE(s_rx.empty());
  TEST_ASSERT_EQUAL_INT(1, hal_simcom_a76xx_mqtt_poll(s_modem));
  TEST_ASSERT_EQUAL_STRING("in/cmd", s_rx[0].topic.c_str());
  TEST_ASSERT_TRUE(s_rx[0].payload == "\r\nOK\r\n");
}

static std::atomic<int> s_reply_publish_result{HAL_SIMCOM_A76XX_ERROR};
static std::atomic<bool> s_reply_published{false};
static std::thread s_reply_thread;
static bool s_published_in_callback;
static int s_nested_poll;
static void publish_reply(int client_index, const char *topic,
                          const uint8_t *payload, size_t payload_len,
                          void *user) {
  (void)topic;
  (void)payload;
  (void)payload_len;
  (void)user;
  s_msg_calls++;
  /* Publish from another thread and wait for it here: with the driver lock
     still held by poll this would time out instead of deadlocking. */
  s_reply_thread = std::thread([client_index] {
    s_reply_publish_result.store(hal_simcom_a76xx_mqtt_publish(
        s_modem, client_index, "reply", "ping", 4u, 0));
    s_reply_published.store(true);
  });
  for (int ms = 0; ms < 2000 && !s_reply_published.load(); ++ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  /* Asserted after poll returns; a nested poll is safe only without the
     lock and must not deliver the same message again. */
  s_published_in_callback = s_reply_published.load();
  if (s_published_in_callback) {
    s_nested_poll = hal_simcom_a76xx_mqtt_poll(s_modem);
  }
}

/* The callback runs outside the driver lock, so it may publish. */
void test_mqtt_callback_may_publish_from_poll(void) {
  script_mqtt_connect_plain(0);
  hal_simcom_a76xx_mqtt_config_t cfg = {0};
  cfg.broker_host = "h";
  cfg.broker_port = 1883;
  cfg.client_id = "c";
  cfg.client_index = 0;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_mqtt_connect(s_modem, &cfg));
  script_push("AT+CMQTTTOPIC=0,5", ">");
  script_push("reply", "\r\nOK\r\n");
  script_push("AT+CMQTTPAYLOAD=0,4", ">");
  script_push("ping", "\r\nOK\r\n");
  script_push("AT+CMQTTPUB=0,0", "\r\nOK\r\n\r\n+CMQTTPUB: 0,0\r\n");

  s_msg_calls = 0;
  s_reply_publish_result.store(HAL_SIMCOM_A76XX_ERROR);
  s_reply_published.store(false);
  s_published_in_callback = false;
  s_nested_poll = -1;
  TEST_ASSERT_EQUAL(
      HAL_SIMCOM_A76XX_OK,
      hal_simcom_a76xx_mqtt_set_message_callback(s_modem, publish_reply, NULL));
  const std::string rx = rx_message("in", "ping");
  hal_mock_uart_push(s_uart, (const uint8_t *)rx.data(), (int)rx.size());
  const int delivered = hal_simcom_a76xx_mqtt_poll(s_modem);
  if (s_reply_thread.joinable()) {
    s_reply_thread.join();
  }
  TEST_ASSERT_TRUE_MESSAGE(s_published_in_callback,
                           "callback ran with the driver lock held");
  TEST_ASSERT_EQUAL_INT(0, s_nested_poll);
  TEST_ASSERT_EQUAL_INT(1, delivered);
  TEST_ASSERT_EQUAL_INT(1, s_msg_calls);
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK, s_reply_publish_result.load());
}

/* A '>' inside an incoming payload is data: send_with_data must keep
   waiting for the real prompt before it writes the topic bytes. */
static int s_unread_at_data_write = -1;
static void reply_prompt_after_message(hal_uart_t h, const char *text,
                                       void *user) {
  (void)user;
  const std::string line(text);
  std::string reply;
  if (line.rfind("AT+CMQTTTOPIC=", 0) == 0) {
    reply = rx_message("in", "a>b") + ">";
  } else if (line.rfind("AT+CMQTTPAYLOAD=", 0) == 0) {
    reply = ">";
  } else if (line.rfind("AT+CMQTTPUB=", 0) == 0) {
    reply = "\r\nOK\r\n\r\n+CMQTTPUB: 0,0\r\n";
  } else {
    if (line == "out") {
      s_unread_at_data_write = hal_uart_available(h);
    }
    reply = "\r\nOK\r\n";
  }
  hal_mock_uart_push(h, (const uint8_t *)reply.data(), (int)reply.size());
}

void test_prompt_char_in_incoming_payload_is_not_the_prompt(void) {
  script_mqtt_connect_plain(0);
  hal_simcom_a76xx_mqtt_config_t cfg = {0};
  cfg.broker_host = "h";
  cfg.broker_port = 1883;
  cfg.client_id = "c";
  cfg.client_index = 0;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_mqtt_connect(s_modem, &cfg));
  start_recording();
  s_unread_at_data_write = -1;
  hal_mock_uart_set_write_callback(s_uart, reply_prompt_after_message, NULL);
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK, hal_simcom_a76xx_mqtt_publish(
                                             s_modem, 0, "out", "x", 1u, 0));
  hal_mock_uart_set_write_callback(s_uart, NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, s_unread_at_data_write);
  TEST_ASSERT_EQUAL_INT(1, hal_simcom_a76xx_mqtt_poll(s_modem));
  TEST_ASSERT_TRUE(s_rx[0].payload == "a>b");
}

/* ── Concurrent callers (audit F08) ───────────────────────────────────── */

/* Stateless modem: answers each write by its content. Every publish also
   brings one incoming message, as a busy broker would. */
static std::vector<std::string> s_wire;
static void modem_by_command(hal_uart_t h, const char *text, void *user) {
  (void)user;
  const std::string line(text);
  s_wire.push_back(line.substr(0, line.find_first_of("=\r")));
  std::string reply;
  if (line.rfind("AT+CMQTTTOPIC=", 0) == 0 ||
      line.rfind("AT+CMQTTPAYLOAD=", 0) == 0) {
    reply = ">";
  } else if (line.rfind("AT+CMQTTPUB=", 0) == 0) {
    reply = "\r\nOK\r\n\r\n+CMQTTPUB: 0,0\r\n" + rx_message("in", "m");
  } else if (line.rfind("AT+CPIN?", 0) == 0) {
    reply = "\r\n+CPIN: READY\r\n\r\nOK\r\n";
  } else {
    reply = "\r\nOK\r\n"; /* topic or payload bytes after a prompt */
  }
  hal_mock_uart_push(h, (const uint8_t *)reply.data(), (int)reply.size());
}

static std::atomic<int> s_delivered{0};
static void count_msg(int client_index, const char *topic,
                      const uint8_t *payload, size_t payload_len, void *user) {
  (void)client_index;
  (void)topic;
  (void)payload;
  (void)payload_len;
  (void)user;
  s_delivered.fetch_add(1);
}

/* A publish is three AT transactions and get_sim_state parses the reply
   after its command: both must stay whole while other threads use the
   modem, and messages that arrive meanwhile must all reach the callback.
   Under the TSan gate stage this also checks the shared state for races. */
void test_concurrent_callers_keep_operations_whole(void) {
  script_mqtt_connect_plain(0);
  hal_simcom_a76xx_mqtt_config_t cfg = {0};
  cfg.broker_host = "h";
  cfg.broker_port = 1883;
  cfg.client_id = "c";
  cfg.client_index = 0;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_mqtt_connect(s_modem, &cfg));
  hal_mock_uart_set_write_callback(s_uart, modem_by_command, NULL);
  s_wire.clear();
  s_delivered.store(0);
  TEST_ASSERT_EQUAL(
      HAL_SIMCOM_A76XX_OK,
      hal_simcom_a76xx_mqtt_set_message_callback(s_modem, count_msg, NULL));

  const int rounds = 100;
  std::atomic<bool> done{false};
  std::atomic<int> publish_failures{0};
  std::atomic<int> sim_failures{0};
  std::thread publisher([&] {
    for (int i = 0; i < rounds; ++i) {
      if (hal_simcom_a76xx_mqtt_publish(s_modem, 0, "out", "data", 4u, 0) !=
          HAL_SIMCOM_A76XX_OK) {
        publish_failures.fetch_add(1);
      }
      /* Keep at most one message waiting, below the queue depth. */
      for (int spin = 0; spin < 100000 && s_delivered.load() <= i; ++spin) {
        std::this_thread::yield();
      }
    }
    done.store(true);
  });
  std::thread poller([&] {
    while (!done.load()) {
      (void)hal_simcom_a76xx_mqtt_poll(s_modem);
    }
    (void)hal_simcom_a76xx_mqtt_poll(s_modem);
  });
  std::thread sim_reader([&] {
    while (!done.load()) {
      hal_simcom_a76xx_sim_state_t state = HAL_SIMCOM_A76XX_SIM_OTHER;
      if (hal_simcom_a76xx_get_sim_state(s_modem, &state) !=
              HAL_SIMCOM_A76XX_OK ||
          state != HAL_SIMCOM_A76XX_SIM_READY) {
        sim_failures.fetch_add(1);
      }
      std::this_thread::yield();
    }
  });
  publisher.join();
  poller.join();
  sim_reader.join();
  hal_mock_uart_set_write_callback(s_uart, NULL, NULL);

  TEST_ASSERT_EQUAL_INT(0, publish_failures.load());
  TEST_ASSERT_EQUAL_INT(0, sim_failures.load());
  TEST_ASSERT_EQUAL_INT(rounds, s_delivered.load());
  TEST_ASSERT_EQUAL_UINT32(0u, hal_simcom_a76xx_mqtt_rx_dropped(s_modem));
  /* Each publish shows on the wire as its five writes in a row. */
  const char *const publish_writes[] = {
      "AT+CMQTTTOPIC", "out", "AT+CMQTTPAYLOAD", "data", "AT+CMQTTPUB"};
  int publishes = 0;
  for (size_t i = 0u; i < s_wire.size(); ++i) {
    if (s_wire[i] != publish_writes[0]) {
      continue;
    }
    TEST_ASSERT_LESS_OR_EQUAL_UINT(s_wire.size(), i + COUNTOF(publish_writes));
    for (size_t k = 1u; k < COUNTOF(publish_writes); ++k) {
      TEST_ASSERT_EQUAL_STRING(publish_writes[k], s_wire[i + k].c_str());
    }
    ++publishes;
  }
  TEST_ASSERT_EQUAL_INT(rounds, publishes);
}

void test_mqtt_poll_without_data_returns_zero(void) {
  TEST_ASSERT_EQUAL_INT(0, hal_simcom_a76xx_mqtt_poll(s_modem));
}

void test_mqtt_connlost_urc_clears_flag(void) {
  script_mqtt_connect_plain(0);
  hal_simcom_a76xx_mqtt_config_t cfg = {0};
  cfg.broker_host = "h";
  cfg.broker_port = 1883;
  cfg.client_id = "c";
  cfg.client_index = 0;
  TEST_ASSERT_EQUAL(HAL_SIMCOM_A76XX_OK,
                    hal_simcom_a76xx_mqtt_connect(s_modem, &cfg));
  TEST_ASSERT_TRUE(hal_simcom_a76xx_mqtt_is_connected(s_modem, 0));

  hal_mock_uart_push(s_uart, (const uint8_t *)"\r\n+CMQTTCONNLOST: 0,2\r\n",
                     23);
  (void)hal_simcom_a76xx_mqtt_poll(s_modem);
  TEST_ASSERT_FALSE(hal_simcom_a76xx_mqtt_is_connected(s_modem, 0));
}

/* ── Runner ───────────────────────────────────────────────────────────── */

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_create_rejects_null);
  RUN_TEST(test_create_rejects_missing_uart);
  RUN_TEST(test_create_rejects_small_buffer);
  RUN_TEST(test_destroy_null_safe);
  RUN_TEST(test_get_at_returns_non_null);

  RUN_TEST(test_power_toggle_no_pin_is_ok);
  RUN_TEST(test_hard_reset_no_pin_is_ok);
  RUN_TEST(test_power_toggle_invalid_handle);

  RUN_TEST(test_wait_boot_completes_on_pb_done);
  RUN_TEST(test_wait_boot_invalid_handle);

  RUN_TEST(test_init_handshake_succeeds);
  RUN_TEST(test_init_invalid_handle);

  RUN_TEST(test_wait_sim_ready_succeeds);
  RUN_TEST(test_wait_sim_ready_times_out_when_silent);
  RUN_TEST(test_wait_sim_ready_polls_until_ready);
  RUN_TEST(test_get_sim_state_parses_codes);
  RUN_TEST(test_get_sim_state_errors_leave_state_untouched);
  RUN_TEST(test_set_pin_accepts_4_and_8_digits);
  RUN_TEST(test_set_pin_rejects_invalid_args);
  RUN_TEST(test_set_pin_wrong_pin_is_error);
  RUN_TEST(test_set_pin_times_out_and_hides_pin_in_log);

  RUN_TEST(test_wait_network_registered_home);
  RUN_TEST(test_wait_network_registered_roaming);
  RUN_TEST(test_wait_network_registered_searching_eventually_times_out);

  RUN_TEST(test_attach_pdp_happy_path);
  RUN_TEST(test_attach_pdp_invalid_apn);
  RUN_TEST(test_attach_pdp_cgact_error);

  RUN_TEST(test_get_network_time_iso8601_basic);
  RUN_TEST(test_get_network_time_negative_tz);
  RUN_TEST(test_get_network_time_parse_fail);
  RUN_TEST(test_get_network_time_buffer_too_small);

  RUN_TEST(test_get_cell_location_happy_path);
  RUN_TEST(test_get_cell_location_happy_path_without_accuracy);
  RUN_TEST(test_get_cell_location_second_fix_reports_speed);
  RUN_TEST(test_get_cell_location_parse_fail_truncated_lon);
  RUN_TEST(test_get_cell_location_parse_fail_nonzero_status);
  RUN_TEST(test_get_cell_location_payload_split_mid_number);
  RUN_TEST(test_get_cell_location_invalid_args);

  RUN_TEST(test_gnss_location_init_sets_sentinels);
  RUN_TEST(test_gnss_power_on_happy_path);
  RUN_TEST(test_get_gnss_location_cgnssinfo_happy_path);
  RUN_TEST(test_get_gnss_location_empty_fix_is_not_ready);
  RUN_TEST(test_get_gnss_location_cgnsinf_fallback_happy_path);
  RUN_TEST(test_get_gnss_location_cgpsinfo_fallback_happy_path);
  RUN_TEST(test_get_gnss_location_invalid_args);

  RUN_TEST(test_mqtt_connect_happy_path);
  RUN_TEST(test_mqtt_connect_invalid_args);
  RUN_TEST(test_mqtt_connect_start_fail);
  RUN_TEST(test_mqtt_connect_failure_decodes_and_logs_result);
  RUN_TEST(test_mqtt_result_strings_cover_auth_tls_and_unknown_codes);
  RUN_TEST(test_mqtt_disconnect_clears_flag);

  RUN_TEST(test_mqtt_publish_requires_connection);
  RUN_TEST(test_mqtt_publish_happy_path);
  RUN_TEST(test_mqtt_publish_invalid_qos);

  RUN_TEST(test_mqtt_subscribe_requires_connection);
  RUN_TEST(test_mqtt_subscribe_happy_path);
  RUN_TEST(test_mqtt_unsubscribe_happy_path);
  RUN_TEST(test_mqtt_set_message_callback_null_handle);
  RUN_TEST(test_mqtt_rx_reassembly_dispatches_message);
  RUN_TEST(test_mqtt_poll_without_data_returns_zero);
  RUN_TEST(test_mqtt_rx_burst_delivers_every_message);
  RUN_TEST(test_mqtt_rx_payload_keeps_every_byte);
  RUN_TEST(test_mqtt_rx_split_at_every_byte);
  RUN_TEST(test_mqtt_rx_joins_topic_and_payload_packets);
  RUN_TEST(test_mqtt_rx_size_limits_drop_and_count);
  RUN_TEST(test_mqtt_rx_full_queue_drops_newest_and_counts);
  RUN_TEST(test_mqtt_rx_during_command_is_queued);
  RUN_TEST(test_mqtt_callback_may_publish_from_poll);
  RUN_TEST(test_prompt_char_in_incoming_payload_is_not_the_prompt);
  RUN_TEST(test_concurrent_callers_keep_operations_whole);
  RUN_TEST(test_mqtt_connlost_urc_clears_flag);

  return UNITY_END();
}
