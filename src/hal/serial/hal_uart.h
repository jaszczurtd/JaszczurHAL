#pragma once

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_UART

/**
 * @file hal_uart.h
 * @brief Hardware abstraction for target hardware UART ports.
 *
 * The API is intentionally close to hal_swserial so both backends can be
 * swapped in application code with minimal changes.
 */

#include "hal/core/hal_status.h"
#include "hal/serial/hal_uart_config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Portable hardware UART port identifiers. */
typedef enum {
  HAL_UART_PORT_1 = 1,
  HAL_UART_PORT_2 = 2,
} hal_uart_port_t;

typedef struct {
  /** Receiver overruns. On STM32G474 also each receive DMA interrupt held off
   *  long enough that a whole ring may have gone by: bytes may be missing
   *  that rx_buffer_overflow cannot count. */
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
 *
 * On STM32G474 PORT_1 is USART1 (RX PA10, PB7, PC5, PE1; TX PA9, PB6, PC4,
 * PE0, PG9) and PORT_2 is USART2 (RX PA3, PA15, PB4, PD6; TX PA2, PA14, PB3,
 * PD5). USART2 also carries the debug console: while a PORT_2 handle is
 * started, printf, HAL logs, assertion text and the fault message are not
 * printed. Pin 255 leaves a direction unconnected on RP and STM32.
 *
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
 * error counters. Bytes already queued for transmission are sent at the old
 * settings first; on STM32G474, when they cannot leave within twice the time
 * the queue needs to drain, the call returns HAL_ETIMEOUT and the port runs
 * on unchanged.
 *
 * @note STM32G474 sends words of 7, 8 or 9 bits including the parity bit, so
 *       5-bit frames and 6-bit frames without parity return HAL_EUNSUPPORTED,
 *       as do rates above SYSCLK/16 or below SYSCLK/16776960 (the divider
 *       is rounded to the nearest step). Reception there runs on DMA into a
 *       ring of HAL_UART_RX_BUFFER_SIZE bytes and transmission on DMA from a
 *       queue of HAL_UART_TX_BUFFER_SIZE bytes.
 *
 * @note On RP2040 and ESP32-S3, the calling core owns the active UART
 *       lifecycle. Reconfiguration and destruction must be performed from
 *       that same core. ESP32-S3 reports HAL_ESTATE for a wrong-core
 *       reconfiguration; its compatibility destroy operation asserts and
 *       retains the handle.
 *       In FreeRTOS/SMP builds call lifecycle operations from a task pinned to
 *       the intended core.
 *
 * @param config Frame format, e.g. HAL_UART_CFG_8N1.
 * @return HAL_OK on success; HAL_EINVAL for an invalid handle or zero baud;
 *         HAL_EUNSUPPORTED for a frame format or rate the backend cannot
 *         produce; HAL_ETIMEOUT when queued bytes could not leave first
 *         (STM32G474, nothing changed); HAL_ESTATE for a wrong-core ESP32-S3
 *         reconfiguration; HAL_EBUSY when the backend could not enable
 *         reception; or a backend error.
 */
hal_status_t hal_uart_begin(hal_uart_t h, uint32_t baud, uint16_t config);

/**
 * @brief Return the number of bytes available in the receive buffer.
 *
 * When more bytes arrived than the buffer holds, the oldest are dropped and
 * counted in rx_buffer_overflow.
 */
int hal_uart_available(hal_uart_t h);

/**
 * @brief Read up to @p size received bytes without waiting.
 *
 * On STM32G474 the call does not wait for a writer. A read preempted long
 * enough for the receive DMA to come round onto the bytes it was copying
 * drops those bytes, counts them in rx_buffer_overflow and returns the rest.
 *
 * @param out Destination; may be NULL only when @p size is 0.
 * @param size Capacity of @p out.
 * @param out_read Optional; receives the number of bytes copied.
 * @return HAL_OK when at least one byte was copied (or @p size is 0),
 *         HAL_EAGAIN when nothing was waiting, HAL_EINVAL for an invalid
 *         handle or pointer, HAL_EUNINIT before hal_uart_begin().
 */
hal_status_t hal_uart_read_bytes_ex(hal_uart_t h, uint8_t *out, size_t size,
                                    size_t *out_read);

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
 * Waits until every byte is accepted. On STM32G474 the bytes go to the
 * transmit queue and leave by DMA, so the call returns before they are on
 * the wire; it waits only for room in the queue and gives up with
 * HAL_ETIMEOUT when the queue does not move for twice the time it needs to
 * drain.
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

/**
 * @brief Queue a whole message without waiting, or nothing at all.
 *
 * Meant for streams that drop a frame rather than stall when the host reads
 * slowly. Supported on STM32G474 and the mock; RP and ESP32-S3 return
 * HAL_EUNSUPPORTED (their writes go straight to the hardware FIFO).
 *
 * @param data Bytes to send; may be NULL only when @p len is 0.
 * @param len Message length.
 * @return HAL_OK when the message was queued; HAL_EAGAIN when the queue has
 *         less room than @p len or another writer holds it (a blocking
 *         write waiting for room, a flush), nothing queued; HAL_EOVERFLOW
 *         when @p len exceeds the queue size; HAL_EINVAL for an invalid
 *         handle or pointer; HAL_EUNINIT before hal_uart_begin();
 *         HAL_EUNSUPPORTED on backends without a transmit queue.
 */
hal_status_t hal_uart_try_write_ex(hal_uart_t h, const uint8_t *data,
                                   size_t len);

/**
 * @brief Report the free room in the transmit queue.
 *
 * Does not wait for a writer. The value comes from one moment; a writer or
 * the transmit DMA may change it right after.
 *
 * @param out_free Receives the number of bytes hal_uart_try_write_ex() would
 *                 accept now; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for an invalid handle or pointer, HAL_EUNINIT
 *         before hal_uart_begin(), HAL_EUNSUPPORTED on backends without a
 *         transmit queue (RP, ESP32-S3).
 */
hal_status_t hal_uart_tx_free_ex(hal_uart_t h, size_t *out_free);

/** @brief Status-returning variant of hal_uart_println(). */
hal_status_t hal_uart_println_ex(hal_uart_t h, const char *s,
                                 size_t *out_written);

/** @brief Print a string followed by a line ending. */
size_t hal_uart_println(hal_uart_t h, const char *s);

/**
 * @brief Flush the transmit buffer, blocking until all bytes are sent.
 * @return HAL_OK when the last stop bit has left; HAL_EINVAL for an invalid
 *         handle; HAL_EUNINIT before begin() on backends that track it;
 *         HAL_ETIMEOUT on STM32G474 when the queue does not drain within
 *         twice the time it needs.
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
 *
 * On STM32G474 queued bytes get the drain time to leave; what is still
 * queued after it is dropped.
 *
 * @note On RP2040 and ESP32-S3 call from the same core that successfully
 *       called hal_uart_begin(). A wrong-core ESP32-S3 call asserts and keeps
 *       the handle active.
 */
void hal_uart_destroy(hal_uart_t h);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_UART */
