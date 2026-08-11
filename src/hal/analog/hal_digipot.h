#pragma once

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_DIGIPOT

/**
 * @file hal_digipot.h
 * @brief Hardware abstraction for I2C digital potentiometers.
 *
 * Supported chips:
 *   - MCP401x (Microchip, I2C @ 0x2F): MCP4017 / MCP4018 / MCP4019, 7-bit
 *     wiper (128 taps), non-volatile-less single wiper register. End-to-end
 *     resistance variants: 5 k / 10 k / 50 k / 100 kOhm.
 *   - MAX5395 (Maxim, I2C @ 0x28 / 0x29 / 0x2B): 8-bit wiper (256 taps),
 *     command set with shutdown modes and an on-chip charge pump. End-to-end
 *     resistance variants: 10 k / 50 k / 100 kOhm.
 *
 * Backend selection (compile-time, opt-in):
 *   HAL_ENABLE_MCP401X  - enable the MCP401x backend (propagates
 *                          HAL_ENABLE_DIGIPOT + HAL_ENABLE_I2C).
 *   HAL_ENABLE_MAX5395  - enable the MAX5395 backend (propagates
 *                          HAL_ENABLE_DIGIPOT + HAL_ENABLE_I2C).
 *   Both flags may be enabled simultaneously; chip selection is then made
 *   per-handle via @ref hal_digipot_config_t::chip. Enabling
 *   HAL_ENABLE_DIGIPOT alone (without a backend) is a compile-time #error.
 *
 * Portability: this module is backend-agnostic. The public facade delegates to
 * shared chip drivers under hal/analog/digipot/, and the chip logic
 * runs unchanged on every target that provides hal_i2c - proven on RP2040 and
 * STM32G474.
 *
 * Thread-safety: each instance carries its own mutex and every transaction
 * goes through the hal_i2c bus lock, so concurrent callers are serialised.
 *
 * Multiple simultaneous instances are supported up to
 * HAL_DIGIPOT_MAX_INSTANCES (default 4, override via -D flag).
 */

#include <stdbool.h>
#include <stdint.h>

#include "hal/core/hal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Pool size ───────────────────────────────────────────────────────────── */

/** @brief Maximum number of simultaneous digital-potentiometer instances. */
#ifndef HAL_DIGIPOT_MAX_INSTANCES
#define HAL_DIGIPOT_MAX_INSTANCES 4
#endif

/* ── Chip selector ───────────────────────────────────────────────────────── */

/** @brief Supported digital-potentiometer chips. */
typedef enum {
  HAL_DIGIPOT_CHIP_MCP401X, /**< Microchip MCP4017/4018/4019 via I2C. */
  HAL_DIGIPOT_CHIP_MAX5395, /**< Maxim MAX5395 via I2C.               */
} hal_digipot_chip_t;

/** @brief MCP401x device variant (decides which modes are legal). */
typedef enum {
  HAL_DIGIPOT_MCP4017, /**< Variable resistor (W-L) only.            */
  HAL_DIGIPOT_MCP4018, /**< Voltage divider and both variable modes. */
  HAL_DIGIPOT_MCP4019, /**< Variable resistor (W-L) only.            */
} hal_digipot_mcp401x_device_t;

/* ── Operation mode ──────────────────────────────────────────────────────── */

/**
 * @brief Potentiometer operation mode.
 *
 * In voltage-divider mode the resistance set is between the wiper (W) and the
 * low terminal (L/B). In variable-resistor mode the resistance is set between
 * the wiper (W) and either the low (W-L) or high (W-H) terminal.
 */
typedef enum {
  HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER,      /**< W..L tap of a divider.       */
  HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL, /**< Rheostat between W and L.     */
  HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WH, /**< Rheostat between W and H.     */
} hal_digipot_mode_t;

/* ── Configuration descriptor ────────────────────────────────────────────── */

/**
 * @brief Initialisation descriptor passed to hal_digipot_init_ex().
 *
 * Common fields apply to every chip; chip-specific fields are only consulted
 * for their respective @ref chip value.
 */
