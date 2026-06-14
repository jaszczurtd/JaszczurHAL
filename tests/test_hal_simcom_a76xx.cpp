#include "hal/hal_modem_at.h"
#include "hal/hal_simcom_a76xx.h"
#include "hal/hal_uart.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
  hal_mock_uart_push(
      s_uart,
      (const uint8_t
           *)"\r\n*ATREADY\r\n+CPIN: READY\r\nSMS DONE\r\nPB DONE\r\n",
      47);
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
  RUN_TEST(test_mqtt_connlost_urc_clears_flag);

  return UNITY_END();
}
