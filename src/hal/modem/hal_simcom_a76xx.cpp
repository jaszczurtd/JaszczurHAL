#include "hal/modem/hal_simcom_a76xx.h"

#ifdef HAL_ENABLE_A7670

#include "hal/gpio/hal_gpio.h"
#include "hal/modem/hal_modem_at.h"
#include "hal/serial/hal_serial.h"
#include "hal/serial/hal_uart.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward decl from hal_time / hal_system. */
extern uint32_t hal_millis(void);
extern void hal_delay_ms(uint32_t ms);

#ifndef HAL_SIMCOM_A76XX_MAX_INSTANCES
#define HAL_SIMCOM_A76XX_MAX_INSTANCES 1
#endif

#ifndef HAL_SIMCOM_A76XX_TOPIC_MAX
#define HAL_SIMCOM_A76XX_TOPIC_MAX 128
#endif

#ifndef HAL_SIMCOM_A76XX_PAYLOAD_MAX
#define HAL_SIMCOM_A76XX_PAYLOAD_MAX 512
#endif

/* ── Internal state ──────────────────────────────────────────────────── */

struct hal_simcom_a76xx_impl_s {
  hal_simcom_a76xx_config_t cfg;
  hal_modem_at_t at;

  bool mqtt_connected[2];
  int mqtt_active_client;
  int mqtt_last_connect_result[2];

  /* MQTT-RX URC reassembly. The CMQTTRX* family delivers a single
     message as a four-URC sequence:
       +CMQTTRXSTART: <idx>,<topic_len>,<payload_len>
       <topic>
       +CMQTTRXTOPIC: <idx>,<topic_len>
       <topic_string>                 (variant-dependent)
       +CMQTTRXPAYLOAD: <idx>,<payload_len>
       <payload_bytes>
       +CMQTTRXEND: <idx>
     The exact line ordering varies between firmware revisions; we
     handle the common case by capturing the next non-URC line after
     RXTOPIC as the topic, and the next non-URC line after RXPAYLOAD
     as the payload. */
  struct {
    bool in_progress;
    int client_index;
    size_t topic_len_announced;
    size_t payload_len_announced;
    char topic[HAL_SIMCOM_A76XX_TOPIC_MAX];
    size_t topic_len;
    uint8_t payload[HAL_SIMCOM_A76XX_PAYLOAD_MAX];
    size_t payload_len;
    bool expect_topic_line;
    bool expect_payload_line;
    bool complete;
  } rx;

  hal_simcom_a76xx_mqtt_message_cb_t msg_cb;
  void *msg_cb_user;
  int dispatched_in_poll;

  bool cell_prev_fix_valid;
  float cell_prev_lat;
  float cell_prev_lon;
  uint32_t cell_prev_fix_ms;
  float cell_speed_kmh;

  bool gnss_powered;
  bool gnss_supported;

  bool in_use;
};

static hal_simcom_a76xx_impl_t s_pool[HAL_SIMCOM_A76XX_MAX_INSTANCES];

/* ── Helpers ─────────────────────────────────────────────────────────── */

static hal_simcom_a76xx_result_t map_at(hal_modem_at_result_t r) {
  switch (r) {
  case HAL_MODEM_AT_OK:
    return HAL_SIMCOM_A76XX_OK;
  case HAL_MODEM_AT_ERROR:
    return HAL_SIMCOM_A76XX_ERROR;
  case HAL_MODEM_AT_TIMEOUT:
    return HAL_SIMCOM_A76XX_TIMEOUT;
  case HAL_MODEM_AT_NO_PROMPT:
    return HAL_SIMCOM_A76XX_ERROR;
  case HAL_MODEM_AT_INVALID_ARG:
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  case HAL_MODEM_AT_BUSY:
    return HAL_SIMCOM_A76XX_NOT_READY;
  default:
    return HAL_SIMCOM_A76XX_ERROR;
  }
}

static bool parse_cmqtt_result(const char *line, const char *prefix,
                               int *client_index, int *result_code) {
  if (!line || !prefix || !client_index || !result_code)
    return false;
  const size_t prefix_len = strlen(prefix);
  if (strncmp(line, prefix, prefix_len) != 0)
    return false;

  int parsed_client = -1;
  int parsed_result = HAL_SIMCOM_A76XX_MQTT_RESULT_UNKNOWN;
  if (sscanf(line + prefix_len, " %d,%d", &parsed_client, &parsed_result) !=
      2) {
    return false;
  }
  if (parsed_client < 0 || parsed_client > 1 || parsed_result < 0)
    return false;

  *client_index = parsed_client;
  *result_code = parsed_result;
  return true;
}

const char *hal_simcom_a76xx_mqtt_result_string(int result_code) {
  static const char *const result_text[] = {
      "operation succeeded",
      "operation failed",
      "bad UTF-8 string",
      "socket connect failed",
      "socket create failed",
      "socket close failed",
      "message receive failed",
      "network open failed",
      "network close failed",
      "network not opened",
      "client index error",
      "no connection",
      "invalid parameter",
      "operation not supported",
      "client is busy",
      "connection acquisition failed",
      "socket send failed",
      "timeout",
      "topic is empty",
      "client is already in use",
      "client not acquired",
      "client not released",
      "length out of range",
      "network is already open",
      "packet error",
      "DNS error",
      "socket closed by server",
      "connection refused: unsupported protocol version",
      "connection refused: client identifier rejected",
      "connection refused: server unavailable",
      "connection refused: bad username or password",
      "connection refused: not authorized",
      "TLS handshake failed",
      "certificate not configured",
      "TLS session open failed",
      "server disconnect failed",
  };

  const size_t result_count = sizeof(result_text) / sizeof(result_text[0]);
  if (result_code < 0 || (size_t)result_code >= result_count)
    return "unknown MQTT result";
  return result_text[result_code];
}

/* Copy the +CLBS payload into `out`. Some modem firmwares fragment
   the URC across UART writes so a CRLF can land in the middle of a
   numeric field (e.g. the line shows up as
   "+CLBS: 0,50.2743\r\n72,19.124077,550"). When we encounter a CRLF
   sequence, peek at what follows: if it is another numeric/punctuation
   character belonging to the same payload, drop the CRLF and keep
   stitching; otherwise stop - that CRLF terminates the URC line.
   Returns true when a true line terminator was reached (so the caller
   knows the payload is complete), false when the buffer ended before
   a terminator was seen (possible truncation). */
static bool clbs_collapse_line(const char *src, char *out, size_t out_size,
                               size_t *out_len) {
  if (out_len)
    *out_len = 0;
  if (!src || !out || out_size == 0)
    return false;

  size_t w = 0;
  bool terminated = false;
  const char *p = src;

  while (*p && w + 1 < out_size) {
    if (*p == '\r' || *p == '\n') {
      const char *q = p;
      while (*q == '\r' || *q == '\n')
        ++q;
      char c = *q;
      bool continuation = (c == ',' || c == '.' || c == '+' || c == '-' ||
                           c == ' ' || c == '\t' || (c >= '0' && c <= '9'));
      /* '+' is ambiguous: it can be a sign in the next field, or
         the start of a new URC like "+CMQTTPUB:". If followed by
         a letter, it's a URC; if followed by a digit, sign of a
         number. */
      if (c == '+') {
        continuation = (q[1] >= '0' && q[1] <= '9');
      }
      if (!continuation) {
        terminated = true;
        break;
      }
      p = q;
      continue;
    }
    out[w++] = *p++;
  }
  out[w] = '\0';
  if (out_len)
    *out_len = w;
  return terminated;
}