typedef struct {
  hal_digipot_chip_t chip; /**< Which chip to drive.             */
  uint8_t i2c_bus;         /**< I2C controller index (0 = default). */
  uint8_t i2c_addr;        /**< 7-bit address. MCP401x: 0x2F;
                                MAX5395: 0x28 / 0x29 / 0x2B.     */
  uint32_t e2e_resistance; /**< End-to-end resistance in Ohms.
                                MCP401x: 5k/10k/50k/100k;
                                MAX5395: 10k/50k/100k.           */
  hal_digipot_mode_t mode; /**< Operation mode.                  */

  /* MCP401x-only: */
  hal_digipot_mcp401x_device_t mcp401x_device; /**< MCP4017/4018/4019.     */

  /* MAX5395-only: */
  bool charge_pump_en; /**< Keep the on-chip charge pump on
                            (lower wiper resistance).        */
} hal_digipot_config_t;

/* ── Opaque handle ───────────────────────────────────────────────────────── */

/** @brief Opaque digital-potentiometer instance handle. NULL = invalid. */
typedef struct hal_digipot_impl_s hal_digipot_impl_t;
typedef hal_digipot_impl_t *hal_digipot_t;

/* ── API ─────────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise a digital potentiometer and return a status code.
 *
 * Validates the configuration against the selected chip (legal end-to-end
 * resistance, address and device/mode combination) and performs any required
 * power-on sequence (MAX5395: reset, optional charge-pump disable, shutdown
 * condition for the unused terminal). The caller must have brought the I2C
 * bus up beforehand with hal_i2c_init() / hal_i2c_init_bus().
 *
 * @param cfg  Pointer to a filled-in configuration struct.
 * @param out  Pointer receiving the created handle on success. Set to NULL on
 *             failure when provided.
 * @return HAL_OK on success, HAL_EINVAL for invalid configuration,
 *         HAL_ENOMEM when the static pool is exhausted, or HAL_EBUS/HAL_EIO
 *         for transport/device failures.
 */
hal_status_t hal_digipot_init_ex(const hal_digipot_config_t *cfg,
                                 hal_digipot_t *out);

/**
 * @brief Initialise a digital potentiometer and return an opaque handle.
 *
 * Compatibility wrapper over hal_digipot_init_ex().
 *
 * @param cfg  Pointer to a filled-in configuration struct.
 * @return Handle on success; NULL on failure.
 */
hal_digipot_t hal_digipot_init(const hal_digipot_config_t *cfg);

/**
 * @brief Release a digital-potentiometer handle back to the static pool.
 * @param h  Handle to release (NULL is a safe no-op).
 */
void hal_digipot_deinit(hal_digipot_t h);

/**
 * @brief Set the wiper resistance in Ohms according to the configured mode.
 *
 * The value is clamped at the wiper's minimum on-resistance and rejected if it
 * exceeds the end-to-end resistance. For MCP401x the wiper register is read
 * back and verified after the write.
 *
 * @param h     Valid handle.
 * @param ohms  Desired resistance in Ohms.
 * @return HAL_OK on success, HAL_EUNINIT for an invalid handle, HAL_EINVAL for
 *         invalid/unsupported resistance or mode, HAL_EBUS for I2C failures,
 *         or HAL_EIO for read-back verification failures.
 */
hal_status_t hal_digipot_set_resistance_ex(hal_digipot_t h, uint32_t ohms);

/**
 * @brief Set the wiper resistance in Ohms according to the configured mode.
 *
 * Compatibility wrapper over hal_digipot_set_resistance_ex().
 *
 * @param h     Valid handle.
 * @param ohms  Desired resistance in Ohms.
 * @return true on success; false on failure.
 */
bool hal_digipot_set_resistance(hal_digipot_t h, uint32_t ohms);

/**
 * @brief Number of resistive steps (taps - 1): 127 for MCP401x, 255 for
 * MAX5395.
 * @param h  Valid handle.
 * @return Step count, or 0 for an invalid handle.
 */
uint16_t hal_digipot_step_count(hal_digipot_t h);

/**
 * @brief Configured end-to-end resistance in Ohms.
 * @param h  Valid handle.
 * @return End-to-end resistance, or 0 for an invalid handle.
 */
uint32_t hal_digipot_e2e_resistance(hal_digipot_t h);

/**
 * @brief Configured operation mode.
 * @param h  Valid handle.
 * @return The mode, or HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER for an invalid handle.
 */
hal_digipot_mode_t hal_digipot_mode(hal_digipot_t h);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_DIGIPOT */
