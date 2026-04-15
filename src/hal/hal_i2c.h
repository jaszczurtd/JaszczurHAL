#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifndef HAL_DISABLE_I2C

/**
 * @file hal_i2c.h
 * @brief Hardware abstraction for I2C bus.
 *
 * Wraps platform-specific pin assignment, bus startup and transfer
 * primitives so that project code is decoupled from the Arduino Wire
 * object and can be tested on a PC using a mock implementation.
 *
 * Two I2C controllers are supported via bus-index APIs:
 *   - bus 0 -> Wire  (default controller)
 *   - bus 1 -> Wire1 (second controller, when available)
 *
 * The legacy no-bus APIs are preserved and operate on bus 0.
 *
 * Thread-safety: the HAL owns an internal mutex.
 *   - hal_i2c_begin_transmission() acquires the mutex.
 *   - hal_i2c_end_transmission()   releases the mutex.
 *   - hal_i2c_request_from()       acquires and releases the mutex
 *     around the transfer; the received bytes remain in the Wire
 *     buffer and can be read with hal_i2c_available() / hal_i2c_read()
 *     without holding the lock.
 *   - For multi-step sequences involving third-party I2C libraries
 *     (e.g. ADS1115) that call Wire directly, use hal_i2c_lock() and
 *     hal_i2c_unlock() to guard the whole sequence explicitly.
 */

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Configure I2C pins, start the bus in controller (master) mode,
 *        and initialise the internal thread-safety mutex.
 * @param sda_pin  SDA pin number.
 * @param scl_pin  SCL pin number.
 * @param clock_hz Bus clock frequency in Hz (e.g. 100000, 400000).
 */
void hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz);

/**
 * @brief Configure pins and start the selected I2C controller.
 * @param bus      I2C controller index (0 = Wire, 1 = Wire1).
 * @param sda_pin  SDA pin number.
 * @param scl_pin  SCL pin number.
 * @param clock_hz Bus clock frequency in Hz.
 */
void hal_i2c_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz);

/**
 * @brief Stop the I2C bus.
 */
void hal_i2c_deinit(void);

/**
 * @brief Stop the selected I2C controller.
 * @param bus I2C controller index (0 = Wire, 1 = Wire1).
 */
void hal_i2c_deinit_bus(uint8_t bus);

/**
 * @brief Acquire the I2C bus mutex.
 *
 * Use this together with hal_i2c_unlock() when wrapping a third-party
 * library that talks to Wire directly (e.g. ADS1115).
 *
 * @note Requires hal_i2c_init() first. Calling before init triggers HAL_ASSERT.
 */
void hal_i2c_lock(void);

/**
 * @brief Acquire the mutex for the selected I2C controller.
 * @param bus I2C controller index (0 = Wire, 1 = Wire1).
 */
void hal_i2c_lock_bus(uint8_t bus);

/**
 * @brief Release the I2C bus mutex.
 * @note Requires hal_i2c_init() first. Calling before init triggers HAL_ASSERT.
 */
void hal_i2c_unlock(void);

/**
 * @brief Release the mutex for the selected I2C controller.
 * @param bus I2C controller index (0 = Wire, 1 = Wire1).
 */
void hal_i2c_unlock_bus(uint8_t bus);

/**
 * @brief Acquire the mutex and begin a transmission to the given address.
 * @param address 7-bit I2C device address.
 * @note Requires hal_i2c_init() first. Calling before init triggers HAL_ASSERT.
 */
void hal_i2c_begin_transmission(uint8_t address);

/**
 * @brief Acquire the selected bus mutex and begin transmission to address.
 * @param bus     I2C controller index (0 = Wire, 1 = Wire1).
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
 * @param bus  I2C controller index (0 = Wire, 1 = Wire1).
 * @param data Byte to send.
 * @return Number of bytes queued (1 on success, 0 on failure).
 */
size_t hal_i2c_write_bus(uint8_t bus, uint8_t data);

/**
 * @brief Flush the transmission buffer to the bus and release the mutex.
 * @return 0 on success, non-zero error code on failure.
 * @note Requires hal_i2c_init() first. Calling before init triggers HAL_ASSERT.
 */
uint8_t hal_i2c_end_transmission(void);

/**
 * @brief Flush selected bus transmission and release its mutex.
 * @param bus I2C controller index (0 = Wire, 1 = Wire1).
 * @return 0 on success, non-zero error code on failure.
 */
uint8_t hal_i2c_end_transmission_bus(uint8_t bus);

/**
 * @brief Request bytes from an I2C device (acquires and releases the mutex).
 * @param address 7-bit I2C device address.
 * @param count   Number of bytes to request.
 * @return Number of bytes received.
 * @note Requires hal_i2c_init() first. Calling before init triggers HAL_ASSERT.
 */
uint8_t hal_i2c_request_from(uint8_t address, uint8_t count);

/**
 * @brief Request bytes from a device on the selected bus.
 * @param bus     I2C controller index (0 = Wire, 1 = Wire1).
 * @param address 7-bit I2C device address.
 * @param count   Number of bytes to request.
 * @return Number of bytes received.
 */
uint8_t hal_i2c_request_from_bus(uint8_t bus, uint8_t address, uint8_t count);

/**
 * @brief Return the number of bytes available in the receive buffer.
 * @return Byte count.
 */
int hal_i2c_available(void);

/**
 * @brief Return bytes available in the receive buffer of selected bus.
 * @param bus I2C controller index (0 = Wire, 1 = Wire1).
 * @return Byte count.
 */
int hal_i2c_available_bus(uint8_t bus);

/**
 * @brief Read one byte from the receive buffer.
 * @return Byte value, or -1 if none available.
 */
int hal_i2c_read(void);

/**
 * @brief Read one byte from the selected bus receive buffer.
 * @param bus I2C controller index (0 = Wire, 1 = Wire1).
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
 * @param bus     I2C controller index (0 = Wire, 1 = Wire1).
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
 * @param bus I2C controller index (0 = Wire, 1 = Wire1).
 */
uint32_t hal_i2c_get_transaction_count_bus(uint8_t bus);


#endif /* HAL_DISABLE_I2C */
#ifdef __cplusplus
}
#endif
