#pragma once

#include "hal_config.h"
#ifdef HAL_ENABLE_UART

/**
 * @file hal_uart.h
 * @brief Hardware abstraction for RP2040 hardware UART ports.
 *
 * The API is intentionally close to hal_swserial so both backends can be
 * swapped in application code with minimal changes.
 */

#include "hal_status.h"
#include "hal_uart_config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Available hardware UART ports on the RP2040. */
typedef enum {
  HAL_UART_PORT_1 = 1,
  HAL_UART_PORT_2 = 2,
} hal_uart_port_t;

typedef struct {
  uint32_t rx_overrun;
  /** Framing errors; STM32 noise errors are counted here too. */
  uint32_t rx_framing;
  uint32_t rx_parity;
  /** Explicit break condition when the backend exposes a break flag. */
  uint32_t rx_break;
  uint32_t rx_buffer_overflow;
} hal_uart_error_counters_t;

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
hal_uart_t hal_uart_create(hal_uart_port_t port, uint8_t rx_pin,
                           uint8_t tx_pin);

/** @brief Status-returning variant of hal_uart_set_rx(). */
hal_status_t hal_uart_set_rx_ex(hal_uart_t h, uint8_t rx_pin);

/**
 * @brief Reassign the RX pin.
 * @return true on success.
 */
bool hal_uart_set_rx(hal_uart_t h, uint8_t rx_pin);

/** @brief Status-returning variant of hal_uart_set_tx(). */
hal_status_t hal_uart_set_tx_ex(hal_uart_t h, uint8_t tx_pin);

/**
 * @brief Reassign the TX pin.
 * @return true on success.
 */
bool hal_uart_set_tx(hal_uart_t h, uint8_t tx_pin);

/**
 * @brief Start the UART with the given baud rate and frame config.
 *
 * Repeating this call reconfigures the port and clears its receive buffer and
 * error counters.
 *
 * @note On RP2040 this call installs the UART RX interrupt on the calling
 *       core. Reconfiguration and destruction must be performed from that
 *       same core. The current API does not expose or validate this implicit
 *       owner; in FreeRTOS/SMP builds call it from a task pinned to the
 *       intended core.
 *
 * @param config Frame format, e.g. HAL_UART_CFG_8N1.
 * @return HAL_OK on success; HAL_EINVAL for an invalid handle or zero baud;
 *         HAL_EBUSY when the backend could not enable reception.
 */
hal_status_t hal_uart_begin(hal_uart_t h, uint32_t baud, uint16_t config);

/** @brief Return the number of bytes available in the receive buffer. */
int hal_uart_available(hal_uart_t h);

/**
 * @brief Status-returning one-byte read helper.
 * @param out_value Destination byte. Must not be NULL.
 * @return HAL_OK when a byte was read, HAL_EAGAIN when no byte is available.
 */
hal_status_t hal_uart_read_ex(hal_uart_t h, uint8_t *out_value);

/** @brief Read one byte (0-255) or return -1 if empty. */
int hal_uart_read(hal_uart_t h);

/**
 * @brief Status-returning variant of hal_uart_write().
 *
 * @p out_written is optional and receives the number of bytes accepted for
 * transmission or captured by the backend.
 */
hal_status_t hal_uart_write_ex(hal_uart_t h, const uint8_t *data, size_t len,
                               size_t *out_written);

/**
 * @brief Write raw bytes.
 * @return Number of bytes accepted for transmission.
 */
size_t hal_uart_write(hal_uart_t h, const uint8_t *data, size_t len);

/** @brief Status-returning variant of hal_uart_println(). */
hal_status_t hal_uart_println_ex(hal_uart_t h, const char *s,
                                 size_t *out_written);

/** @brief Print a string followed by a line ending. */
size_t hal_uart_println(hal_uart_t h, const char *s);

/**
 * @brief Flush the transmit buffer, blocking until all bytes are sent.
 * @return HAL_OK when transmission is complete; HAL_EINVAL for an invalid
 *         handle; HAL_EUNINIT before begin() on backends that track it.
 */
hal_status_t hal_uart_flush(hal_uart_t h);

/**
 * @brief Status-returning variant of hal_uart_get_error_counters().
 *
 * Counters are reset by hal_uart_begin().
 */
hal_status_t
hal_uart_get_error_counters_ex(hal_uart_t h,
                               hal_uart_error_counters_t *counters);

/**
 * @brief Copy cumulative RX error counters.
 *
 * Counters are reset by hal_uart_begin().
 * @return true when counters were copied.
 */
bool hal_uart_get_error_counters(hal_uart_t h,
                                 hal_uart_error_counters_t *counters);

/**
 * @brief Release resources. The handle must not be used after this call.
 * @note On RP2040 call from the same core that successfully called
 *       hal_uart_begin(), because removal of the RX IRQ is core-local.
 */
void hal_uart_destroy(hal_uart_t h);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_UART */
