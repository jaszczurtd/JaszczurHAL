#pragma once

#include "hal_config.h"
#ifndef HAL_DISABLE_SWSERIAL

/**
 * @file hal_swserial.h
 * @brief Hardware abstraction for software (bit-banged) serial ports.
 *
 * Used to abstract peripheral serial buses such as GPS NMEA streams.
 */

#include <stdint.h>
#include <stddef.h>
#include "hal_uart_config.h"

/**
 * @brief Opaque handle for a software-UART instance.
 *
 * Obtain via hal_swserial_create(); release via hal_swserial_destroy().
 */
typedef struct hal_swserial_impl_s hal_swserial_impl_t;
typedef hal_swserial_impl_t *hal_swserial_t;

/**
 * @brief Create a software serial instance on the given RX/TX pins.
 * @param rx_pin Receive pin.
 * @param tx_pin Transmit pin.
 * @return Opaque handle, or NULL on failure / pool exhaustion.
 */
hal_swserial_t hal_swserial_create(uint8_t rx_pin, uint8_t tx_pin);

/**
 * @brief Reassign the RX pin before begin().
 * @param h      Handle from hal_swserial_create().
 * @param rx_pin Receive pin.
 * @return true on success, false on invalid handle or when already started.
 */
bool hal_swserial_set_rx(hal_swserial_t h, uint8_t rx_pin);

/**
 * @brief Reassign the TX pin before begin().
 * @param h      Handle from hal_swserial_create().
 * @param tx_pin Transmit pin.
 * @return true on success, false on invalid handle or when already started.
 */
bool hal_swserial_set_tx(hal_swserial_t h, uint8_t tx_pin);

/**
 * @brief Start the serial port.
 * @param h      Handle from hal_swserial_create().
 * @param baud   Baud rate.
 * @param config UART config word (e.g. HAL_UART_CFG_8N1).
 */
void hal_swserial_begin(hal_swserial_t h, uint32_t baud, uint16_t config);

/**
 * @brief Return the number of bytes available in the receive buffer.
 * @param h Handle.
 * @return Number of bytes ready to read.
 */
int hal_swserial_available(hal_swserial_t h);

/**
 * @brief Read one byte from the receive buffer.
 * @param h Handle.
 * @return Byte value (0-255) or -1 if empty.
 */
int hal_swserial_read(hal_swserial_t h);

/**
 * @brief Write raw bytes to the serial port.
 * @param h    Handle.
 * @param data Byte buffer.
 * @param len  Number of bytes to send.
 * @return Number of bytes accepted for transmission.
 */
size_t hal_swserial_write(hal_swserial_t h, const uint8_t *data, size_t len);

/**
 * @brief Print a string followed by a line ending.
 * @param h Handle.
 * @param s Null-terminated string (nullptr treated as empty string).
 * @return Number of payload bytes written, excluding line ending.
 */
size_t hal_swserial_println(hal_swserial_t h, const char *s);

/**
 * @brief Flush the transmit buffer, blocking until all bytes are sent.
 * @param h Handle.
 */
void hal_swserial_flush(hal_swserial_t h);

/**
 * @brief Release resources. The handle must not be used after this call.
 * @param h Handle to release.
 */
void hal_swserial_destroy(hal_swserial_t h);


#endif /* HAL_DISABLE_SWSERIAL */