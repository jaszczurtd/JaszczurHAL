#pragma once

#include "hal/core/hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_I2C_SLAVE

/**
 * @file hal_i2c_slave.h
 * @brief I2C peripheral (slave/target) mode with register-map interface.
 *
 * The device exposes an internal register map over I2C. A remote master
 * writes a one-byte register address, then reads N bytes starting from
 * that register. The register pointer advances for every byte clocked by the
 * master and survives STOP conditions, so a later bare read continues from
 * the last position. Reads must stay within the configured register map.
 *
 * The ESP32 backend preserves this wire-level contract with an ESP-IDF TX
 * FIFO/ring buffer. Its software producer advances by bytes accepted into
 * that queue; bytes not yet clocked remain queued across STOP. Writing a new
 * register address discards the old queued suffix and creates a fresh
 * register-map snapshot. Consequently, local reg_write*() updates to bytes
 * already queued become visible after the master selects a register again.
 *
 * Thread-safety:
 *   - hal_i2c_slave_init*() and hal_i2c_slave_deinit*() must be serialized by
 *     the application.
 *   - hal_i2c_slave_reg_write*() and hal_i2c_slave_reg_read*() use an
 *     internal short register-map lock shared with backend bus handlers, so
 *     they are safe to call from normal task/core context.
 *   - backend bus handlers may run from hardware IRQ/callback context
 *     (for example RP2040 I2C IRQs or STM32 I2C EV/ER IRQs) and do not take
 *     HAL mutexes.
 *
 * Two I2C controllers are supported via bus-index APIs:
 *   - bus 0 -> default I2C controller
 *   - bus 1 -> second I2C controller, when available
 * Any other bus value is invalid and triggers HAL_ASSERT in checked builds.
 *
 * Register map size is fixed at compile time (HAL_I2C_SLAVE_REG_MAP_SIZE,
 * default 32 bytes). Override in hal_project_config.h if needed.
 */

#ifndef HAL_I2C_SLAVE_REG_MAP_SIZE
#define HAL_I2C_SLAVE_REG_MAP_SIZE 32U
#endif

/**
 * @brief Initialise I2C peripheral mode on the default bus (bus 0).
 * @param sda_pin  SDA GPIO pin number.
 * @param scl_pin  SCL GPIO pin number.
 * @param address  7-bit I2C slave address.
 */
void hal_i2c_slave_init(uint8_t sda_pin, uint8_t scl_pin, uint8_t address);

/**
 * @brief Initialise I2C peripheral mode on a specific bus.
 * @param bus      Bus index: 0 = default controller, 1 = second controller.
 * @param sda_pin  SDA GPIO pin number.
 * @param scl_pin  SCL GPIO pin number.
 * @param address  7-bit I2C slave address.
 */
void hal_i2c_slave_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin,
                            uint8_t address);

/**
 * @brief Shut down I2C slave on the default bus.
 */
void hal_i2c_slave_deinit(void);

/**
 * @brief Shut down I2C slave on a specific bus.
 */
void hal_i2c_slave_deinit_bus(uint8_t bus);

/**
 * @brief Write a single byte into the register map.
 * @param reg   Register offset (0 .. HAL_I2C_SLAVE_REG_MAP_SIZE-1).
 * @param value Byte to store.
 *
 * Writes to out-of-range registers are silently ignored.
 */
void hal_i2c_slave_reg_write8(uint8_t reg, uint8_t value);
void hal_i2c_slave_reg_write8_bus(uint8_t bus, uint8_t reg, uint8_t value);

/**
 * @brief Write a 16-bit value into the register map (big-endian).
 * @param reg   Register offset of the MSB (LSB goes to reg+1).
 * @param value 16-bit value to store.
 *
 * Both bytes must fit; writes are ignored if reg+1 >= map size.
 */
void hal_i2c_slave_reg_write16(uint8_t reg, uint16_t value);
void hal_i2c_slave_reg_write16_bus(uint8_t bus, uint8_t reg, uint16_t value);

/**
 * @brief Read a single byte from the register map.
 * @param reg  Register offset.
 * @return     The stored byte, or 0 if reg is out of range.
 */
uint8_t hal_i2c_slave_reg_read8(uint8_t reg);
uint8_t hal_i2c_slave_reg_read8_bus(uint8_t bus, uint8_t reg);

/**
 * @brief Read a 16-bit value from the register map (big-endian).
 * @param reg  Register offset of the MSB.
 * @return     The stored 16-bit value, or 0 if out of range.
 */
uint16_t hal_i2c_slave_reg_read16(uint8_t reg);
uint16_t hal_i2c_slave_reg_read16_bus(uint8_t bus, uint8_t reg);

/**
 * @brief Return the 7-bit slave address configured on the default bus.
 */
uint8_t hal_i2c_slave_get_address(void);
uint8_t hal_i2c_slave_get_address_bus(uint8_t bus);

/**
 * @brief Return the backend-observed I2C activity count since initialisation.
 *
 * RP, STM32 and mock backends count completed master reads and writes. The
 * ESP32 backend counts completed writes and read-service requests accepted by
 * the ESP-IDF callback path because that driver does not expose read-complete
 * byte counts. Depending on the backend, the handler may be a hardware
 * ISR/callback or a mock event path.
 * The value wraps at UINT32_MAX.
 *
 * Thread-safe: uses atomic access; callable from any core or context.
 */
uint32_t hal_i2c_slave_get_transaction_count(void);
uint32_t hal_i2c_slave_get_transaction_count_bus(uint8_t bus);

#endif /* HAL_ENABLE_I2C_SLAVE */
#ifdef __cplusplus
}
#endif
