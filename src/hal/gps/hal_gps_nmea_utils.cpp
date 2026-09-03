#include "hal/gps/hal_gps_nmea_utils.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

int hal_gps_nmea_hex_value(char value) {
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  return value - '0';
}

int32_t hal_gps_nmea_decimal_x100(const char *text) {
  const bool negative = *text == '-';
  if (negative) {
    ++text;
  }
  int32_t result = 100 * (int32_t)atol(text);
  while (isdigit((unsigned char)*text) != 0) {
    ++text;
  }
  if (*text == '.' && isdigit((unsigned char)text[1]) != 0) {
    result += 10 * (text[1] - '0');
    if (isdigit((unsigned char)text[2]) != 0) {
      result += text[2] - '0';
    }
  }
  return negative ? -result : result;
}

void hal_gps_nmea_degrees(const char *text, int16_t *degrees,
                          uint32_t *billionths) {
  if (degrees == nullptr || billionths == nullptr) {
    return;
  }
  const uint32_t left = (uint32_t)atol(text);
  const uint16_t minutes = (uint16_t)(left % 100u);
  uint32_t multiplier = UINT32_C(10000000);
  uint32_t ten_millionths = (uint32_t)minutes * multiplier;
  *degrees = (int16_t)(left / 100u);
  while (isdigit((unsigned char)*text) != 0) {
    ++text;
  }
  if (*text == '.') {
    while (isdigit((unsigned char)*++text) != 0) {
      multiplier /= 10u;
      ten_millionths += (uint32_t)(*text - '0') * multiplier;
    }
  }
  *billionths = (5u * ten_millionths + 1u) / 3u;
}
