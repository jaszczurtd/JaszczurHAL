#pragma once

/** @file Scalar parsing helpers shared by NMEA decoders. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert one NMEA checksum hexadecimal character to a numeric value.
 * @param value Character in `0..9`, `A..F`, or `a..f`.
 * @return Numeric digit value. Input outside the documented ranges is not
 * validated and preserves the established subtraction behaviour.
 */
int hal_gps_nmea_hex_value(char value);

/**
 * @brief Parse a signed decimal value with two fractional digits.
 * @param text Non-NULL null-terminated decimal text.
 * @return Parsed value multiplied by 100; extra fractional digits are ignored.
 */
int32_t hal_gps_nmea_decimal_x100(const char *text);

/**
 * @brief Parse an NMEA `DDMM.MMMM` coordinate.
 * @param text Non-NULL null-terminated coordinate text.
 * @param degrees Receives the whole degrees.
 * @param billionths Receives the fractional degrees scaled by one billion.
 * If either output pointer is NULL, neither output is modified.
 */
void hal_gps_nmea_degrees(const char *text, int16_t *degrees,
                          uint32_t *billionths);

#ifdef __cplusplus
}
#endif