static bool parse_clbs_from_response(const char *resp, int *status, float *lat,
                                     float *lon, int *acc) {
  if (!resp || !status || !lat || !lon || !acc)
    return false;

  /* Parse from the LAST occurrence so a stale, partial line in the
     buffer does not shadow a fresh, complete one. */
  const char *p = NULL;
  for (const char *cur = resp;;) {
    const char *hit = strstr(cur, "+CLBS:");
    if (!hit)
      break;
    p = hit;
    cur = hit + 6;
  }
  if (!p)
    return false;

  char buf[96];
  size_t buf_len = 0;
  bool terminated = clbs_collapse_line(p, buf, sizeof(buf), &buf_len);
  if (!terminated)
    return false; /* incomplete fragment - likely truncated */

  int consumed = 0;
  int st = -1;
  float la = 0.0f;
  float lo = 0.0f;
  int ac = -1;

  int n = sscanf(buf, "+CLBS: %d,%f,%f,%d%n", &st, &la, &lo, &ac, &consumed);
  if (n != 4) {
    consumed = 0;
    n = sscanf(buf, "+CLBS: %d, %f, %f, %d%n", &st, &la, &lo, &ac, &consumed);
  }
  if (n == 4 && (size_t)consumed == buf_len) {
    *status = st;
    *lat = la;
    *lon = lo;
    *acc = ac;
    return true;
  }

  consumed = 0;
  n = sscanf(buf, "+CLBS: %d,%f,%f%n", &st, &la, &lo, &consumed);
  if (n != 3) {
    consumed = 0;
    n = sscanf(buf, "+CLBS: %d, %f, %f%n", &st, &la, &lo, &consumed);
  }
  if (n == 3 && (size_t)consumed == buf_len) {
    *status = st;
    *lat = la;
    *lon = lo;
    *acc = -1;
    return true;
  }

  /* Status-only response (modem error variants like "+CLBS: 5"). */
  consumed = 0;
  n = sscanf(buf, "+CLBS: %d%n", &st, &consumed);
  if (n == 1 && (size_t)consumed == buf_len) {
    *status = st;
    *lat = 0.0f;
    *lon = 0.0f;
    *acc = -1;
    return true;
  }

  return false;
}

static void update_cell_speed_state(hal_simcom_a76xx_t h, float lat, float lon,
                                    uint32_t now_ms, float *out_speed_kmh) {
  const float max_speed_kmh = 220.0f;
  const float ema_alpha = 0.35f;

  if (!h || !out_speed_kmh)
    return;

  if (!h->cell_prev_fix_valid) {
    h->cell_prev_fix_valid = true;
    h->cell_prev_lat = lat;
    h->cell_prev_lon = lon;
    h->cell_prev_fix_ms = now_ms;
    h->cell_speed_kmh = -1.0f;
    *out_speed_kmh = -1.0f;
    return;
  }

  uint32_t dt_ms = now_ms - h->cell_prev_fix_ms;
  if (dt_ms == 0u) {
    *out_speed_kmh = h->cell_speed_kmh;
    return;
  }

  if (dt_ms < 3000u || dt_ms > 120000u) {
    h->cell_prev_lat = lat;
    h->cell_prev_lon = lon;
    h->cell_prev_fix_ms = now_ms;
    *out_speed_kmh = h->cell_speed_kmh;
    return;
  }

  const float meters_per_deg = 111320.0f;
  float mean_lat_rad = ((lat + h->cell_prev_lat) * 0.5f) * 0.01745329252f;
  float dlat_m = (lat - h->cell_prev_lat) * meters_per_deg;
  float dlon_m = (lon - h->cell_prev_lon) * meters_per_deg * cosf(mean_lat_rad);
  float dist_m = sqrtf(dlat_m * dlat_m + dlon_m * dlon_m);

  if (dist_m < 5.0f) {
    dist_m = 0.0f;
  }

  float speed_mps = dist_m / ((float)dt_ms / 1000.0f);
  float raw_speed_kmh = speed_mps * 3.6f;

  if (raw_speed_kmh > max_speed_kmh) {
    raw_speed_kmh = (h->cell_speed_kmh >= 0.0f) ? h->cell_speed_kmh : -1.0f;
  }

  if (raw_speed_kmh >= 0.0f) {
    if (h->cell_speed_kmh < 0.0f) {
      h->cell_speed_kmh = raw_speed_kmh;
    } else {
      h->cell_speed_kmh = (ema_alpha * raw_speed_kmh) +
                          ((1.0f - ema_alpha) * h->cell_speed_kmh);
    }
  }

  h->cell_prev_lat = lat;
  h->cell_prev_lon = lon;
  h->cell_prev_fix_ms = now_ms;
  *out_speed_kmh = h->cell_speed_kmh;
}

/* ── GNSS helpers ────────────────────────────────────────────────────── */

#define HAL_SIMCOM_A76XX_GNSS_MAX_TOKENS 32

static char *gnss_trim_token(char *s) {
  if (!s)
    return s;

  while (*s && isspace((unsigned char)*s)) {
    s++;
  }

  char *end = s + strlen(s);
  while (end > s && isspace((unsigned char)end[-1])) {
    *--end = '\0';
  }
  return s;
}

static size_t gnss_split_csv(char *line, char *tokens[], size_t max_tokens) {
  if (!line || !tokens || max_tokens == 0u)
    return 0u;

  size_t count = 0u;
  tokens[count++] = line;
  for (char *p = line; *p != '\0'; ++p) {
    if (*p == ',') {
      *p = '\0';
      if (count < max_tokens) {
        tokens[count++] = p + 1;
      }
    }
  }

  for (size_t i = 0; i < count; ++i) {
    tokens[i] = gnss_trim_token(tokens[i]);
  }
  return count;
}

static bool gnss_token_is_empty(const char *s) { return !s || *s == '\0'; }

static bool gnss_parse_double(const char *s, double *out) {
  if (gnss_token_is_empty(s) || !out)
    return false;

  char *end = NULL;
  double v = strtod(s, &end);
  if (end == s)
    return false;
  while (*end && isspace((unsigned char)*end)) {
    end++;
  }
  if (*end != '\0')
    return false;

  *out = v;
  return true;
}

static bool gnss_parse_int(const char *s, int *out) {
  if (gnss_token_is_empty(s) || !out)
    return false;

  char *end = NULL;
  long v = strtol(s, &end, 10);
  if (end == s)
    return false;
  while (*end && isspace((unsigned char)*end)) {
    end++;
  }
  if (*end != '\0')
    return false;

  *out = (int)v;
  return true;
}

static bool gnss_is_hemisphere(const char *s, char a, char b) {
  return s && s[0] != '\0' && s[1] == '\0' &&
         (s[0] == a || s[0] == b || s[0] == (char)tolower((unsigned char)a) ||
          s[0] == (char)tolower((unsigned char)b));
}

static bool gnss_parse_coordinate(const char *value, const char *hemisphere,
                                  bool latitude, double *out) {
  if (!hemisphere || !out)
    return false;
  if (!gnss_is_hemisphere(hemisphere, latitude ? 'N' : 'E',
                          latitude ? 'S' : 'W')) {
    return false;
  }

  double raw = 0.0;
  if (!gnss_parse_double(value, &raw))
    return false;

  const double limit = latitude ? 90.0 : 180.0;
  double abs_raw = fabs(raw);
  double deg = abs_raw;

  /* Some commands return ddmm.mmmm / dddmm.mmmm, others decimal degrees.
     Values outside the legal degree range must be NMEA-style. */
  if (abs_raw > limit) {
    double whole_deg = floor(abs_raw / 100.0);
    double minutes = abs_raw - (whole_deg * 100.0);
    if (minutes < 0.0 || minutes >= 60.0)
      return false;
    deg = whole_deg + (minutes / 60.0);
  }

  if (deg < 0.0 || deg > limit)
    return false;

  char hemi = (char)toupper((unsigned char)hemisphere[0]);
  if (hemi == 'S' || hemi == 'W') {
    deg = -deg;
  }

  *out = deg;
  return true;
}

