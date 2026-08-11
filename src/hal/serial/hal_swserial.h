#pragma once

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_SWSERIAL

/**
 * @file hal_swserial.h
 * @brief Hardware abstraction for target-optimized software UART ports.
 *
 * Used to abstract peripheral serial buses such as GPS NMEA streams. RP2040
 * uses a native Pico SDK PIO/DMA backend; other targets may use a shared GPIO
 * implementation.
 */

#include "hal/core/hal_status.h"
#include "hal/serial/hal_uart_config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle for a software-UART instance.
 *
 * Obtain via hal_swserial_create(); release via hal_swserial_destroy().
 */
typedef struct hal_swserial_impl_s hal_swserial_impl_t;
typedef hal_swserial_impl_t *hal_swserial_t;

/**
 * @brief Create a software serial instance on the given RX/TX pins.
 *
 * The status form owns argument validation and resource allocation. On any
 * failure, @p out_handle is set to NULL when it is non-NULL.
 *
 * @param rx_pin Receive pin.
 * @param tx_pin Transmit pin.
 * @param out_handle Destination for the created handle. Must not be NULL.
 * @return HAL_OK on success; HAL_EINVAL for an invalid output pointer, pin or
 *         overlapping RX/TX pins; HAL_ENOMEM when the instance pool, mutex or
 *         target backend resources are exhausted.
 */
hal_status_t hal_swserial_create_ex(uint8_t rx_pin, uint8_t tx_pin,
                                    hal_swserial_t *out_handle);

/**
 * @brief Create a software serial instance on the given RX/TX pins.
 *
 * Compatibility wrapper over hal_swserial_create_ex().
 *
 * @param rx_pin Receive pin.
 * @param tx_pin Transmit pin.
 * @return Opaque handle, or NULL on failure.
 */
hal_swserial_t hal_swserial_create(uint8_t rx_pin, uint8_t tx_pin);

/**
 * @brief Reassign the RX pin before begin().
 * @param h      Handle from hal_swserial_create().
 * @param rx_pin Receive pin.
 * @return HAL_OK on success (including an idempotent assignment of the current
 *         pin); HAL_EINVAL for an invalid handle, pin or RX/TX overlap; or
 *         HAL_ESTATE when changing the pin after begin().
 */
hal_status_t hal_swserial_set_rx_ex(hal_swserial_t h, uint8_t rx_pin);

/**
 * @brief Reassign the RX pin before begin().
 *
 * Compatibility wrapper over hal_swserial_set_rx_ex().
 *
 * @param h      Handle from hal_swserial_create().
 * @param rx_pin Receive pin.
 * @return true on success; false on any status error.
 */
bool hal_swserial_set_rx(hal_swserial_t h, uint8_t rx_pin);

/**
 * @brief Reassign the TX pin before begin().
 * @param h      Handle from hal_swserial_create().
 * @param tx_pin Transmit pin.
 * @return HAL_OK on success (including an idempotent assignment of the current
 *         pin); HAL_EINVAL for an invalid handle, pin or RX/TX overlap; or
 *         HAL_ESTATE when changing the pin after begin().
 */
hal_status_t hal_swserial_set_tx_ex(hal_swserial_t h, uint8_t tx_pin);

/**
 * @brief Reassign the TX pin before begin().
 *
 * Compatibility wrapper over hal_swserial_set_tx_ex().
 *
 * @param h      Handle from hal_swserial_create().
 * @param tx_pin Transmit pin.
 * @return true on success; false on any status error.
 */
bool hal_swserial_set_tx(hal_swserial_t h, uint8_t tx_pin);

/**
 * @brief Start the serial port.
 *
 * Repeating this call reconfigures the port and clears its receive buffer and
 * error/overflow state.
 *
 * @param h      Handle from hal_swserial_create().
 * @param baud   Baud rate.
 * @param config UART config word (e.g. HAL_UART_CFG_8N1).
 * @return HAL_OK on success, or HAL_EINVAL for an invalid handle, zero or
 *         unrepresentable baud rate, or unsupported frame configuration.
 */
