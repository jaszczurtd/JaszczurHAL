#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_I2C

/**
 * @file hal_i2c.h
 * @brief Hardware abstraction for I2C bus.
 *
 * Wraps platform-specific pin assignment, bus startup and transfer
 * primitives so that project code is decoupled from backend-specific I2C
 * peripherals and can be tested on a PC using a mock implementation.
 *
 * Two I2C controllers are supported via bus-index APIs:
 *   - bus 0 -> default hardware controller (RP2040 I2C0, STM32G474 I2C1)
 *   - bus 1 -> second hardware controller (RP2040 I2C1, STM32G474 I2C2)
 * Any other bus value is invalid and triggers HAL_ASSERT in checked builds.
 *
 * The legacy no-bus APIs are preserved and operate on bus 0.
 *
 * Thread-safety: the HAL owns an internal per-bus mutex.
 *   - hal_i2c_begin_transmission() acquires the bus guard.
 *   - hal_i2c_end_transmission()   releases the begin-transmission guard.
 *   - hal_i2c_write_read(), hal_i2c_read_bytes() and
 *     hal_i2c_read_byte() hold the bus guard across request+buffer drain and
 *     are the preferred read APIs for thread-safe drivers.
 *   - hal_i2c_request_from(), hal_i2c_available() and hal_i2c_read() are
 *     legacy buffered receive APIs. They are not an atomic read sequence
 *     unless the caller wraps the full request/available/read sequence in
 *     hal_i2c_lock() / hal_i2c_unlock().
 *   - For multi-step device-driver sequences that must be atomic, use
 *     hal_i2c_lock() and hal_i2c_unlock() to guard the whole sequence
 *     explicitly. HAL I2C calls made by the same execution context inside
 *     that manual lock reuse it instead of taking the platform mutex again;
 *     nested end/request helpers do not release the caller's outer lock.
 *
 * Mutex lifecycle: hal_i2c_init()/hal_i2c_init_bus() creates the per-bus
 * mutex early in normal use. Runtime calls keep an atomic create-once fallback
 * for defensive use before init, so two FreeRTOS tasks/RP2040 cores cannot
 * accidentally create different locks for the same bus.
 *
 * Init order: hal_i2c_init()/hal_i2c_init_bus() is still required to
 * configure pins, clock and start the selected hardware controller before real
 * bus traffic.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief I2C standard-mode clock: 100 kHz. */
#define HAL_I2C_CLOCK_STANDARD_HZ 100000UL

/** @brief I2C fast-mode clock: 400 kHz. */
#define HAL_I2C_CLOCK_FAST_HZ 400000UL

/** @brief I2C fast-mode plus clock: 1 MHz. */
#define HAL_I2C_CLOCK_FAST_PLUS_HZ 1000000UL

/** @brief I2C high-speed mode clock: 3.4 MHz. */
#define HAL_I2C_CLOCK_HIGH_SPEED_HZ 3400000UL

/** @brief I2C transaction completed successfully. */
#define HAL_I2C_RESULT_OK 0u

/** @brief I2C transaction failed with a generic bus/device error. */
#define HAL_I2C_ERROR_GENERIC 2u

/** @brief I2C transaction failed with a non-specific backend error. */
#define HAL_I2C_ERROR_OTHER 3u

/** @brief I2C transaction timed out. */
#define HAL_I2C_ERROR_TIMEOUT 4u

/**
 * @brief Configure I2C pins, start the bus in controller (master) mode,
 *        and initialise the internal thread-safety mutex.
 * @param sda_pin  SDA pin number.
 * @param scl_pin  SCL pin number.
 * @param clock_hz Bus clock frequency in Hz, e.g.
 *                 HAL_I2C_CLOCK_STANDARD_HZ, HAL_I2C_CLOCK_FAST_HZ,
 *                 HAL_I2C_CLOCK_FAST_PLUS_HZ, or
 *                 HAL_I2C_CLOCK_HIGH_SPEED_HZ.
 */
void hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz);

/**
 * @brief Configure pins and start the selected I2C controller.
 * @param bus      I2C controller index (0 = default, 1 = second controller).
 * @param sda_pin  SDA pin number.
 * @param scl_pin  SCL pin number.
 * @param clock_hz Bus clock frequency in Hz.
 */