static bool gnss_parse_decimal_lat_lon(const char *lat_s, const char *lon_s,
                                       double *lat, double *lon) {
  double la = 0.0;
  double lo = 0.0;
  if (!gnss_parse_double(lat_s, &la) || !gnss_parse_double(lon_s, &lo)) {
    return false;
  }
  if (la < -90.0 || la > 90.0 || lo < -180.0 || lo > 180.0) {
    return false;
  }
  *lat = la;
  *lon = lo;
  return true;
}

static bool gnss_copy_prefixed_line(const char *response, const char *prefix,
                                    char *out, size_t out_size) {
  if (!response || !prefix || !out || out_size == 0u)
    return false;

  const char *p = strstr(response, prefix);
  if (!p)
    return false;

  p += strlen(prefix);
  while (*p && isspace((unsigned char)*p)) {
    p++;
  }

  size_t i = 0u;
  while (p[i] && p[i] != '\r' && p[i] != '\n' && i + 1u < out_size) {
    out[i] = p[i];
    i++;
  }
  out[i] = '\0';
  return true;
}

static void gnss_copy_utc(hal_simcom_a76xx_gnss_location_t *loc,
                          const char *date_s, const char *time_s) {
  if (!loc || gnss_token_is_empty(date_s) || gnss_token_is_empty(time_s)) {
    return;
  }
  (void)snprintf(loc->utc, sizeof(loc->utc), "%sT%sZ", date_s, time_s);
}

static hal_simcom_a76xx_result_t
gnss_parse_cgnsinf(char *line, hal_simcom_a76xx_gnss_location_t *out) {
  char *tokens[HAL_SIMCOM_A76XX_GNSS_MAX_TOKENS] = {};
  size_t n = gnss_split_csv(line, tokens, HAL_SIMCOM_A76XX_GNSS_MAX_TOKENS);
  if (n < 5u)
    return HAL_SIMCOM_A76XX_PARSE;

  int run_status = 0;
  int fix_status = 0;
  (void)gnss_parse_int(tokens[0], &run_status);
  (void)gnss_parse_int(tokens[1], &fix_status);
  if (run_status == 0 || fix_status == 0) {
    return HAL_SIMCOM_A76XX_NOT_READY;
  }

  hal_simcom_a76xx_gnss_location_t loc;
  hal_simcom_a76xx_gnss_location_init(&loc);
  if (!gnss_parse_decimal_lat_lon(tokens[3], tokens[4], &loc.latitude_deg,
                                  &loc.longitude_deg)) {
    return HAL_SIMCOM_A76XX_NOT_READY;
  }

  if (n > 2u && !gnss_token_is_empty(tokens[2])) {
    (void)snprintf(loc.utc, sizeof(loc.utc), "%s", tokens[2]);
  }
  if (n > 5u)
    (void)gnss_parse_double(tokens[5], &loc.altitude_m);
  if (n > 6u)
    (void)gnss_parse_double(tokens[6], &loc.speed_kmh);
  if (n > 7u)
    (void)gnss_parse_double(tokens[7], &loc.course_deg);
  if (n > 8u)
    (void)gnss_parse_int(tokens[8], &loc.fix_mode);
  if (n > 10u)
    (void)gnss_parse_double(tokens[10], &loc.hdop);
  if (n > 11u)
    (void)gnss_parse_double(tokens[11], &loc.pdop);
  if (n > 12u)
    (void)gnss_parse_double(tokens[12], &loc.vdop);
  if (n > 14u)
    (void)gnss_parse_int(tokens[14], &loc.satellites_view);
  if (n > 15u)
    (void)gnss_parse_int(tokens[15], &loc.satellites_used);

  *out = loc;
  return HAL_SIMCOM_A76XX_OK;
}

static hal_simcom_a76xx_result_t
gnss_parse_cgpsinfo(char *line, hal_simcom_a76xx_gnss_location_t *out) {
  char *tokens[HAL_SIMCOM_A76XX_GNSS_MAX_TOKENS] = {};
  size_t n = gnss_split_csv(line, tokens, HAL_SIMCOM_A76XX_GNSS_MAX_TOKENS);
  if (n < 4u)
    return HAL_SIMCOM_A76XX_PARSE;
  if (gnss_token_is_empty(tokens[0]) || gnss_token_is_empty(tokens[2])) {
    return HAL_SIMCOM_A76XX_NOT_READY;
  }

  hal_simcom_a76xx_gnss_location_t loc;
  hal_simcom_a76xx_gnss_location_init(&loc);
  if (!gnss_parse_coordinate(tokens[0], tokens[1], true, &loc.latitude_deg) ||
      !gnss_parse_coordinate(tokens[2], tokens[3], false, &loc.longitude_deg)) {
    return HAL_SIMCOM_A76XX_NOT_READY;
  }

  if (n > 5u)
    gnss_copy_utc(&loc, tokens[4], tokens[5]);
  if (n > 6u)
    (void)gnss_parse_double(tokens[6], &loc.altitude_m);
  if (n > 7u) {
    double speed_knots = 0.0;
    if (gnss_parse_double(tokens[7], &speed_knots)) {
      loc.speed_kmh = speed_knots * 1.852;
    }
  }
  if (n > 8u)
    (void)gnss_parse_double(tokens[8], &loc.course_deg);

  *out = loc;
  return HAL_SIMCOM_A76XX_OK;
}

static hal_simcom_a76xx_result_t
gnss_parse_cgnssinfo(char *line, hal_simcom_a76xx_gnss_location_t *out) {
  char *tokens[HAL_SIMCOM_A76XX_GNSS_MAX_TOKENS] = {};
  size_t n = gnss_split_csv(line, tokens, HAL_SIMCOM_A76XX_GNSS_MAX_TOKENS);
  if (n < 4u)
    return HAL_SIMCOM_A76XX_PARSE;

  int lat_idx = -1;
  double lat = 0.0;
  double lon = 0.0;
  for (size_t i = 0u; i + 3u < n; ++i) {
    if (gnss_parse_coordinate(tokens[i], tokens[i + 1u], true, &lat) &&
        gnss_parse_coordinate(tokens[i + 2u], tokens[i + 3u], false, &lon)) {
      lat_idx = (int)i;
      break;
    }
  }
  if (lat_idx < 0)
    return HAL_SIMCOM_A76XX_NOT_READY;

  hal_simcom_a76xx_gnss_location_t loc;
  hal_simcom_a76xx_gnss_location_init(&loc);
  loc.latitude_deg = lat;
  loc.longitude_deg = lon;

  (void)gnss_parse_int(tokens[0], &loc.fix_mode);

  int sat_count = 0;
  bool have_sat_count = false;
  for (int i = 1; i < lat_idx; ++i) {
    int v = 0;
    if (gnss_parse_int(tokens[i], &v) && v >= 0 && v <= 64) {
      sat_count += v;
      have_sat_count = true;
    }
  }
  if (have_sat_count) {
    loc.satellites_view = sat_count;
    loc.satellites_used = sat_count;
  }

  size_t base = (size_t)lat_idx + 4u;
  if (n > base + 1u)
    gnss_copy_utc(&loc, tokens[base], tokens[base + 1u]);
  if (n > base + 2u)
    (void)gnss_parse_double(tokens[base + 2u], &loc.altitude_m);
  if (n > base + 3u)
    (void)gnss_parse_double(tokens[base + 3u], &loc.speed_kmh);
  if (n > base + 4u)
    (void)gnss_parse_double(tokens[base + 4u], &loc.course_deg);
  if (n > base + 5u)
    (void)gnss_parse_double(tokens[base + 5u], &loc.pdop);
  if (n > base + 6u)
    (void)gnss_parse_double(tokens[base + 6u], &loc.hdop);
  if (n > base + 7u)
    (void)gnss_parse_double(tokens[base + 7u], &loc.vdop);

  *out = loc;
  return HAL_SIMCOM_A76XX_OK;
}