hal_status_t hal_swserial_begin(hal_swserial_t h, uint32_t baud,
                                uint16_t config);

/**
 * @brief Return the number of bytes available in the receive buffer.
 * @param h Handle.
 * @return Number of bytes ready to read, or 0 for an invalid or unstarted
 *         handle.
 */
int hal_swserial_available(hal_swserial_t h);

/**
 * @brief Read one byte from the receive buffer and return a typed status.
 * @param h         Handle.
 * @param out_value Destination byte. Set to 0 before other validation when
 *                  non-NULL.
 * @return HAL_OK when a byte was read; HAL_EINVAL for an invalid handle or
 *         output pointer; HAL_EUNINIT before begin(); or HAL_EAGAIN when the
 *         receive buffer is empty.
 */
hal_status_t hal_swserial_read_ex(hal_swserial_t h, uint8_t *out_value);

/**
 * @brief Read one byte from the receive buffer.
 *
 * Compatibility wrapper over hal_swserial_read_ex().
 *
 * @param h Handle.
 * @return Byte value (0-255), or -1 on any status error (including an empty
 *         receive buffer).
 */
int hal_swserial_read(hal_swserial_t h);

/**
 * @brief Write raw bytes to the serial port and return a typed status.
 *
 * On a started handle, a zero-length write succeeds and permits @p data to be
 * NULL. When provided, @p out_written is set to 0 before validation and
 * receives the number of bytes accepted before the function returns.
 *
 * @param h           Handle.
 * @param data        Byte buffer, required when @p len is non-zero.
 * @param len         Number of bytes to send.
 * @param out_written Optional destination for the number of bytes accepted.
 * @return HAL_OK when all requested bytes were accepted; HAL_EINVAL for an
 *         invalid handle or buffer; or HAL_EUNINIT before begin().
 */
hal_status_t hal_swserial_write_ex(hal_swserial_t h, const uint8_t *data,
                                   size_t len, size_t *out_written);

/**
 * @brief Write raw bytes to the serial port.
 *
 * Compatibility wrapper over hal_swserial_write_ex().
 *
 * @param h    Handle.
 * @param data Byte buffer.
 * @param len  Number of bytes to send.
 * @return Number of bytes accepted for transmission, or 0 on failure.
 */
size_t hal_swserial_write(hal_swserial_t h, const uint8_t *data, size_t len);

/**
 * @brief Print a string followed by a CRLF line ending and return a status.
 *
 * A NULL string is treated as an empty string. When provided, @p out_written
 * is set to 0 before validation and receives only the number of payload bytes
 * written; the two CRLF bytes are deliberately excluded for compatibility
 * with hal_swserial_println().
 *
 * @param h           Handle.
 * @param s           Null-terminated string, or NULL for an empty string.
 * @param out_written Optional destination for the payload-byte count.
 * @return HAL_OK when the payload and line ending were written; HAL_EINVAL for
 *         an invalid handle; or HAL_EUNINIT before begin().
 */
hal_status_t hal_swserial_println_ex(hal_swserial_t h, const char *s,
                                     size_t *out_written);

/**
 * @brief Print a string followed by a CRLF line ending.
 *
 * Compatibility wrapper over hal_swserial_println_ex().
 *
 * @param h Handle.
 * @param s Null-terminated string, or NULL for an empty string.
 * @return Number of payload bytes written, excluding the line ending, or 0 on
 *         failure.
 */
size_t hal_swserial_println(hal_swserial_t h, const char *s);

/**
 * @brief Flush the transmit buffer, blocking until all bytes are sent.
 * @param h Handle.
 * @return HAL_OK when transmission is complete; HAL_EINVAL for an invalid
 *         handle; or HAL_EUNINIT before begin().
 */
hal_status_t hal_swserial_flush(hal_swserial_t h);

/**
 * @brief Release resources. The handle must not be used after this call.
 * @param h Handle to release. NULL is a safe no-op.
 */
void hal_swserial_destroy(hal_swserial_t h);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_SWSERIAL */
