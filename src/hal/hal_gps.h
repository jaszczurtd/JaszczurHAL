#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_GPS

/**
 * @file hal_gps.h
 * @brief Hardware abstraction for GPS NMEA receivers.
 *
 * Wraps an NMEA parser behind a platform-independent API. RP2040 supports a
 * PIO/DMA SoftwareSerial transport and an interrupt-driven hardware UART
 * transport; the mock lets tests inject location, speed, date and time
 * directly.
 *
 * Only one GPS instance is supported (singleton).
 */

#include "hal_uart_config.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialise the GPS subsystem.
 *
 * Only the first call has effect (singleton guard).  The implementation
 * automatically tries the alternate framing (8N1↔️7N1) if, after the first
 * ~500 received characters, every NMEA sentence fails its checksum.
 *
 * @note On RP2040 with HAL_GPS_TRANSPORT_UART, the UART RX interrupt is
 *       installed on the core executing this call. The current API does not
 *       expose or validate that implicit owner. Call from a task pinned to the
 *       intended core. HAL_GPS_TRANSPORT_SWSERIAL uses PIO/DMA and installs no
 *       CPU RX interrupt.
 *
 * @param rx_pin  GPIO pin for serial RX from the GPS module.
 * @param tx_pin  GPIO pin for serial TX to the GPS module.
 * @param baud    Baud rate (typically 9600).
 * @param config  UART frame format constant (e.g. HAL_UART_CFG_8N1).
 */
void hal_gps_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud,
                  uint16_t config);

/**
 * @brief Drain all available bytes from the serial port into the NMEA parser.
 *
 * Should be called frequently (e.g. every main-loop iteration) to prevent the
 * selected transport's receive buffer from overflowing. In multicore code,
 * keeping this call in the task/core that initialized GPS makes transport
 * ownership explicit.
 *
 * In the mock build this function is a no-op; use the inject helpers instead.
 */
void hal_gps_update(void);

/**
 * @brief Feed one byte of raw NMEA data into the parser.
 *
 * Hardware transports feed bytes to this function while their buffered input
 * is drained by hal_gps_update(); the NMEA parser itself does not run directly
 * in the RP2040 UART ISR. In mock builds this is a no-op (use inject functions
 * instead).
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

/* ── Extended fix data ───────────────────────────────────────────────────
 * Native fields come from GGA/RMC; the remainder are
 * parsed from GSA / GSV / GST. Each getter returns a safe default (0) when
 * the corresponding sentence has not been received. On the mock backend the
 * values are whatever the hal_mock_gps_set_* helpers injected. */

/** @brief Altitude above mean sea level in metres (GGA). 0.0 when no fix. */
double hal_gps_altitude_m(void);

/** @brief Course over ground in degrees, 0..360 (RMC/VTG). 0.0 when no fix. */
double hal_gps_course_deg(void);

/** @brief Satellites used in the navigation solution (GGA). */
uint32_t hal_gps_satellites_used(void);

/** @brief Satellites in view, summed across GP/GL/GA/GB talkers (GSV). */
uint8_t hal_gps_satellites_in_view(void);

/** @brief Horizontal dilution of precision (GGA/GSA). 0.0 when unavailable. */
double hal_gps_hdop(void);

/** @brief Vertical dilution of precision (GSA). 0.0 when unavailable. */
double hal_gps_vdop(void);

/** @brief Position (3-D) dilution of precision (GSA). 0.0 when unavailable. */
double hal_gps_pdop(void);

/** @brief GGA fix-quality indicator (0 = no fix, 1 = GPS, 2 = DGPS, ...). */
uint8_t hal_gps_fix_quality(void);

/** @brief GSA fix mode (1 = no fix, 2 = 2-D, 3 = 3-D). */
uint8_t hal_gps_fix_mode(void);

/**
 * @brief Estimated horizontal position accuracy in metres (GST).
 *
 * Computed as sqrt(semi_major^2 + semi_minor^2) from the GST error-ellipse
 * deviations. 0.0 when no GST sentence.
 */
double hal_gps_horizontal_accuracy_m(void);

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

#endif /* HAL_ENABLE_GPS */
#ifdef __cplusplus
}
#endif