static hal_simcom_a76xx_result_t
gnss_parse_response(const char *response,
                    hal_simcom_a76xx_gnss_location_t *out) {
  if (!response || !out)
    return HAL_SIMCOM_A76XX_INVALID_ARG;

  char line[512] = {};
  if (gnss_copy_prefixed_line(response, "+CGNSSINFO:", line, sizeof(line))) {
    return gnss_parse_cgnssinfo(line, out);
  }
  if (gnss_copy_prefixed_line(response, "+CGNSINF:", line, sizeof(line))) {
    return gnss_parse_cgnsinf(line, out);
  }
  if (gnss_copy_prefixed_line(response, "+CGPSINFO:", line, sizeof(line))) {
    return gnss_parse_cgpsinfo(line, out);
  }

  return HAL_SIMCOM_A76XX_PARSE;
}

/* ── MQTT-RX URC handlers ────────────────────────────────────────────── */

static void rx_reset(hal_simcom_a76xx_impl_t *h) {
  h->rx.in_progress = false;
  h->rx.client_index = -1;
  h->rx.topic_len_announced = 0;
  h->rx.payload_len_announced = 0;
  h->rx.topic_len = 0;
  h->rx.payload_len = 0;
  h->rx.topic[0] = '\0';
  h->rx.expect_topic_line = false;
  h->rx.expect_payload_line = false;
  h->rx.complete = false;
}

static void on_urc_rxstart(const char *line, void *user) {
  hal_simcom_a76xx_impl_t *h = (hal_simcom_a76xx_impl_t *)user;
  /* +CMQTTRXSTART: <idx>,<topic_len>,<payload_len> */
  int idx = 0, tl = 0, pl = 0;
  if (sscanf(line, "+CMQTTRXSTART: %d,%d,%d", &idx, &tl, &pl) >= 1) {
    rx_reset(h);
    h->rx.in_progress = true;
    h->rx.client_index = idx;
    h->rx.topic_len_announced = (tl > 0) ? (size_t)tl : 0u;
    h->rx.payload_len_announced = (pl > 0) ? (size_t)pl : 0u;
  }
}

static void on_urc_rxtopic(const char *line, void *user) {
  hal_simcom_a76xx_impl_t *h = (hal_simcom_a76xx_impl_t *)user;
  if (!h->rx.in_progress)
    return;
  /* +CMQTTRXTOPIC: <idx>,<topic_len>  - the actual topic is on the
     next non-URC line. Mark that we expect it. */
  int idx = 0, tl = 0;
  if (sscanf(line, "+CMQTTRXTOPIC: %d,%d", &idx, &tl) >= 1) {
    h->rx.topic_len_announced =
        (tl > 0) ? (size_t)tl : h->rx.topic_len_announced;
    h->rx.expect_topic_line = true;
  }
}

static void on_urc_rxpayload(const char *line, void *user) {
  hal_simcom_a76xx_impl_t *h = (hal_simcom_a76xx_impl_t *)user;
  if (!h->rx.in_progress)
    return;
  int idx = 0, pl = 0;
  if (sscanf(line, "+CMQTTRXPAYLOAD: %d,%d", &idx, &pl) >= 1) {
    h->rx.payload_len_announced =
        (pl > 0) ? (size_t)pl : h->rx.payload_len_announced;
    h->rx.expect_payload_line = true;
  }
}

static void on_urc_rxend(const char *line, void *user) {
  (void)line;
  hal_simcom_a76xx_impl_t *h = (hal_simcom_a76xx_impl_t *)user;
  if (!h->rx.in_progress)
    return;
  h->rx.complete = true;
}

/* Wildcard handler installed at "" - every URC line reaches us; we
   use it to capture the topic and payload text that follow the
   RXTOPIC / RXPAYLOAD announcements. Lines starting with '+' are
   skipped here (they go through the dedicated handlers). */
static void on_urc_any(const char *line, void *user) {
  hal_simcom_a76xx_impl_t *h = (hal_simcom_a76xx_impl_t *)user;
  if (!h->rx.in_progress)
    return;
  if (line[0] == '+' || line[0] == '\0')
    return;

  if (h->rx.expect_topic_line) {
    size_t n = strlen(line);
    if (n >= sizeof(h->rx.topic))
      n = sizeof(h->rx.topic) - 1u;
    memcpy(h->rx.topic, line, n);
    h->rx.topic[n] = '\0';
    h->rx.topic_len = n;
    h->rx.expect_topic_line = false;
    return;
  }
  if (h->rx.expect_payload_line) {
    size_t n = strlen(line);
    if (n > sizeof(h->rx.payload))
      n = sizeof(h->rx.payload);
    memcpy(h->rx.payload, line, n);
    h->rx.payload_len = n;
    h->rx.expect_payload_line = false;
    return;
  }
}

static void on_urc_disconn(const char *line, void *user) {
  (void)line;
  hal_simcom_a76xx_impl_t *h = (hal_simcom_a76xx_impl_t *)user;
  /* +CMQTTCONNLOST: <client_index>,<cause>  - best effort: clear both
     client flags so the application notices and reconnects. */
  int idx = 0, cause = 0;
  if (sscanf(line, "+CMQTTCONNLOST: %d,%d", &idx, &cause) >= 1 && idx >= 0 &&
      idx < 2) {
    h->mqtt_connected[idx] = false;
  } else {
    h->mqtt_connected[0] = false;
    h->mqtt_connected[1] = false;
  }
}

static void on_urc_mqtt_connect(const char *line, void *user) {
  hal_simcom_a76xx_impl_t *h = (hal_simcom_a76xx_impl_t *)user;
  int client_index = -1;
  int result_code = HAL_SIMCOM_A76XX_MQTT_RESULT_UNKNOWN;
  if (!h || !parse_cmqtt_result(line, "+CMQTTCONNECT:", &client_index,
                                &result_code)) {
    return;
  }

  h->mqtt_last_connect_result[client_index] = result_code;
  if (result_code != 0) {
    h->mqtt_connected[client_index] = false;
  }
}

static void install_mqtt_urcs(hal_simcom_a76xx_impl_t *h) {
  hal_modem_at_urc_register(h->at, "+CMQTTRXSTART:", on_urc_rxstart, h);
  hal_modem_at_urc_register(h->at, "+CMQTTRXTOPIC:", on_urc_rxtopic, h);
  hal_modem_at_urc_register(h->at, "+CMQTTRXPAYLOAD:", on_urc_rxpayload, h);
  hal_modem_at_urc_register(h->at, "+CMQTTRXEND:", on_urc_rxend, h);
  hal_modem_at_urc_register(h->at, "+CMQTTCONNLOST:", on_urc_disconn, h);
  hal_modem_at_urc_register(h->at, "+CMQTTCONNECT:", on_urc_mqtt_connect, h);
  /* Raw line observer captures the bare topic/payload lines that the
     SimCom CMQTTRX family emits between the announcement URCs. */
  hal_modem_at_set_line_observer(h->at, on_urc_any, h);
}

