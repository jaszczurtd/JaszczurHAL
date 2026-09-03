#pragma once

/** @file Scalar parsing helpers shared by NMEA decoders. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Convert one hexadecimal character to its numeric value. */
int hal_gps_nmea_hex_value(char value);

/** Parse a signed decimal as an integer scaled by 100. */
int32_t hal_gps_nmea_decimal_x100(const char *text);

/** Parse an NMEA DDMM.MMMM coordinate into degrees and billionths. */
void hal_gps_nmea_degrees(const char *text, int16_t *degrees,
                          uint32_t *billionths);

#ifdef __cplusplus
}
#endif