void hal_i2c_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin,
                      uint32_t clock_hz);

/**
 * @brief Change the clock of the default I2C controller after init.
 * @param clock_hz Bus clock frequency in Hz.
 */
void hal_i2c_set_clock(uint32_t clock_hz);

/**
 * @brief Change the clock of the selected I2C controller after init.
 * @param bus      I2C controller index (0 = default, 1 = second controller).
 * @param clock_hz Bus clock frequency in Hz.
 */
void hal_i2c_set_clock_bus(uint8_t bus, uint32_t clock_hz);

/**
 * @brief Stop the I2C bus.
 */
void hal_i2c_deinit(void);

/**
 * @brief Stop the selected I2C controller.
 * @param bus I2C controller index (0 = default, 1 = second controller).
 */
void hal_i2c_deinit_bus(uint8_t bus);

/**
 * @brief Acquire the I2C bus mutex.
 *
 * Use this together with hal_i2c_unlock() when a driver needs to guard a
 * larger multi-step I2C sequence. Calls such as
 * hal_i2c_begin_transmission(), hal_i2c_request_from() and
 * hal_i2c_write_read() may be used inside the guarded section; they will not
 * recursively take the underlying platform mutex or release this outer lock.
 *
 */
void hal_i2c_lock(void);

/**
 * @brief Acquire the mutex for the selected I2C controller.
 * @param bus I2C controller index (0 = default, 1 = second controller).
 */
void hal_i2c_lock_bus(uint8_t bus);

/**
 * @brief Release the I2C bus mutex.
 */
void hal_i2c_unlock(void);

/**
 * @brief Release the mutex for the selected I2C controller.
 * @param bus I2C controller index (0 = default, 1 = second controller).
 */
void hal_i2c_unlock_bus(uint8_t bus);

/**
 * @brief Acquire the bus guard and begin a transmission to the given address.
 *
 * If the current execution context already owns the bus via hal_i2c_lock(),
 * this reuses that guarded section. Pair with hal_i2c_end_transmission(),
 * which releases only this begin/end nesting level.
 *
 * @param address 7-bit I2C device address.
 */
void hal_i2c_begin_transmission(uint8_t address);

/**
 * @brief Acquire the selected bus guard and begin transmission to address.
 * @param bus     I2C controller index (0 = default, 1 = second controller).
 * @param address 7-bit I2C device address.
 */
void hal_i2c_begin_transmission_bus(uint8_t bus, uint8_t address);

/**
 * @brief Write one byte into the current transmission buffer.
 * @param data Byte to send.
 * @return Number of bytes queued (1 on success, 0 on failure).
 */
size_t hal_i2c_write(uint8_t data);

/**
 * @brief Write one byte to the selected bus transmission buffer.
 * @param bus  I2C controller index (0 = default, 1 = second controller).
 * @param data Byte to send.
 * @return Number of bytes queued (1 on success, 0 on failure).
 */
size_t hal_i2c_write_bus(uint8_t bus, uint8_t data);

/**
 * @brief Flush the transmission buffer to the bus and release the begin guard.
 *
 * When called inside an outer hal_i2c_lock() section, the outer lock remains
 * held after this function returns.
 *
 * @return 0 on success, non-zero error code on failure.
 */
uint8_t hal_i2c_end_transmission(void);

/**
 * @brief One-shot "beginTransmission + write one byte + endTransmission"
 * helper.
 *
 * Wraps the three-step sequence most commonly used to push a single register
 * pointer or configuration byte to an I2C slave. The internal I2C mutex is
 * acquired by begin and released by end, so the helper is safe to call from
 * cooperating threads without any extra locking. If called inside an explicit
 * hal_i2c_lock() section, it reuses that outer lock.
 *
 * @param address    7-bit I2C slave address.
 * @param data       Byte to transmit.
 * @param outWriteOk Optional pointer. Receives true when hal_i2c_write()
 *                   reported the byte was queued, false otherwise. May be NULL.
 * @return hal_i2c_end_transmission() status (0 on success, non-zero on error).
 */