/* ── Lifecycle ───────────────────────────────────────────────────────── */

hal_simcom_a76xx_t
hal_simcom_a76xx_create(const hal_simcom_a76xx_config_t *cfg) {
  if (!cfg || !cfg->uart || !cfg->rx_buf || cfg->rx_buf_size < 256u) {
    return NULL;
  }

  hal_critical_section_enter();
  int slot = -1;
  for (int i = 0; i < HAL_SIMCOM_A76XX_MAX_INSTANCES; i++) {
    if (!s_pool[i].in_use) {
      slot = i;
      s_pool[i].in_use = true;
      break;
    }
  }
  hal_critical_section_exit();
  if (slot < 0)
    return NULL;

  hal_simcom_a76xx_impl_t *h = &s_pool[slot];
  memset(h, 0, sizeof(*h));
  h->in_use = true;
  h->cfg = *cfg;
  h->mqtt_active_client = -1;
  h->mqtt_last_connect_result[0] = HAL_SIMCOM_A76XX_MQTT_RESULT_UNKNOWN;
  h->mqtt_last_connect_result[1] = HAL_SIMCOM_A76XX_MQTT_RESULT_UNKNOWN;
  h->gnss_supported = true;
  rx_reset(h);

  hal_modem_at_config_t atc;
  memset(&atc, 0, sizeof(atc));
  atc.uart = cfg->uart;
  atc.rx_buf = cfg->rx_buf;
  atc.rx_buf_size = cfg->rx_buf_size;
  atc.default_timeout_ms =
      cfg->default_at_timeout_ms ? cfg->default_at_timeout_ms : 2000u;
  atc.quiet_window_ms = 200u;

  h->at = hal_modem_at_create(&atc);
  if (!h->at) {
    hal_critical_section_enter();
    h->in_use = false;
    hal_critical_section_exit();
    return NULL;
  }

  install_mqtt_urcs(h);
  return h;
}

void hal_simcom_a76xx_destroy(hal_simcom_a76xx_t h) {
  if (!h || !h->in_use)
    return;
  if (h->at) {
    hal_modem_at_destroy(h->at);
    h->at = NULL;
  }
  hal_critical_section_enter();
  h->in_use = false;
  hal_critical_section_exit();
}

/* ── Power ───────────────────────────────────────────────────────────── */

