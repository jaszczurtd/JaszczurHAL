#include "gps_nmea_parser.h"
#include "hal/gps/hal_gps_nmea_utils.h"

#include <ctype.h>  /* isdigit */
#include <math.h>   /* sqrt */
#include <stdlib.h> /* atol */
#include <string.h>

/* Ported from TinyGPS++ (Mikal Hart, LGPL) - tokenizer / checksum / RMC / GGA.
 * GSA / GSV / GST field layout is just pure GNSS parser.
 * No HAL/Arduino dependency: age and threading are handled by the facade. */

#define GPS_KMPH_PER_KNOT 1.852

enum { SENT_OTHER = 0, SENT_RMC, SENT_GGA, SENT_GSA, SENT_GSV, SENT_GST };

static double raw_to_double(const gps_raw_degrees_t *d) {
  double v = d->deg + d->billionths / 1000000000.0;
  return d->negative ? -v : v;
}

/* ── Sentence-type detection by the 3-char suffix (any talker) ───────────── */

static uint8_t sentence_type_from(const char *term0) {
  size_t n = strlen(term0);
  if (n < 3)
    return SENT_OTHER;
  const char *s3 = term0 + (n - 3);
  if (!strcmp(s3, "RMC"))
    return SENT_RMC;
  if (!strcmp(s3, "GGA"))
    return SENT_GGA;
  if (!strcmp(s3, "GSA"))
    return SENT_GSA;
  if (!strcmp(s3, "GSV"))
    return SENT_GSV;
  if (!strcmp(s3, "GST"))
    return SENT_GST;
  return SENT_OTHER;
}

static uint8_t *inview_slot(gps_nmea_t *p) {
  if (!strcmp(p->talker, "GP"))
    return &p->inview_gp;
  if (!strcmp(p->talker, "GL"))
    return &p->inview_gl;
  if (!strcmp(p->talker, "GA"))
    return &p->inview_ga;
  if (!strcmp(p->talker, "GB"))
    return &p->inview_gb;
  return NULL;
}

/* ── Commit: copy staged values out once a sentence passes checksum ──────── */

static void commit_sentence(gps_nmea_t *p) {
  ++p->passed_checksum;
  if (p->sentence_has_fix)
    ++p->sentences_with_fix;

  switch (p->sentence_type) {
  case SENT_RMC:
    p->date = p->new_date;
    p->date_valid = true;
    p->time = p->new_time;
    p->time_valid = true;
    if (p->sentence_has_fix) {
      p->lat = p->new_lat;
      p->lng = p->new_lng;
      p->loc_valid = true;
      p->loc_updated = true;
      ++p->loc_commit_seq;
      p->speed = p->new_speed;
      p->course = p->new_course;
    }
    break;
  case SENT_GGA:
    p->time = p->new_time;
    p->time_valid = true;
    p->fix_quality = p->new_fix_quality;
    if (p->sentence_has_fix) {
      p->lat = p->new_lat;
      p->lng = p->new_lng;
      p->loc_valid = true;
      p->loc_updated = true;
      ++p->loc_commit_seq;
      p->altitude = p->new_altitude;
    }
    p->sats_used = p->new_sats_used;
    p->hdop = p->new_hdop;
    break;
  case SENT_GSA:
    p->fix_mode = p->new_fix_mode;
    p->pdop = p->new_pdop;
    p->hdop = p->new_hdop;
    p->vdop = p->new_vdop;
    break;
  case SENT_GSV: {
    uint8_t *slot = inview_slot(p);
    if (slot != NULL)
      *slot = p->new_sats_in_view;
    break;
  }
  case SENT_GST:
    p->gst_smaj = p->new_gst_smaj;
    p->gst_smin = p->new_gst_smin;
    break;
  default:
    break;
  }
}

/* Decode one finished term given (sentence_type, term_number). */
static void decode_term(gps_nmea_t *p) {
  const char *t = p->term;
  const uint8_t n = p->term_number;
  switch (p->sentence_type) {
  case SENT_RMC:
    switch (n) {
    case 1:
      p->new_time = (uint32_t)hal_gps_nmea_decimal_x100(t);
      break;
    case 2:
      p->sentence_has_fix = (t[0] == 'A');
      break;
    case 3:
      hal_gps_nmea_degrees(t, &p->new_lat.deg, &p->new_lat.billionths);
      p->new_lat.negative = false;
      break;
    case 4:
      p->new_lat.negative = (t[0] == 'S');
      break;
    case 5:
      hal_gps_nmea_degrees(t, &p->new_lng.deg, &p->new_lng.billionths);
      p->new_lng.negative = false;
      break;
    case 6:
      p->new_lng.negative = (t[0] == 'W');
      break;
    case 7:
      p->new_speed = hal_gps_nmea_decimal_x100(t);
      break;
    case 8:
      p->new_course = hal_gps_nmea_decimal_x100(t);
      break;
    case 9:
      p->new_date = (uint32_t)atol(t);
      break;
    default:
      break;
    }
    break;
  case SENT_GGA:
    switch (n) {
    case 1:
      p->new_time = (uint32_t)hal_gps_nmea_decimal_x100(t);
      break;
    case 2:
      hal_gps_nmea_degrees(t, &p->new_lat.deg, &p->new_lat.billionths);
      p->new_lat.negative = false;
      break;
    case 3:
      p->new_lat.negative = (t[0] == 'S');
      break;
    case 4:
      hal_gps_nmea_degrees(t, &p->new_lng.deg, &p->new_lng.billionths);
      p->new_lng.negative = false;
      break;
    case 5:
      p->new_lng.negative = (t[0] == 'W');
      break;
    case 6:
      p->new_fix_quality = (uint8_t)atol(t);
      p->sentence_has_fix = (t[0] > '0');
      break;
    case 7:
      p->new_sats_used = (uint32_t)atol(t);
      break;
    case 8:
      p->new_hdop = hal_gps_nmea_decimal_x100(t);
      break;
    case 9:
      p->new_altitude = hal_gps_nmea_decimal_x100(t);
      break;
    default:
      break;
    }
    break;
  case SENT_GSA:
    switch (n) {
    case 2:
      p->new_fix_mode = (uint8_t)atol(t);
      break;
    case 15:
      p->new_pdop = hal_gps_nmea_decimal_x100(t);
      break;
    case 16:
      p->new_hdop = hal_gps_nmea_decimal_x100(t);
      break;
    case 17:
      p->new_vdop = hal_gps_nmea_decimal_x100(t);
      break;
    default:
      break;
    }
    break;
  case SENT_GSV:
    if (n == 3)
      p->new_sats_in_view = (uint8_t)atol(t);
    break;
  case SENT_GST:
    if (n == 3)
      p->new_gst_smaj = hal_gps_nmea_decimal_x100(t);
    else if (n == 4)
      p->new_gst_smin = hal_gps_nmea_decimal_x100(t);
    break;
  default:
    break;
  }
}