uint8_t hal_i2c_write_byte(uint8_t address, uint8_t data, bool *outWriteOk);

/**
 * @brief Bus-selecting variant of hal_i2c_write_byte().
 * @param bus        I2C controller index (0 = default, 1 = second controller).
 * @param address    7-bit I2C slave address.
 * @param data       Byte to transmit.
 * @param outWriteOk Optional pointer; see hal_i2c_write_byte(). May be NULL.
 * @return hal_i2c_end_transmission_bus() status.
 */
uint8_t hal_i2c_write_byte_bus(uint8_t bus, uint8_t address, uint8_t data,
                               bool *outWriteOk);

/**
 * @brief One-shot "request 1 byte + read" helper, symmetric to
 * hal_i2c_write_byte().
 *
 * Requests a single byte from @p address and returns it. The internal I2C
 * mutex is held across the full request+read sequence, making this helper
 * atomic with respect to other HAL I2C operations on the same bus.
 *
 * @param address   7-bit I2C slave address.
 * @param outReadOk Optional pointer. Receives true when the atomic request
 *                  and byte copy completed successfully. Receives false on
 *                  any failure. May be NULL.
 * @return The byte read, or 0 when the transaction failed (inspect @p outReadOk
 *         to distinguish a valid 0x00 from a failure).
 */
uint8_t hal_i2c_read_byte(uint8_t address, bool *outReadOk);

/**
 * @brief Bus-selecting variant of hal_i2c_read_byte().
 * @param bus       I2C controller index (0 = default, 1 = second controller).
 * @param address   7-bit I2C slave address.
 * @param outReadOk Optional pointer; see hal_i2c_read_byte(). May be NULL.
 * @return The byte read, or 0 when the transaction failed.
 */
uint8_t hal_i2c_read_byte_bus(uint8_t bus, uint8_t address, bool *outReadOk);

/**
 * @brief Combined write-then-read transaction on the default I2C controller.
 *
 * Writes @p tx_len bytes, keeps the transaction active for a repeated-start
 * read, then reads @p rx_len bytes into @p rx. This matches the common
 * register-pointer pattern used by I2C sensors.
 *
 * @param address 7-bit I2C slave address.
 * @param tx      Bytes to write before the repeated-start read.
 * @param tx_len  Number of bytes to write.
 * @param rx      Destination buffer for read bytes.
 * @param rx_len  Number of bytes to read.
 * @return true when both phases complete and exactly @p rx_len bytes are read.
 */
bool hal_i2c_write_read(uint8_t address, const uint8_t *tx, size_t tx_len,
                        uint8_t *rx, size_t rx_len);

/**
 * @brief Bus-selecting variant of hal_i2c_write_read().
 */
bool hal_i2c_write_read_bus(uint8_t bus, uint8_t address, const uint8_t *tx,
                            size_t tx_len, uint8_t *rx, size_t rx_len);

/**
 * @brief Read bytes from an I2C device without a preceding register write.
 *
 * Requests @p rx_len bytes from @p address and copies them into @p rx while
 * holding the internal I2C bus mutex. This matches sensors whose current data
 * register is read directly after address+read, without a register pointer
 * phase.
 *
 * @param address 7-bit I2C slave address.
 * @param rx      Destination buffer.
 * @param rx_len  Number of bytes to read.
 * @return true when exactly @p rx_len bytes are read.
 */
bool hal_i2c_read_bytes(uint8_t address, uint8_t *rx, size_t rx_len);

/**
 * @brief Bus-selecting variant of hal_i2c_read_bytes().
 */
bool hal_i2c_read_bytes_bus(uint8_t bus, uint8_t address, uint8_t *rx,
                            size_t rx_len);

/**
 * @brief Flush selected bus transmission and release its begin guard.
 * @param bus I2C controller index (0 = default, 1 = second controller).
 * @return 0 on success, non-zero error code on failure.
 */
uint8_t hal_i2c_end_transmission_bus(uint8_t bus);