hal_simcom_a76xx_result_t hal_simcom_a76xx_power_toggle(hal_simcom_a76xx_t h,
                                                        uint32_t pulse_ms) {
  if (!h)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  h->gnss_powered = false;
  h->gnss_supported = true;
  if (h->cfg.pwr_pin < 0)
    return HAL_SIMCOM_A76XX_OK;
  hal_gpio_set_mode((uint8_t)h->cfg.pwr_pin, HAL_GPIO_OUTPUT);
  hal_gpio_write((uint8_t)h->cfg.pwr_pin, false);
  hal_modem_at_sleep_ms(h->at, pulse_ms ? pulse_ms : 1500u);
  hal_gpio_write((uint8_t)h->cfg.pwr_pin, true);
  return HAL_SIMCOM_A76XX_OK;
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_hard_reset(hal_simcom_a76xx_t h) {
  if (!h)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  if (h->cfg.pwr_pin < 0)
    return HAL_SIMCOM_A76XX_OK;
  (void)hal_simcom_a76xx_power_toggle(h, 1500u);
  hal_modem_at_sleep_ms(h->at, 5000u);
  (void)hal_simcom_a76xx_power_toggle(h, 1500u);
  hal_modem_at_sleep_ms(h->at, 5000u);
  return HAL_SIMCOM_A76XX_OK;
}

/* ── Boot wait ───────────────────────────────────────────────────────── */

typedef struct {
  uint32_t start_ms;
  uint32_t last_rx_ms;
  bool saw_ready;
  bool saw_done;
  bool saw_any;
  uint32_t ready_at_ms;
} boot_state_t;

static bool boot_ready(const char *buf, size_t len, void *user) {
  boot_state_t *st = (boot_state_t *)user;
  (void)len;

  if (!st->saw_done && (strstr(buf, "PB DONE") || strstr(buf, "SMS DONE"))) {
    st->saw_done = true;
  }
  if (!st->saw_ready &&
      (strstr(buf, "*ATREADY") || strstr(buf, "+CPIN: READY"))) {
    st->saw_ready = true;
    st->ready_at_ms = hal_millis();
  }
  if (buf[0] != '\0')
    st->saw_any = true;
  st->last_rx_ms = hal_millis();

  if (st->saw_done)
    return true;
  if (st->saw_ready && (hal_millis() - st->ready_at_ms) >= 2000u)
    return true;
  if (st->saw_any && (hal_millis() - st->last_rx_ms) >= 3000u)
    return true;
  return false;
}

hal_simcom_a76xx_result_t
hal_simcom_a76xx_wait_boot(hal_simcom_a76xx_t h, uint32_t total_timeout_ms) {
  if (!h)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  boot_state_t st;
  memset(&st, 0, sizeof(st));
  st.start_ms = hal_millis();
  st.last_rx_ms = st.start_ms;
  return map_at(
      hal_modem_at_listen_until(h->at, boot_ready, &st, total_timeout_ms));
}

/* ── Init / SIM / Network ────────────────────────────────────────────── */

hal_simcom_a76xx_result_t hal_simcom_a76xx_init(hal_simcom_a76xx_t h) {
  if (!h)
    return HAL_SIMCOM_A76XX_INVALID_ARG;

  bool at_ok = false;
  for (int i = 0; i < 10; i++) {
    if (hal_modem_at_send(h->at, "AT", "OK", 2000u) == HAL_MODEM_AT_OK) {
      at_ok = true;
      break;
    }
    hal_modem_at_sleep_ms(h->at, 1000u);
  }
  if (!at_ok)
    return HAL_SIMCOM_A76XX_TIMEOUT;

  (void)hal_modem_at_send(h->at, "ATE0", "OK", 3000u);
  /* AT+CLTS is firmware-variant dependent; ignore failures. */
  (void)hal_modem_at_send(h->at, "AT+CLTS=1", "OK", 3000u);
  (void)hal_modem_at_send(h->at, "AT+CEREG=0", "OK", 3000u);

  return HAL_SIMCOM_A76XX_OK;
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_wait_sim_ready(hal_simcom_a76xx_t h,
                                                          uint32_t timeout_ms) {
  if (!h)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  uint32_t start = hal_millis();
  do {
    if (hal_modem_at_send(h->at, "AT+CPIN?", "READY", 5000u) ==
        HAL_MODEM_AT_OK) {
      return HAL_SIMCOM_A76XX_OK;
    }
    hal_modem_at_sleep_ms(h->at, 1000u);
  } while ((hal_millis() - start) < timeout_ms);
  return HAL_SIMCOM_A76XX_TIMEOUT;
}

hal_simcom_a76xx_result_t
hal_simcom_a76xx_wait_network_registered(hal_simcom_a76xx_t h,
                                         uint32_t timeout_ms) {
  if (!h)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  uint32_t start = hal_millis();
  do {
    if (hal_modem_at_send(h->at, "AT+CREG?", "OK", 2000u) == HAL_MODEM_AT_OK) {
      const char *r = hal_modem_at_last_response(h->at);
      if (r && (strstr(r, "+CREG: 0,1") || strstr(r, "+CREG: 0,5"))) {
        return HAL_SIMCOM_A76XX_OK;
      }
    }
    hal_modem_at_sleep_ms(h->at, 2000u);
  } while ((hal_millis() - start) < timeout_ms);
  return HAL_SIMCOM_A76XX_TIMEOUT;
}

hal_simcom_a76xx_result_t
hal_simcom_a76xx_attach_pdp(hal_simcom_a76xx_t h,
                            const hal_simcom_a76xx_apn_t *apn) {
  if (!h || !apn || !apn->apn || !*apn->apn)
    return HAL_SIMCOM_A76XX_INVALID_ARG;

  char cmd[160];
  int n = snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn->apn);
  if (n <= 0 || (size_t)n >= sizeof(cmd))
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  hal_modem_at_result_t r = hal_modem_at_send(h->at, cmd, "OK", 5000u);
  if (r != HAL_MODEM_AT_OK)
    return map_at(r);

  r = hal_modem_at_send(h->at, "AT+CGACT=1,1", "OK", 10000u);
  return map_at(r);
}

/* ── Network time ────────────────────────────────────────────────────── */

hal_simcom_a76xx_result_t
hal_simcom_a76xx_get_network_time_iso8601(hal_simcom_a76xx_t h, char *out,
                                          size_t out_size) {
  if (!h || !out || out_size < 26u)
    return HAL_SIMCOM_A76XX_INVALID_ARG;

  hal_modem_at_result_t r =
      hal_modem_at_send(h->at, "AT+CCLK?", "+CCLK:", 3000u);
  if (r != HAL_MODEM_AT_OK)
    return map_at(r);

  const char *resp = hal_modem_at_last_response(h->at);
  if (!resp)
    return HAL_SIMCOM_A76XX_PARSE;

  const char *p = strstr(resp, "+CCLK: \"");
  if (!p)
    p = strstr(resp, "+CCLK:\"");
  if (!p)
    return HAL_SIMCOM_A76XX_PARSE;
  p = strchr(p, '"');
  if (!p)
    return HAL_SIMCOM_A76XX_PARSE;
  p++;

  int yy, mo, dd, hh, mm, ss;
  if (sscanf(p, "%d/%d/%d,%d:%d:%d", &yy, &mo, &dd, &hh, &mm, &ss) != 6) {
    return HAL_SIMCOM_A76XX_PARSE;
  }

  char tz_sign = '+';
  int tz_h = 0, tz_m = 0;
  if (strlen(p) > 17u && (p[17] == '+' || p[17] == '-')) {
    tz_sign = p[17];
    int tz_quarters = atoi(p + 18);
    if (tz_quarters < 0)
      tz_quarters = 0;
    if (tz_quarters > 56)
      tz_quarters = 56;
    int tz_total_min = tz_quarters * 15;
    tz_h = tz_total_min / 60;
    tz_m = tz_total_min % 60;
  }

  int wn = snprintf(out, out_size, "20%02d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
                    yy, mo, dd, hh, mm, ss, tz_sign, tz_h, tz_m);
  if (wn <= 0 || (size_t)wn >= out_size)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  return HAL_SIMCOM_A76XX_OK;
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_get_cell_location(
    hal_simcom_a76xx_t h, hal_simcom_a76xx_cell_location_t *out_location,
    uint32_t timeout_ms) {
  if (!h || !out_location)
    return HAL_SIMCOM_A76XX_INVALID_ARG;

  hal_modem_at_result_t r = hal_modem_at_send(
      h->at, "AT+CLBS=1,1", "+CLBS:", timeout_ms ? timeout_ms : 12000u);
  if (r != HAL_MODEM_AT_OK)
    return map_at(r);

  const char *resp = hal_modem_at_last_response(h->at);
  if (!resp)
    return HAL_SIMCOM_A76XX_PARSE;

  int status = -1;
  float lat = 0.0f;
  float lon = 0.0f;
  int acc = -1;

  bool parsed = parse_clbs_from_response(resp, &status, &lat, &lon, &acc);

  if (!parsed) {
    /* Some A7670 firmware builds split the +CLBS URC across multiple
       UART writes, and `AT+CLBS=1,1` is also unusual in that the
       trailing "\r\nOK\r\n" arrives BEFORE the "+CLBS:" payload.
       That means the at_send() tail-grace window observes the OK
       that came from earlier and returns while only the head of the
       URC is in the buffer. Continue draining WITHOUT resetting so
       the prefix (including the "+CLBS:" marker and any partial
       digits) is preserved and the remaining bytes are appended. */
    (void)hal_modem_at_listen_more(h->at, NULL, NULL, 1500u);
    resp = hal_modem_at_last_response(h->at);
    if (!resp)
      return HAL_SIMCOM_A76XX_PARSE;
    parsed = parse_clbs_from_response(resp, &status, &lat, &lon, &acc);
  }

  if (!parsed || status != 0)
    return HAL_SIMCOM_A76XX_PARSE;

  out_location->latitude_deg = lat;
  out_location->longitude_deg = lon;
  out_location->accuracy_m = acc;
  update_cell_speed_state(h, lat, lon, hal_millis(), &out_location->speed_kmh);
  return HAL_SIMCOM_A76XX_OK;
}

void hal_simcom_a76xx_gnss_location_init(
    hal_simcom_a76xx_gnss_location_t *loc) {
  if (!loc)
    return;
  memset(loc, 0, sizeof(*loc));
  loc->altitude_m = -1.0;
  loc->speed_kmh = -1.0;
  loc->course_deg = -1.0;
  loc->hdop = -1.0;
  loc->pdop = -1.0;
  loc->vdop = -1.0;
  loc->satellites_used = -1;
  loc->satellites_view = -1;
  loc->fix_mode = -1;
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_gnss_power_on(hal_simcom_a76xx_t h,
                                                         uint32_t timeout_ms) {
  if (!h)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  if (h->gnss_powered)
    return HAL_SIMCOM_A76XX_OK;
  if (!h->gnss_supported)
    return HAL_SIMCOM_A76XX_NOT_READY;

  static const char *power_cmds[] = {
      "AT+CGNSSPWR=1", "AT+CGNSSPWR=1,1", "AT+CGNSPWR=1",
      "AT+CGPS=1,1",   "AT+CGPS=1",
  };

  uint32_t per_cmd_timeout =
      timeout_ms ? timeout_ms : h->cfg.default_at_timeout_ms;
  if (per_cmd_timeout == 0u)
    per_cmd_timeout = 3000u;

  hal_simcom_a76xx_result_t last = HAL_SIMCOM_A76XX_ERROR;
  for (size_t i = 0u; i < sizeof(power_cmds) / sizeof(power_cmds[0]); ++i) {
    hal_modem_at_result_t r =
        hal_modem_at_send(h->at, power_cmds[i], NULL, per_cmd_timeout);
    last = map_at(r);
    if (last == HAL_SIMCOM_A76XX_OK) {
      h->gnss_powered = true;
      h->gnss_supported = true;
      return HAL_SIMCOM_A76XX_OK;
    }
  }

  h->gnss_supported = false;
  return last;
}

bool hal_simcom_a76xx_gnss_is_powered(hal_simcom_a76xx_t h) {
  return h && h->gnss_powered;
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_get_gnss_location(
    hal_simcom_a76xx_t h, hal_simcom_a76xx_gnss_location_t *out_location,
    uint32_t timeout_ms) {
  if (!h || !out_location)
    return HAL_SIMCOM_A76XX_INVALID_ARG;

  hal_simcom_a76xx_result_t pwr = hal_simcom_a76xx_gnss_power_on(h, timeout_ms);
  if (pwr != HAL_SIMCOM_A76XX_OK)
    return pwr;

  typedef struct {
    const char *cmd;
    const char *prefix;
  } gnss_query_t;

  static const gnss_query_t queries[] = {
      {"AT+CGNSSINFO", "+CGNSSINFO:"},
      {"AT+CGNSINF", "+CGNSINF:"},
      {"AT+CGPSINFO", "+CGPSINFO:"},
  };

  uint32_t per_query_timeout =
      timeout_ms ? timeout_ms : h->cfg.default_at_timeout_ms;
  if (per_query_timeout == 0u)
    per_query_timeout = 3000u;

  hal_simcom_a76xx_result_t last = HAL_SIMCOM_A76XX_ERROR;
  for (size_t i = 0u; i < sizeof(queries) / sizeof(queries[0]); ++i) {
    hal_modem_at_result_t r = hal_modem_at_send(
        h->at, queries[i].cmd, queries[i].prefix, per_query_timeout);
    last = map_at(r);
    if (last != HAL_SIMCOM_A76XX_OK) {
      continue;
    }

    hal_simcom_a76xx_gnss_location_t loc;
    hal_simcom_a76xx_gnss_location_init(&loc);
    const char *resp = hal_modem_at_last_response(h->at);
    hal_simcom_a76xx_result_t pr = gnss_parse_response(resp, &loc);
    if (pr == HAL_SIMCOM_A76XX_OK) {
      *out_location = loc;
      return HAL_SIMCOM_A76XX_OK;
    }

    last = pr;
    if (pr == HAL_SIMCOM_A76XX_NOT_READY) {
      return pr;
    }
  }

  return last;
}

hal_modem_at_t hal_simcom_a76xx_get_at(hal_simcom_a76xx_t h) {
  return h ? h->at : NULL;
}

/* ── MQTT ────────────────────────────────────────────────────────────── */

static hal_simcom_a76xx_result_t
apply_ssl(hal_simcom_a76xx_t h, const hal_simcom_a76xx_ssl_config_t *s) {
  char cmd[128];
  int n;

  n = snprintf(cmd, sizeof(cmd), "AT+CSSLCFG=\"sslversion\",%d,%d",
               s->ssl_context_id, s->sslversion ? s->sslversion : 4);
  if (n <= 0 || (size_t)n >= sizeof(cmd))
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  (void)hal_modem_at_send(h->at, cmd, "OK", 5000u);

  n = snprintf(cmd, sizeof(cmd), "AT+CSSLCFG=\"authmode\",%d,%d",
               s->ssl_context_id, s->authmode ? s->authmode : 1);
  if (n <= 0 || (size_t)n >= sizeof(cmd))
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  (void)hal_modem_at_send(h->at, cmd, "OK", 5000u);

  if (s->ca_cert_name && *s->ca_cert_name) {
    n = snprintf(cmd, sizeof(cmd), "AT+CSSLCFG=\"cacert\",%d,\"%s\"",
                 s->ssl_context_id, s->ca_cert_name);
    if (n <= 0 || (size_t)n >= sizeof(cmd))
      return HAL_SIMCOM_A76XX_INVALID_ARG;
    (void)hal_modem_at_send(h->at, cmd, "OK", 5000u);
  }

  n = snprintf(cmd, sizeof(cmd), "AT+CSSLCFG=\"ignorelocaltime\",%d,%d",
               s->ssl_context_id, s->ignore_local_time ? 1 : 0);
  if (n <= 0 || (size_t)n >= sizeof(cmd))
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  (void)hal_modem_at_send(h->at, cmd, "OK", 5000u);

  n = snprintf(cmd, sizeof(cmd), "AT+CSSLCFG=\"enableSNI\",%d,%d",
               s->ssl_context_id, s->enable_sni ? 1 : 0);
  if (n <= 0 || (size_t)n >= sizeof(cmd))
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  (void)hal_modem_at_send(h->at, cmd, "OK", 5000u);

  return HAL_SIMCOM_A76XX_OK;
}

hal_simcom_a76xx_result_t
hal_simcom_a76xx_mqtt_connect(hal_simcom_a76xx_t h,
                              const hal_simcom_a76xx_mqtt_config_t *cfg) {
  if (!h || !cfg || !cfg->broker_host || !cfg->client_id)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  if (cfg->client_index < 0 || cfg->client_index > 1)
    return HAL_SIMCOM_A76XX_INVALID_ARG;

  char cmd[320];
  int n;
  int ci = cfg->client_index;
  h->mqtt_last_connect_result[ci] = HAL_SIMCOM_A76XX_MQTT_RESULT_UNKNOWN;

  /* Tear down any previous session (errors ignored). */
  n = snprintf(cmd, sizeof(cmd), "AT+CMQTTDISC=%d,10", ci);
  if (n > 0)
    (void)hal_modem_at_send(h->at, cmd, "OK", 3000u);
  n = snprintf(cmd, sizeof(cmd), "AT+CMQTTREL=%d", ci);
  if (n > 0)
    (void)hal_modem_at_send(h->at, cmd, "OK", 2000u);
  (void)hal_modem_at_send(h->at, "AT+CMQTTSTOP", "OK", 3000u);
  hal_modem_at_sleep_ms(h->at, 1000u);

  if (cfg->ssl.enabled) {
    hal_simcom_a76xx_result_t sr = apply_ssl(h, &cfg->ssl);
    if (sr != HAL_SIMCOM_A76XX_OK)
      return sr;
  }

  hal_modem_at_result_t r =
      hal_modem_at_send(h->at, "AT+CMQTTSTART", "+CMQTTSTART: 0", 5000u);
  if (r != HAL_MODEM_AT_OK)
    return map_at(r);

  n = snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=%d,\"%s\",%d", ci,
               cfg->client_id, cfg->ssl.enabled ? 1 : 0);
  if (n <= 0 || (size_t)n >= sizeof(cmd))
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  r = hal_modem_at_send(h->at, cmd, "OK", 5000u);
  if (r != HAL_MODEM_AT_OK)
    return map_at(r);

  if (cfg->ssl.enabled) {
    n = snprintf(cmd, sizeof(cmd), "AT+CMQTTSSLCFG=%d,%d", ci,
                 cfg->ssl.ssl_context_id);
    if (n > 0)
      (void)hal_modem_at_send(h->at, cmd, "OK", 5000u);
  }

  char expected[32];
  snprintf(expected, sizeof(expected), "+CMQTTCONNECT: %d,0", ci);

  if (cfg->username && cfg->password) {
    n = snprintf(cmd, sizeof(cmd),
                 "AT+CMQTTCONNECT=%d,\"tcp://%s:%u\",%u,%d,\"%s\",\"%s\"", ci,
                 cfg->broker_host, (unsigned)cfg->broker_port,
                 (unsigned)cfg->keepalive_s, cfg->clean_session ? 1 : 0,
                 cfg->username, cfg->password);
  } else {
    n = snprintf(cmd, sizeof(cmd), "AT+CMQTTCONNECT=%d,\"tcp://%s:%u\",%u,%d",
                 ci, cfg->broker_host, (unsigned)cfg->broker_port,
                 (unsigned)cfg->keepalive_s, cfg->clean_session ? 1 : 0);
  }
  if (n <= 0 || (size_t)n >= sizeof(cmd))
    return HAL_SIMCOM_A76XX_INVALID_ARG;

  r = hal_modem_at_send(h->at, cmd, expected, 15000u);
  if (r != HAL_MODEM_AT_OK) {
    h->mqtt_connected[ci] = false;
    const int result_code = h->mqtt_last_connect_result[ci];
    if (result_code == HAL_SIMCOM_A76XX_MQTT_RESULT_UNKNOWN) {
      hal_derr("[SIMCOM][MQTT] connect failed before result URC "
               "(client=%d, AT result=%d)",
               ci, (int)r);
    } else {
      hal_derr("[SIMCOM][MQTT] connect failed: %s (client=%d, code=%d)",
               hal_simcom_a76xx_mqtt_result_string(result_code), ci,
               result_code);
    }
    return map_at(r);
  }

  h->mqtt_last_connect_result[ci] = 0;
  h->mqtt_connected[ci] = true;
  h->mqtt_active_client = ci;
  hal_deb("[SIMCOM][MQTT] connected successfully (client=%d, code=0)", ci);
  return HAL_SIMCOM_A76XX_OK;
}

int hal_simcom_a76xx_mqtt_last_connect_result(hal_simcom_a76xx_t h,
                                              int client_index) {
  if (!h || client_index < 0 || client_index > 1)
    return HAL_SIMCOM_A76XX_MQTT_RESULT_UNKNOWN;
  return h->mqtt_last_connect_result[client_index];
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_disconnect(hal_simcom_a76xx_t h,
                                                           int client_index) {
  if (!h || client_index < 0 || client_index > 1)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+CMQTTDISC=%d,10", client_index);
  hal_modem_at_result_t r = hal_modem_at_send(h->at, cmd, "OK", 5000u);
  h->mqtt_connected[client_index] = false;
  return map_at(r);
}

hal_simcom_a76xx_result_t
hal_simcom_a76xx_mqtt_publish(hal_simcom_a76xx_t h, int client_index,
                              const char *topic, const void *payload,
                              size_t payload_len, int qos) {
  if (!h || client_index < 0 || client_index > 1)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  if (!topic || (!payload && payload_len > 0))
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  if (qos < 0 || qos > 2)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  if (!h->mqtt_connected[client_index])
    return HAL_SIMCOM_A76XX_NOT_READY;

  char cmd[64];
  int n;
  hal_modem_at_result_t r;

  /* Topic */
  n = snprintf(cmd, sizeof(cmd), "AT+CMQTTTOPIC=%d,%u", client_index,
               (unsigned)strlen(topic));
  if (n <= 0 || (size_t)n >= sizeof(cmd))
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  r = hal_modem_at_send_with_data(h->at, cmd, (const uint8_t *)topic,
                                  strlen(topic), 1000u, 5000u);
  if (r != HAL_MODEM_AT_OK)
    return map_at(r);

  /* Payload */
  n = snprintf(cmd, sizeof(cmd), "AT+CMQTTPAYLOAD=%d,%u", client_index,
               (unsigned)payload_len);
  if (n <= 0 || (size_t)n >= sizeof(cmd))
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  r = hal_modem_at_send_with_data(h->at, cmd, (const uint8_t *)payload,
                                  payload_len, 1000u, 5000u);
  if (r != HAL_MODEM_AT_OK)
    return map_at(r);

  /* Pub. retained=0, timeout=60s. */
  n = snprintf(cmd, sizeof(cmd), "AT+CMQTTPUB=%d,%d,60", client_index, qos);
  if (n <= 0 || (size_t)n >= sizeof(cmd))
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  char expected[32];
  snprintf(expected, sizeof(expected), "+CMQTTPUB: %d,0", client_index);
  r = hal_modem_at_send(h->at, cmd, expected, 10000u);
  return map_at(r);
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_subscribe(hal_simcom_a76xx_t h,
                                                          int client_index,
                                                          const char *topic,
                                                          int qos) {
  if (!h || client_index < 0 || client_index > 1)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  if (!topic || !*topic)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  if (qos < 0 || qos > 2)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  if (!h->mqtt_connected[client_index])
    return HAL_SIMCOM_A76XX_NOT_READY;

  char cmd[64];
  int n = snprintf(cmd, sizeof(cmd), "AT+CMQTTSUBTOPIC=%d,%u,%d", client_index,
                   (unsigned)strlen(topic), qos);
  if (n <= 0 || (size_t)n >= sizeof(cmd))
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  hal_modem_at_result_t r = hal_modem_at_send_with_data(
      h->at, cmd, (const uint8_t *)topic, strlen(topic), 1000u, 5000u);
  if (r != HAL_MODEM_AT_OK)
    return map_at(r);

  n = snprintf(cmd, sizeof(cmd), "AT+CMQTTSUB=%d", client_index);
  if (n <= 0 || (size_t)n >= sizeof(cmd))
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  char expected[32];
  snprintf(expected, sizeof(expected), "+CMQTTSUB: %d,0", client_index);
  r = hal_modem_at_send(h->at, cmd, expected, 10000u);
  return map_at(r);
}

hal_simcom_a76xx_result_t
hal_simcom_a76xx_mqtt_unsubscribe(hal_simcom_a76xx_t h, int client_index,
                                  const char *topic) {
  if (!h || client_index < 0 || client_index > 1)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  if (!topic || !*topic)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  if (!h->mqtt_connected[client_index])
    return HAL_SIMCOM_A76XX_NOT_READY;

  char cmd[64];
  int n = snprintf(cmd, sizeof(cmd), "AT+CMQTTUNSUBTOPIC=%d,%u", client_index,
                   (unsigned)strlen(topic));
  if (n <= 0 || (size_t)n >= sizeof(cmd))
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  hal_modem_at_result_t r = hal_modem_at_send_with_data(
      h->at, cmd, (const uint8_t *)topic, strlen(topic), 1000u, 5000u);
  if (r != HAL_MODEM_AT_OK)
    return map_at(r);

  n = snprintf(cmd, sizeof(cmd), "AT+CMQTTUNSUB=%d", client_index);
  if (n <= 0 || (size_t)n >= sizeof(cmd))
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  char expected[32];
  snprintf(expected, sizeof(expected), "+CMQTTUNSUB: %d,0", client_index);
  r = hal_modem_at_send(h->at, cmd, expected, 10000u);
  return map_at(r);
}

hal_simcom_a76xx_result_t hal_simcom_a76xx_mqtt_set_message_callback(
    hal_simcom_a76xx_t h, hal_simcom_a76xx_mqtt_message_cb_t cb, void *user) {
  if (!h)
    return HAL_SIMCOM_A76XX_INVALID_ARG;
  h->msg_cb = cb;
  h->msg_cb_user = user;
  return HAL_SIMCOM_A76XX_OK;
}

int hal_simcom_a76xx_mqtt_poll(hal_simcom_a76xx_t h) {
  if (!h)
    return 0;
  h->dispatched_in_poll = 0;
  (void)hal_modem_at_urc_poll(h->at);

  if (h->rx.complete) {
    if (h->msg_cb && h->rx.topic_len > 0) {
      h->msg_cb(h->rx.client_index, h->rx.topic, h->rx.payload,
                h->rx.payload_len, h->msg_cb_user);
    }
    rx_reset(h);
    h->dispatched_in_poll++;
  }

  return h->dispatched_in_poll;
}

bool hal_simcom_a76xx_mqtt_is_connected(hal_simcom_a76xx_t h,
                                        int client_index) {
  if (!h || client_index < 0 || client_index > 1)
    return false;
  return h->mqtt_connected[client_index];
}

#endif /* HAL_ENABLE_A7670 */
