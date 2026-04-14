#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifndef HAL_DISABLE_GPS

/**
 * @file hal_gps.h
 * @brief Hardware abstraction for GPS NMEA receivers.
 *
 * Wraps an NMEA parser (e.g. TinyGPS++) behind a platform-independent API.
 * The real implementation drives the parser from a software-serial stream;
 * the mock lets tests inject location, speed, date and time directly.
 *
 * Only one GPS instance is supported (singleton).
 */

#include <stdint.h>
#include <stdbool.h>
#include "hal_uart_config.h"

/**
 * @brief Initialise the GPS subsystem.
 *
 * Only the first call has effect (singleton guard).  The implementation
 * automatically tries the alternate framing (8N1↔️7N1) if, after the first
 * ~500 received characters, every NMEA sentence fails its checksum.
 *
 * @param rx_pin  GPIO pin for serial RX from the GPS module.
 * @param tx_pin  GPIO pin for serial TX to the GPS module.
 * @param baud    Baud rate (typically 9600).
 * @param config  UART frame format constant (e.g. HAL_UART_CFG_8N1).
 */
void hal_gps_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud, uint16_t config);

/**
 * @brief Drain all available bytes from the serial port into the NMEA parser.
 *
 * Should be called frequently (e.g. every main-loop iteration) to prevent
 * the PIO/SoftwareSerial receive buffer from overflowing.
 *
 * In the mock build this function is a no-op; use the inject helpers instead.
 */
void hal_gps_update(void);

/**
 * @brief Feed one byte of raw NMEA data into the parser.
 *
 * In the real build this is called from the serial-RX interrupt.
 * In mock builds it is a no-op (use inject functions instead).
 *
 * @param c Byte received from the GPS module.
 */
void hal_gps_encode(char c);

/**
 * @brief Check whether the GPS has a valid position fix.
 * @return true if the last parsed sentence contained a valid location.
 */
bool hal_gps_location_is_valid(void);

/**
 * @brief Check whether the location has been updated since the last query.
 * @return true if new position data arrived since the previous call.
 */
bool hal_gps_location_is_updated(void);

/**
 * @brief Age (in ms) of the most recent valid location fix.
 * @return Milliseconds since the last valid location sentence,
 *         or UINT32_MAX if no fix was ever obtained.
 */
uint32_t hal_gps_location_age(void);

/** @brief Latitude in degrees (negative = south).  */
double hal_gps_latitude(void);

/** @brief Longitude in degrees (negative = west).  */
double hal_gps_longitude(void);

/** @brief Ground speed in km/h. Returns 0.0 when no fix. */
double hal_gps_speed_kmph(void);

/** @brief Four-digit year from the GPS date sentence. */
int hal_gps_date_year(void);
/** @brief Month (1-12). */
int hal_gps_date_month(void);
/** @brief Day of the month (1-31). */
int hal_gps_date_day(void);
/** @brief Hour (0-23 UTC). */
int hal_gps_time_hour(void);
/** @brief Minute (0-59). */
int hal_gps_time_minute(void);
/** @brief Second (0-59). */
int hal_gps_time_second(void);

/* ── Diagnostics (for debugging GPS reception issues) ────────────────── */

/** @brief Total characters fed into the NMEA parser since init. */
uint32_t hal_gps_chars_processed(void);

/** @brief Number of NMEA sentences that passed checksum validation. */
uint32_t hal_gps_passed_checksum(void);

/** @brief Number of NMEA sentences that failed checksum validation. */
uint32_t hal_gps_failed_checksum(void);

/** @brief Number of valid sentences that contained a location fix. */
uint32_t hal_gps_sentences_with_fix(void);

/** @brief Bytes currently waiting in the underlying serial RX buffer. */
int hal_gps_serial_available(void);


#endif /* HAL_DISABLE_GPS */
#ifdef __cplusplus
}
#endif