/**
 * @brief Legacy request into the backend receive buffer.
 *
 * This guards only the physical request transaction. The bytes remain in the
 * backend receive buffer and later hal_i2c_available()/hal_i2c_read() calls are
 * not atomic with the request unless the caller wraps the full sequence in
 * hal_i2c_lock()/hal_i2c_unlock(). Prefer hal_i2c_read_bytes() or
 * hal_i2c_write_read() in new thread-safe code.
 *
 * @param address 7-bit I2C device address.
 * @param count   Number of bytes to request.
 * @return Number of bytes received.
 */
uint8_t hal_i2c_request_from(uint8_t address, uint8_t count);

/**
 * @brief Bus-selecting variant of the legacy buffered receive API.
 * @param bus     I2C controller index (0 = default, 1 = second controller).
 * @param address 7-bit I2C device address.
 * @param count   Number of bytes to request.
 * @return Number of bytes received.
 */
uint8_t hal_i2c_request_from_bus(uint8_t bus, uint8_t address, uint8_t count);

/**
 * @brief Return bytes available in the legacy receive buffer.
 * @return Byte count.
 */
int hal_i2c_available(void);

/**
 * @brief Return bytes available in the legacy receive buffer of selected bus.
 * @param bus I2C controller index (0 = default, 1 = second controller).
 * @return Byte count.
 */
int hal_i2c_available_bus(uint8_t bus);

/**
 * @brief Read one byte from the legacy receive buffer.
 * @return Byte value, or -1 if none available.
 */
int hal_i2c_read(void);

/**
 * @brief Read one byte from the selected bus legacy receive buffer.
 * @param bus I2C controller index (0 = default, 1 = second controller).
 * @return Byte value, or -1 if none available.
 */
int hal_i2c_read_bus(uint8_t bus);

/**
 * @brief Check whether an I2C device is busy by probing its address.
 *
 * Sends the device address and immediately ends the transmission without
 * data.  A NACK response (non-zero return from endTransmission) means the
 * device is busy or absent; an ACK (0) means it is ready.
 *
 * Typical use: poll after a write to an AT24C256 EEPROM until the chip
 * finishes its internal write cycle and starts ACKing again.
 *
 * @param address 7-bit I2C address to probe.
 * @return true  if the device did NOT ACK (busy / absent).
 * @return false if the device ACKed (ready).
 */
bool hal_i2c_is_busy(uint8_t address);

/**
 * @brief Probe device ACK state on the selected I2C controller.
 * @param bus     I2C controller index (0 = default, 1 = second controller).
 * @param address 7-bit I2C address to probe.
 * @return true if the device did NOT ACK (busy / absent), false otherwise.
 */
bool hal_i2c_is_busy_bus(uint8_t bus, uint8_t address);

/**
 * @brief Return the number of completed I2C transactions (writes and reads)
 *        since initialisation on the default bus (bus 0).
 *
 * Incremented by hal_i2c_end_transmission() (write) and
 * hal_i2c_request_from() (read). Resets to 0 on hal_i2c_init().
 * Wraps at UINT32_MAX. Thread-safe (atomic access).
 */
uint32_t hal_i2c_get_transaction_count(void);

/**
 * @brief Return the transaction count for a specific I2C bus.
 * @param bus I2C controller index (0 = default, 1 = second controller).
 */
uint32_t hal_i2c_get_transaction_count_bus(uint8_t bus);

/**
 * @brief Perform an I2C bus clear procedure (per I2C specification).
 *
 * Toggles SCL up to 9 times at GPIO level to release a slave that is
 * holding SDA low (e.g. after a master reset mid-transaction), then
 * generates a STOP condition.  Leaves SDA/SCL as inputs with pull-ups.
 *
 * Must be called @b before hal_i2c_init() - the bus is not usable for
 * I2C transactions during this procedure.
 *
 * @param sda_pin  SDA pin number.
 * @param scl_pin  SCL pin number.
 */
void hal_i2c_bus_clear(uint8_t sda_pin, uint8_t scl_pin);

/**
 * @brief Perform a bus clear on the specified I2C controller pins.
 * @param bus     I2C controller index (0 = default, 1 = second controller).
 * @param sda_pin SDA pin number.
 * @param scl_pin SCL pin number.
 */
void hal_i2c_bus_clear_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin);

#endif /* HAL_ENABLE_I2C */
#ifdef __cplusplus
}
#endif