/* Process a just-completed term. Returns true on a freshly validated sentence.
 */
static bool end_of_term(gps_nmea_t *p) {
  if (p->is_checksum_term) {
    uint8_t checksum = (uint8_t)(16 * hal_gps_nmea_hex_value(p->term[0]) +
                                 hal_gps_nmea_hex_value(p->term[1]));
    if (checksum == p->parity) {
      commit_sentence(p);
      return true;
    }
    ++p->failed_checksum;
    return false;
  }

  if (p->term_number == 0) {
    p->sentence_type = sentence_type_from(p->term);
    if (strlen(p->term) >= 5) { /* remember talker for GSV */
      p->talker[0] = p->term[0];
      p->talker[1] = p->term[1];
      p->talker[2] = '\0';
    } else {
      p->talker[0] = '\0';
    }
    return false;
  }

  if (p->sentence_type != SENT_OTHER && p->term[0]) {
    decode_term(p);
  }
  return false;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void gps_nmea_init(gps_nmea_t *p) { memset(p, 0, sizeof(*p)); }

bool gps_nmea_encode(gps_nmea_t *p, char c) {
  ++p->chars_processed;
  switch (c) {
  case ',':
    p->parity ^= (uint8_t)c;
    /* fallthrough: ',' also terminates a term */
    // fall through
  case '\r':
  case '\n':
  case '*': {
    bool committed = false;
    if (p->term_offset < sizeof(p->term)) {
      p->term[p->term_offset] = '\0';
      committed = end_of_term(p);
    }
    ++p->term_number;
    p->term_offset = 0;
    p->is_checksum_term = (c == '*');
    return committed;
  }
  case '$':
    p->term_number = 0;
    p->term_offset = 0;
    p->parity = 0;
    p->sentence_type = SENT_OTHER;
    p->is_checksum_term = false;
    p->sentence_has_fix = false;
    return false;
  default:
    if (p->term_offset < sizeof(p->term) - 1) {
      p->term[p->term_offset++] = c;
    }
    if (!p->is_checksum_term) {
      p->parity ^= (uint8_t)c;
    }
    return false;
  }
}

/* ── Accessors ───────────────────────────────────────────────────────────── */

double gps_nmea_latitude(const gps_nmea_t *p) { return raw_to_double(&p->lat); }
double gps_nmea_longitude(const gps_nmea_t *p) {
  return raw_to_double(&p->lng);
}
double gps_nmea_speed_kmph(const gps_nmea_t *p) {
  return GPS_KMPH_PER_KNOT * p->speed / 100.0;
}
double gps_nmea_course_deg(const gps_nmea_t *p) { return p->course / 100.0; }
double gps_nmea_altitude_m(const gps_nmea_t *p) { return p->altitude / 100.0; }
double gps_nmea_hdop(const gps_nmea_t *p) { return p->hdop / 100.0; }
double gps_nmea_vdop(const gps_nmea_t *p) { return p->vdop / 100.0; }
double gps_nmea_pdop(const gps_nmea_t *p) { return p->pdop / 100.0; }

double gps_nmea_horizontal_accuracy_m(const gps_nmea_t *p) {
  double maj = p->gst_smaj / 100.0;
  double min = p->gst_smin / 100.0;
  return sqrt(maj * maj + min * min);
}

uint8_t gps_nmea_satellites_in_view(const gps_nmea_t *p) {
  unsigned total =
      (unsigned)p->inview_gp + p->inview_gl + p->inview_ga + p->inview_gb;
  return (uint8_t)(total > 255u ? 255u : total);
}

int gps_nmea_year(const gps_nmea_t *p) { return (int)((p->date % 100) + 2000); }
int gps_nmea_month(const gps_nmea_t *p) { return (int)((p->date / 100) % 100); }
int gps_nmea_day(const gps_nmea_t *p) { return (int)(p->date / 10000); }
int gps_nmea_hour(const gps_nmea_t *p) { return (int)(p->time / 1000000); }
int gps_nmea_minute(const gps_nmea_t *p) {
  return (int)((p->time / 10000) % 100);
}
int gps_nmea_second(const gps_nmea_t *p) {
  return (int)((p->time / 100) % 100);
}
