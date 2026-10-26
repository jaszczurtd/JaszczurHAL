#include "hal/core/hal_array.h"
#include "hal/gps/gps_nmea_parser.h"
#include "utils/unity.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Exercises the portable NMEA parser directly with real sentences (checksums
 * computed here so any body is accepted), asserting the decoded fields and the
 * ported field mappings - the part most prone to off-by-one term errors. */

static gps_nmea_t p;

void setUp(void) { gps_nmea_init(&p); }
void tearDown(void) {}

/* Feed @p len bytes of a sentence body (between '$' and '*'), appending a
 * valid checksum. The length lets a body carry NUL bytes. */
static bool feed_n(const char *body, size_t len) {
  bool committed = false;
  uint8_t cs = 0;
  gps_nmea_encode(&p, '$');
  for (size_t i = 0; i < len; ++i) {
    cs ^= (uint8_t)body[i];
    gps_nmea_encode(&p, body[i]);
  }
  char tail[8];
  snprintf(tail, sizeof(tail), "*%02X\r\n", cs);
  for (const char *c = tail; *c; ++c) {
    committed |= gps_nmea_encode(&p, *c);
  }
  return committed;
}

static bool feed(const char *body) { return feed_n(body, strlen(body)); }

/* String literal body that may contain NUL bytes. */
#define FEED_BYTES(literal) feed_n(literal, sizeof(literal) - 1u)

/* Feed a sentence body followed by a deliberately wrong checksum. */
static void feed_bad_checksum(const char *body) {
  gps_nmea_encode(&p, '$');
  for (const char *c = body; *c; ++c)
    gps_nmea_encode(&p, *c);
  for (const char *c = "*00\r\n"; *c; ++c)
    gps_nmea_encode(&p, *c);
}

