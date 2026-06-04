#include "utils/unity.h"
#include "hal/impl/shared/gps_nmea_parser.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

/* Exercises the portable NMEA parser directly with real sentences (checksums
 * computed here so any body is accepted), asserting the decoded fields and the
 * ported field mappings - the part most prone to off-by-one term errors. */

static gps_nmea_t p;

void setUp(void)    { gps_nmea_init(&p); }
void tearDown(void) {}

/* Feed a sentence body (between '$' and '*'), appending a valid checksum. */
static bool feed(const char *body) {
    bool committed = false;
    uint8_t cs = 0;
    gps_nmea_encode(&p, '$');
    for (const char *c = body; *c; ++c) {
        cs ^= (uint8_t)*c;
        gps_nmea_encode(&p, *c);
    }
    char tail[8];
    snprintf(tail, sizeof(tail), "*%02X\r\n", cs);
    for (const char *c = tail; *c; ++c) {
        committed |= gps_nmea_encode(&p, *c);
    }
    return committed;
}

void test_gga_position_quality_alt(void) {
    TEST_ASSERT_TRUE(feed("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));
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
    TEST_ASSERT_TRUE(feed("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,100525,003.1,W"));
    TEST_ASSERT_TRUE(p.loc_valid);
    /* 22.4 knots -> 1.852 * 22.4 = 41.48 km/h */
    TEST_ASSERT_TRUE(fabs((gps_nmea_speed_kmph(&p)) - (41.48)) < 0.05);
    TEST_ASSERT_TRUE(fabs((gps_nmea_course_deg(&p)) - (84.4)) < 0.05);
    TEST_ASSERT_EQUAL_INT(2025, gps_nmea_year(&p));
    TEST_ASSERT_EQUAL_INT(5,  gps_nmea_month(&p));
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
    TEST_ASSERT_TRUE(feed("GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45"));
    TEST_ASSERT_EQUAL_UINT8(8, gps_nmea_satellites_in_view(&p));
    /* A second constellation adds to the total. */
    TEST_ASSERT_TRUE(feed("GLGSV,1,1,04,65,12,034,20,66,33,090,25,72,55,200,30,73,10,310,18"));
    TEST_ASSERT_EQUAL_UINT8(12, gps_nmea_satellites_in_view(&p));
}

void test_gst_horizontal_accuracy(void) {
    /* semi-major (term 3) and semi-minor (term 4) -> sqrt(maj^2 + min^2).
     * parse_decimal keeps 2 decimals: 0.023->0.02, 0.020->0.02. */
    TEST_ASSERT_TRUE(feed("GPGST,172814.0,0.006,0.023,0.020,273.6,0.023,0.020,0.031"));
    TEST_ASSERT_TRUE(fabs((gps_nmea_horizontal_accuracy_m(&p)) - (0.02828)) < 0.001);
}

void test_bad_checksum_is_rejected(void) {
    /* Append a deliberately wrong checksum. */
    const char *body = "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,";
    gps_nmea_encode(&p, '$');
    for (const char *c = body; *c; ++c) gps_nmea_encode(&p, *c);
    for (const char *c = "*00\r\n"; *c; ++c) gps_nmea_encode(&p, *c);

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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_gga_position_quality_alt);
    RUN_TEST(test_rmc_speed_course_datetime);
    RUN_TEST(test_gsa_fix_mode_and_dops);
    RUN_TEST(test_gsv_satellites_in_view);
    RUN_TEST(test_gst_horizontal_accuracy);
    RUN_TEST(test_bad_checksum_is_rejected);
    RUN_TEST(test_no_fix_keeps_location_invalid);
    return UNITY_END();
}
