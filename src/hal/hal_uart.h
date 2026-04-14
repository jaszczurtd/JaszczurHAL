#pragma once

#include "hal_config.h"
#ifndef HAL_DISABLE_UART

/**
 * @file hal_uart.h
 * @brief Hardware abstraction for RP2040 hardware UART ports.
 *
 * The API is intentionally close to hal_swserial so both backends can be
 * swapped in application code with minimal changes.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hal_uart_config.h"

/** @brief Available hardware UART ports on the RP2040. */
typedef enum {
    HAL_UART_PORT_1 = 1,
    HAL_UART_PORT_2 = 2,
} hal_uart_port_t;

/** @brief Opaque handle for a hardware UART instance. */
typedef struct hal_uart_impl_s hal_uart_impl_t;
typedef hal_uart_impl_t *hal_uart_t;

/**
 * @brief Create a hardware UART instance.
 * @param port   UART peripheral (HAL_UART_PORT_1 or HAL_UART_PORT_2).
 * @param rx_pin GPIO pin for RX.
 * @param tx_pin GPIO pin for TX.
 * @return Opaque handle, or NULL on failure / pool exhaustion.
 */
hal_uart_t hal_uart_create(hal_uart_port_t port, uint8_t rx_pin, uint8_t tx_pin);

/**
 * @brief Reassign the RX pin (Arduino: calls SerialUART::setRX).
 * @return true on success.
 */
bool hal_uart_set_rx(hal_uart_t h, uint8_t rx_pin);

/**
 * @brief Reassign the TX pin (Arduino: calls SerialUART::setTX).
 * @return true on success.
 */
bool hal_uart_set_tx(hal_uart_t h, uint8_t tx_pin);

/**
 * @brief Start the UART with the given baud rate and frame config.
 * @param config Frame format, e.g. HAL_UART_CFG_8N1.
 */
void hal_uart_begin(hal_uart_t h, uint32_t baud, uint16_t config);

/** @brief Return the number of bytes available in the receive buffer. */
int hal_uart_available(hal_uart_t h);

/** @brief Read one byte (0–255) or return -1 if empty. */
int hal_uart_read(hal_uart_t h);

/**
 * @brief Write raw bytes.
 * @return Number of bytes accepted for transmission.
 */
size_t hal_uart_write(hal_uart_t h, const uint8_t *data, size_t len);

/** @brief Print a string followed by a line ending. */
size_t hal_uart_println(hal_uart_t h, const char *s);

/** @brief Flush the transmit buffer, blocking until all bytes are sent. */
void hal_uart_flush(hal_uart_t h);

/** @brief Release resources. The handle must not be used after this call. */
void hal_uart_destroy(hal_uart_t h);

#endif /* HAL_DISABLE_UART */