void test_gga_position_quality_alt(void) {
  TEST_ASSERT_TRUE(
      feed("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));
  TEST_ASSERT_TRUE(p.loc_valid);
  TEST_ASSERT_TRUE(fabs((gps_nmea_latitude(&p)) - (48.1173)) < 0.0005);
  TEST_ASSERT_TRUE(fabs((gps_nmea_longitude(&p)) - (11.5166)) < 0.0005);
  TEST_ASSERT_EQUAL_UINT8(1, p.fix_quality);
  TEST_ASSERT_EQUAL_UINT32(8, p.sats_used);
  TEST_ASSERT_TRUE(fabs((gps_nmea_hdop(&p)) - (0.9)) < 0.001);
  TEST_ASSERT_TRUE(fabs((gps_nmea_altitude_m(&p)) - (545.4)) < 0.01);
  TEST_ASSERT_EQUAL_UINT32(1, p.passed_checksum);
}

void test_rmc_speed_course_datetime(void) {
  TEST_ASSERT_TRUE(
      feed("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,100525,003.1,W"));
  TEST_ASSERT_TRUE(p.loc_valid);
  /* 22.4 knots -> 1.852 * 22.4 = 41.48 km/h */
  TEST_ASSERT_TRUE(fabs((gps_nmea_speed_kmph(&p)) - (41.48)) < 0.05);
  TEST_ASSERT_TRUE(fabs((gps_nmea_course_deg(&p)) - (84.4)) < 0.05);
  TEST_ASSERT_EQUAL_INT(2025, gps_nmea_year(&p));
  TEST_ASSERT_EQUAL_INT(5, gps_nmea_month(&p));
  TEST_ASSERT_EQUAL_INT(10, gps_nmea_day(&p));
  TEST_ASSERT_EQUAL_INT(12, gps_nmea_hour(&p));
  TEST_ASSERT_EQUAL_INT(35, gps_nmea_minute(&p));
  TEST_ASSERT_EQUAL_INT(19, gps_nmea_second(&p));
}

void test_gsa_fix_mode_and_dops(void) {
  TEST_ASSERT_TRUE(feed("GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1"));
  TEST_ASSERT_EQUAL_UINT8(3, p.fix_mode);
  TEST_ASSERT_TRUE(fabs((gps_nmea_pdop(&p)) - (2.5)) < 0.001);
  TEST_ASSERT_TRUE(fabs((gps_nmea_hdop(&p)) - (1.3)) < 0.001);
  TEST_ASSERT_TRUE(fabs((gps_nmea_vdop(&p)) - (2.1)) < 0.001);
}

void test_gsv_satellites_in_view(void) {
  TEST_ASSERT_TRUE(
      feed("GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45"));
  TEST_ASSERT_EQUAL_UINT8(8, gps_nmea_satellites_in_view(&p));
  /* A second constellation adds to the total. */
  TEST_ASSERT_TRUE(
      feed("GLGSV,1,1,04,65,12,034,20,66,33,090,25,72,55,200,30,73,10,310,18"));
  TEST_ASSERT_EQUAL_UINT8(12, gps_nmea_satellites_in_view(&p));
}

void test_gst_horizontal_accuracy(void) {
  /* semi-major (term 3) and semi-minor (term 4) -> sqrt(maj^2 + min^2).
   * hal_gps_nmea_decimal_x100 keeps 2 decimals: 0.023->0.02, 0.020->0.02. */
  TEST_ASSERT_TRUE(
      feed("GPGST,172814.0,0.006,0.023,0.020,273.6,0.023,0.020,0.031"));
  TEST_ASSERT_TRUE(fabs((gps_nmea_horizontal_accuracy_m(&p)) - (0.02828)) <
                   0.001);
}

void test_bad_checksum_is_rejected(void) {
  feed_bad_checksum(
      "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");

  TEST_ASSERT_FALSE(p.loc_valid);
  TEST_ASSERT_EQUAL_UINT32(0, p.passed_checksum);
  TEST_ASSERT_EQUAL_UINT32(1, p.failed_checksum);
}

void test_no_fix_keeps_location_invalid(void) {
  /* GGA with fix quality 0 must not commit a location. */
  TEST_ASSERT_TRUE(feed("GPGGA,123519,4807.038,N,01131.000,E,0,00,,,M,,M,,"));
  TEST_ASSERT_FALSE(p.loc_valid);
  TEST_ASSERT_EQUAL_UINT8(0, p.fix_quality);
}

/* RMC body with a fix at the Munich test position and the given fields. */
static bool feed_rmc(const char *status, const char *time, const char *date) {
  char body[96];
  snprintf(body, sizeof(body),
           "GPRMC,%s,%s,4807.038,N,01131.000,E,022.4,084.4,%s,003.1,W", time,
           status, date);
  return feed(body);
}

static void assert_utc(unsigned year, unsigned month, unsigned day,
                       unsigned hour, unsigned minute, unsigned second) {
  TEST_ASSERT_TRUE(p.utc_valid);
  TEST_ASSERT_EQUAL_UINT16(year, p.utc.year);
  TEST_ASSERT_EQUAL_UINT8(month, p.utc.month);
  TEST_ASSERT_EQUAL_UINT8(day, p.utc.day);
  TEST_ASSERT_EQUAL_UINT8(hour, p.utc.hour);
  TEST_ASSERT_EQUAL_UINT8(minute, p.utc.minute);
  TEST_ASSERT_EQUAL_UINT8(second, p.utc.second);
}

void test_rmc_with_fix_commits_utc(void) {
  TEST_ASSERT_FALSE(p.utc_valid);
  TEST_ASSERT_TRUE(feed_rmc("A", "123519.50", "100525"));
  assert_utc(2025u, 5u, 10u, 12u, 35u, 19u);
  TEST_ASSERT_EQUAL_UINT32(1u, p.utc_commit_seq);
  TEST_ASSERT_TRUE(feed_rmc("A", "123520", "100525"));
  assert_utc(2025u, 5u, 10u, 12u, 35u, 20u);
  TEST_ASSERT_EQUAL_UINT32(2u, p.utc_commit_seq);
}

void test_utc_ignores_gga_time_and_rmc_without_fix(void) {
  TEST_ASSERT_TRUE(feed_rmc("A", "235959", "311225"));
  /* Past midnight GGA time alone would pair 00:00:01 with the old date. */
  TEST_ASSERT_TRUE(
      feed("GPGGA,000001,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));
  TEST_ASSERT_TRUE(feed_rmc("V", "000002", "010126"));
  assert_utc(2025u, 12u, 31u, 23u, 59u, 59u);
  TEST_ASSERT_EQUAL_UINT32(1u, p.utc_commit_seq);
}

void test_utc_needs_date_and_time_of_the_same_sentence(void) {
  TEST_ASSERT_TRUE(feed_rmc("A", "123519", "100525"));
  /* The previous sentence's staged date or time must not fill the gap. */
  TEST_ASSERT_TRUE(feed_rmc("A", "123600", ""));
  TEST_ASSERT_TRUE(feed_rmc("A", "", "110525"));
  assert_utc(2025u, 5u, 10u, 12u, 35u, 19u);
  TEST_ASSERT_EQUAL_UINT32(1u, p.utc_commit_seq);
}

void test_utc_rejects_invalid_calendar_values(void) {
  static const char *const bad[][2] = {
      {"120000", "310425"}, /* 31 April */
      {"120000", "290225"}, /* 29 February outside leap year */
      {"120000", "001025"}, /* day 0 */
      {"120000", "101325"}, /* month 13 */
      {"240000", "100525"}, /* hour 24 */
      {"126000", "100525"}, /* minute 60 */
      {"235960", "311225"}, /* leap second */
  };
  for (size_t i = 0; i < COUNTOF(bad); ++i) {
    TEST_ASSERT_TRUE(feed_rmc("A", bad[i][0], bad[i][1]));
    TEST_ASSERT_FALSE_MESSAGE(p.utc_valid, bad[i][1]);
  }
  TEST_ASSERT_EQUAL_UINT32(0u, p.utc_commit_seq);
}

void test_utc_rejects_malformed_fields(void) {
  static const char *const bad[][3] = {
      /* status, time, date */
      {"A", "garbage", "100525"},
      {"A", "", "100525"},
      {"A", "12351", "100525"},
      {"A", "1235199", "100525"}, /* hour 123 */
      {"A", "2612345", "100525"}, /* hour 261 would wrap to 5 */
      {"A", "-12351", "100525"},
      {"A", "123519.", "100525"},
      {"A", "123519.x", "100525"},
      {"A", "123519x5", "100525"},
      {"A", "123519.5x", "100525"},
      {"A", "123519.1234567", "100525"},
      {"A", "123519.000000000000", "100525"}, /* cut by the term buffer */
      {"A", "123519", "100525oops"},
      {"A", "123519", "10052"},
      {"A", "123519", "2570525"}, /* day 257 would wrap to 1 */
      {"A", "123519", "+10525"},
      {"AV", "123519", "100525"},
  };
  for (size_t i = 0; i < COUNTOF(bad); ++i) {
    TEST_ASSERT_TRUE(feed_rmc(bad[i][0], bad[i][1], bad[i][2]));
    TEST_ASSERT_FALSE_MESSAGE(p.utc_valid, bad[i][1]);
  }
  TEST_ASSERT_EQUAL_UINT32(0u, p.utc_commit_seq);
}

void test_utc_rejects_nul_inside_fields(void) {
  TEST_ASSERT_TRUE(feed_rmc("A", "123519", "100525"));
  TEST_ASSERT_TRUE(FEED_BYTES("GPRMC,123600\0oops,A,4807.038,N,01131.000,E,"
                              "022.4,084.4,100525,003.1,W"));
  TEST_ASSERT_TRUE(FEED_BYTES("GPRMC,123600,A\0x,4807.038,N,01131.000,E,"
                              "022.4,084.4,100525,003.1,W"));
  TEST_ASSERT_TRUE(FEED_BYTES("GPRMC,123600,A,4807.038,N,01131.000,E,"
                              "022.4,084.4,100525\0x,003.1,W"));
  assert_utc(2025u, 5u, 10u, 12u, 35u, 19u);
  TEST_ASSERT_EQUAL_UINT32(1u, p.utc_commit_seq);
}

void test_utc_accepts_fractions_of_a_second(void) {
  TEST_ASSERT_TRUE(feed_rmc("A", "123519.5", "100525"));
  assert_utc(2025u, 5u, 10u, 12u, 35u, 19u);
  TEST_ASSERT_TRUE(feed_rmc("A", "123520.123456", "100525"));
  assert_utc(2025u, 5u, 10u, 12u, 35u, 20u);
  TEST_ASSERT_EQUAL_UINT32(2u, p.utc_commit_seq);
}

void test_utc_calendar_boundaries(void) {
  TEST_ASSERT_TRUE(feed_rmc("A", "000000", "010100"));
  assert_utc(2000u, 1u, 1u, 0u, 0u, 0u);
  TEST_ASSERT_TRUE(feed_rmc("A", "120000", "290224"));
  assert_utc(2024u, 2u, 29u, 12u, 0u, 0u);
  TEST_ASSERT_TRUE(feed_rmc("A", "235959", "311299"));
  assert_utc(2099u, 12u, 31u, 23u, 59u, 59u);
}

void test_utc_from_ublox_sentence_burst(void) {
  /* Field layout of a u-blox NEO receiver (fraction .00, five-decimal minutes,
   * empty course, mode indicator), with the sample position. The GGA second
   * is moved on purpose: only the RMC may set the UTC value. */
  TEST_ASSERT_TRUE(feed("GPRMC,234913.00,A,4807.03800,N,01131.00000,E,0.188,,"
                        "251026,,,A"));
  TEST_ASSERT_TRUE(feed("GPVTG,,T,,M,0.188,N,0.348,K,A"));
  TEST_ASSERT_TRUE(feed("GPGGA,234914.00,4807.03800,N,01131.00000,E,1,08,1.21,"
                        "176.4,M,40.2,M,,"));
  TEST_ASSERT_TRUE(
      feed("GPGSA,A,3,16,23,32,10,27,01,08,18,,,,,1.95,1.21,1.52"));
  TEST_ASSERT_TRUE(feed("GPGLL,4807.03800,N,01131.00000,E,234914.00,A,A"));
  assert_utc(2026u, 10u, 25u, 23u, 49u, 13u);
  TEST_ASSERT_EQUAL_UINT32(1u, p.utc_commit_seq);
  TEST_ASSERT_TRUE(fabs(gps_nmea_latitude(&p) - 48.1173) < 0.0005);
}

void test_utc_ignores_bad_checksum(void) {
  feed_bad_checksum(
      "GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,100525,003.1,W");

  TEST_ASSERT_FALSE(p.utc_valid);
  TEST_ASSERT_EQUAL_UINT32(0u, p.utc_commit_seq);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_gga_position_quality_alt);
  RUN_TEST(test_rmc_speed_course_datetime);
  RUN_TEST(test_gsa_fix_mode_and_dops);
  RUN_TEST(test_gsv_satellites_in_view);
  RUN_TEST(test_gst_horizontal_accuracy);
  RUN_TEST(test_bad_checksum_is_rejected);
  RUN_TEST(test_no_fix_keeps_location_invalid);
  RUN_TEST(test_rmc_with_fix_commits_utc);
  RUN_TEST(test_utc_ignores_gga_time_and_rmc_without_fix);
  RUN_TEST(test_utc_needs_date_and_time_of_the_same_sentence);
  RUN_TEST(test_utc_rejects_invalid_calendar_values);
  RUN_TEST(test_utc_rejects_malformed_fields);
  RUN_TEST(test_utc_rejects_nul_inside_fields);
  RUN_TEST(test_utc_accepts_fractions_of_a_second);
  RUN_TEST(test_utc_calendar_boundaries);
  RUN_TEST(test_utc_from_ublox_sentence_burst);
  RUN_TEST(test_utc_ignores_bad_checksum);
  return UNITY_END();
}
