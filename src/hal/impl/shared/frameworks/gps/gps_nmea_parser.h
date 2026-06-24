#pragma once

/**
 * @file gps_nmea_parser.h
 * @brief Portable NMEA-0183 parser for JaszczurHAL's GPS HAL.
 *
 * The tokenizer, checksum handling and RMC/GGA decoding logic is ported from
 * TinyGPS++ (Mikal Hart, LGPL) into a dependency-free form - no Arduino, no
 * millis(): position age is stamped by the hal_gps facade via hal_millis()
 * using @ref gps_nmea_t::loc_commit_seq. GSA / GSV / GST decoding (fix mode,
 * DOPs, satellites-in-view, horizontal accuracy) follows the standard NMEA-0183
 * field layouts.
 *
 * Feed bytes with gps_nmea_encode(); read the committed fields via the
 * accessors. The struct is plain data, so the parser is unit-testable on the
 * host without any HAL backend.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GPS_NMEA_TERM_SIZE 16

/* High-precision raw latitude/longitude (TinyGPS++ DDMM.MMMM representation).
 */
typedef struct {
  int16_t deg;
  uint32_t billionths;
  bool negative;
} gps_raw_degrees_t;

typedef struct {
  /* ── tokenizer state ─────────────────────────────────────────────── */
  uint8_t parity;
  bool is_checksum_term;
  char term[GPS_NMEA_TERM_SIZE];
  uint8_t term_number;
  uint8_t term_offset;
  uint8_t sentence_type; /* internal SENT_* code                  */
  char talker[3];        /* two-letter talker of the current GSV  */
  bool sentence_has_fix;

  /* ── staging (filled while parsing, copied out on a valid checksum) ── */
  gps_raw_degrees_t new_lat, new_lng;
  int32_t new_speed, new_course, new_altitude; /* value * 100         */
  int32_t new_hdop, new_vdop, new_pdop;        /* value * 100         */
  int32_t new_gst_smaj, new_gst_smin;          /* value * 100, metres */
  uint32_t new_date, new_time, new_sats_used;
  uint8_t new_fix_quality, new_fix_mode, new_sats_in_view;

  /* ── committed values (the public interface) ─────────────────────── */
  bool loc_valid, loc_updated;
  uint32_t loc_commit_seq; /* bumped on each location commit        */
  gps_raw_degrees_t lat, lng;

  bool date_valid, time_valid;
  uint32_t date, time; /* DDMMYY, HHMMSScc                 */

  int32_t speed, course, altitude; /* value * 100                      */
  int32_t hdop, vdop, pdop;        /* value * 100                      */
  int32_t gst_smaj, gst_smin;      /* value * 100, metres              */
  uint32_t sats_used;
  uint8_t fix_quality, fix_mode;
  uint8_t inview_gp, inview_gl, inview_ga, inview_gb;

  /* ── diagnostics ─────────────────────────────────────────────────── */
  uint32_t chars_processed;
  uint32_t passed_checksum, failed_checksum, sentences_with_fix;
} gps_nmea_t;

/** @brief Reset a parser to its initial state. */
void gps_nmea_init(gps_nmea_t *p);

/**
 * @brief Feed one received character into the parser.
 * @return true when a sentence has just passed checksum and been committed.
 */
bool gps_nmea_encode(gps_nmea_t *p, char c);

/* ── Convenience accessors over the committed fields ─────────────────── */
double gps_nmea_latitude(const gps_nmea_t *p);
double gps_nmea_longitude(const gps_nmea_t *p);
double gps_nmea_speed_kmph(const gps_nmea_t *p);
double gps_nmea_course_deg(const gps_nmea_t *p);
double gps_nmea_altitude_m(const gps_nmea_t *p);
double gps_nmea_hdop(const gps_nmea_t *p);
double gps_nmea_vdop(const gps_nmea_t *p);
double gps_nmea_pdop(const gps_nmea_t *p);
double gps_nmea_horizontal_accuracy_m(const gps_nmea_t *p);
uint8_t gps_nmea_satellites_in_view(const gps_nmea_t *p);
int gps_nmea_year(const gps_nmea_t *p);
int gps_nmea_month(const gps_nmea_t *p);
int gps_nmea_day(const gps_nmea_t *p);
int gps_nmea_hour(const gps_nmea_t *p);
int gps_nmea_minute(const gps_nmea_t *p);
int gps_nmea_second(const gps_nmea_t *p);

#ifdef __cplusplus
}
#endif
