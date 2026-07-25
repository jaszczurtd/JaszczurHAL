#pragma once

#include "hal_config.h"
#ifdef HAL_ENABLE_THERMOCOUPLE

/**
 * @file hal_thermocouple.h
 * @brief Hardware abstraction for thermocouple amplifier / ADC ICs.
 *
 * Supported chips:
 *   - MCP9600 (I2C): K/J/T/N/S/E/B/R wire types, cold-junction compensation,
 *                    configurable ADC resolution, hardware IIR filter,
 *                    4-channel programmable alerts, sleep mode.
 *   - MAX6675 (shared SPI bit-bang over HAL GPIO): K-type only,
 *                              12-bit hot-junction read, open-circuit
 * detection.
 *
 * Backend selection (compile-time, opt-in):
 *   HAL_ENABLE_MCP9600  - enable the shared MCP9600/MCP9601 backend
 *                          (propagates HAL_ENABLE_THERMOCOUPLE +
 * HAL_ENABLE_I2C). HAL_ENABLE_MAX6675  - enable the shared MAX6675 backend
 *                          (propagates HAL_ENABLE_THERMOCOUPLE).
 *   Both flags may be enabled simultaneously; chip selection is then
 *   made per-handle via @ref hal_thermocouple_config_t::chip.
 *   Enabling HAL_ENABLE_THERMOCOUPLE alone (without a backend) triggers
 *   a compile-time #error from hal_config.h.
 *
 * Functions not available on the selected chip print an error to the serial
 * console and return a safe default (NAN / 0 / false).
 * Error format: "function_name: CHIP_NAME is not supporting this functionality"
 *
 * Multiple simultaneous instances are supported up to
 * HAL_THERMOCOUPLE_MAX_INSTANCES (default 4, override via -D flag).
 *
 * Obtain a handle with hal_thermocouple_init(); release with
 * hal_thermocouple_deinit().
 */

#include <math.h> /* NAN */
#include <stdbool.h>
#include <stdint.h>

#include "hal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Pool size ───────────────────────────────────────────────────────────── */

/** @brief Maximum number of simultaneous thermocouple instances. */
#ifndef HAL_THERMOCOUPLE_MAX_INSTANCES
#define HAL_THERMOCOUPLE_MAX_INSTANCES 4
#endif

/* ── Chip selector ───────────────────────────────────────────────────────── */

/** @brief Supported thermocouple amplifier chips. */
typedef enum {
  HAL_THERMOCOUPLE_CHIP_MCP9600, /**< MCP9600 / MCP9601 via I2C.        */
  HAL_THERMOCOUPLE_CHIP_MAX6675, /**< MAX6675 via HAL GPIO bit-bang SPI. */
} hal_thermocouple_chip_t;

/* ── Bus configuration ───────────────────────────────────────────────────── */

#ifdef HAL_ENABLE_MCP9600
/**
 * @brief I2C bus parameters for MCP9600.
 *
 * Initialise every field before calling @ref hal_thermocouple_init or
 * @ref hal_thermocouple_init_ex. In particular, set @p i2c_bus explicitly;
 * zero-initialising the complete @ref hal_thermocouple_config_t first is the
 * recommended pattern.
 */
typedef struct {
  uint8_t sda_pin;   /**< SDA GPIO pin.                                  */
  uint8_t scl_pin;   /**< SCL GPIO pin.                                  */
  uint32_t clock_hz; /**< Bus speed in Hz (MCP9600 supports up to 100 kHz). */
  uint8_t i2c_bus;   /**< I2C bus index: 0 = default, 1 = second.        */
  uint8_t i2c_addr;  /**< 7-bit I2C address (default 0x67).              */
} hal_thermocouple_i2c_cfg_t;
#endif /* HAL_ENABLE_MCP9600 */

#ifdef HAL_ENABLE_MAX6675
/** @brief SPI bit-bang parameters for MAX6675. */
typedef struct {
  uint8_t sclk_pin; /**< Clock pin.                                     */
  uint8_t cs_pin;   /**< Chip-select (active-low).                      */
  uint8_t miso_pin; /**< Data-out from sensor (MISO).                   */
} hal_thermocouple_spi_cfg_t;
#endif /* HAL_ENABLE_MAX6675 */

/**
 * @brief Complete initialisation descriptor passed to hal_thermocouple_init().
 *
 * Zero-initialise the complete descriptor, fill in @p chip, then populate the
 * matching union member:
 *   - bus.i2c  for HAL_THERMOCOUPLE_CHIP_MCP9600
 *   - bus.spi  for HAL_THERMOCOUPLE_CHIP_MAX6675
 *
 * @code
 * hal_thermocouple_config_t cfg = {0};
 * cfg.chip = HAL_THERMOCOUPLE_CHIP_MCP9600;
 * cfg.bus.i2c.i2c_bus = 0;
 * @endcode
 */
typedef struct {
  hal_thermocouple_chip_t chip; /**< Which chip to drive.               */
  union {
#ifdef HAL_ENABLE_MCP9600
    hal_thermocouple_i2c_cfg_t i2c; /**< Used for MCP9600.             */
#endif
#ifdef HAL_ENABLE_MAX6675
    hal_thermocouple_spi_cfg_t spi; /**< Used for MAX6675.             */
#endif
  } bus;
} hal_thermocouple_config_t;

/* ── Opaque handle ───────────────────────────────────────────────────────── */

/** @brief Opaque thermocouple instance handle.  NULL = invalid. */
typedef struct hal_thermocouple_impl_s hal_thermocouple_impl_t;
typedef hal_thermocouple_impl_t *hal_thermocouple_t;

/* ── Enumerations ────────────────────────────────────────────────────────── */

/**
 * @brief Thermocouple wire type.
 *
 * MCP9600 supports all types listed here and allows runtime selection via
 * hal_thermocouple_set_type().
 * MAX6675 is permanently wired for K-type; hal_thermocouple_set_type() will
 * print an unsupported-function error.
 */
typedef enum {
  HAL_THERMOCOUPLE_TYPE_K = 0,
  HAL_THERMOCOUPLE_TYPE_J,
  HAL_THERMOCOUPLE_TYPE_T,
  HAL_THERMOCOUPLE_TYPE_N,
  HAL_THERMOCOUPLE_TYPE_S,
  HAL_THERMOCOUPLE_TYPE_E,
  HAL_THERMOCOUPLE_TYPE_B,
  HAL_THERMOCOUPLE_TYPE_R,
} hal_thermocouple_type_t;

#ifdef HAL_ENABLE_MCP9600
/**
 * @brief Hot-junction ADC resolution (MCP9600 only).
 *
 * Higher resolution gives finer temperature steps at the cost of a longer
 * conversion time.
 */
typedef enum {
  HAL_THERMOCOUPLE_ADC_RES_18 = 0, /**< 18-bit, ~320 ms/conversion.     */
  HAL_THERMOCOUPLE_ADC_RES_16,     /**< 16-bit, ~80 ms/conversion.      */
  HAL_THERMOCOUPLE_ADC_RES_14,     /**< 14-bit, ~20 ms/conversion.      */
  HAL_THERMOCOUPLE_ADC_RES_12,     /**< 12-bit, ~5 ms/conversion.       */
} hal_thermocouple_adc_res_t;

/**
 * @brief Cold-junction (ambient) ADC resolution (MCP9600 only).
 */
typedef enum {
  HAL_THERMOCOUPLE_AMBIENT_RES_0_25 = 0, /**< 0.25 °C per LSB.       */
  HAL_THERMOCOUPLE_AMBIENT_RES_0_125,    /**< 0.125 °C per LSB.      */
  HAL_THERMOCOUPLE_AMBIENT_RES_0_0625,   /**< 0.0625 °C per LSB.     */
  HAL_THERMOCOUPLE_AMBIENT_RES_0_03125,  /**< 0.03125 °C per LSB.    */
} hal_thermocouple_ambient_res_t;

/**
 * @brief Alert channel configuration (MCP9600 only, channels 1-4).
 */
typedef struct {
  float temperature;        /**< Trigger temperature in °C.            */
  bool rising;              /**< true = alert on rise; false = on fall.*/
  bool alert_cold_junction; /**< true = monitor cold junction;
                                 false = hot junction (default).       */
  bool active_high;         /**< Output polarity when alert fires.     */
  bool interrupt_mode;      /**< true = latch until cleared;
                                 false = comparator (self-clearing).   */
} hal_thermocouple_alert_cfg_t;
#endif /* HAL_ENABLE_MCP9600 */

/* ── API ─────────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise a thermocouple chip and return an opaque handle.
 *
 * For MCP9600: calls hal_i2c_init() internally, then verifies the chip via
 * its device-ID register.  Two sensors on the same I2C bus are supported -
 * call hal_thermocouple_init() for each; repeated hal_i2c_init() calls with
 * identical parameters are idempotent on RP2040 and STM32G474 backends.
 *
 * For MAX6675: configures the bit-bang SPI GPIO pins through the shared,
 * Arduino-free MAX6675 driver.
 *
 * Every field used by the selected chip must be initialised. Zero-initialise
 * the complete descriptor first and set `bus.i2c.i2c_bus` explicitly for an
 * MCP9600.
 *
 * @param cfg  Pointer to the fully initialised configuration struct.
 * @return Handle on success; NULL if the pool is exhausted or the chip
 *         does not respond.
 */
hal_thermocouple_t hal_thermocouple_init(const hal_thermocouple_config_t *cfg);
hal_status_t hal_thermocouple_init_ex(const hal_thermocouple_config_t *cfg,
                                      hal_thermocouple_t *out_handle);

/**
 * @brief Release a thermocouple handle back to the static pool.
 *
 * The slot becomes available for a future hal_thermocouple_init() call.
 * Passing NULL is safe (no-op).
 *
 * @param h  Handle to release.
 */
void hal_thermocouple_deinit(hal_thermocouple_t h);

/**
 * @brief Read the hot-junction (probe tip) temperature in °C.
 *
 * Supported by both MCP9600 and MAX6675.
 * Returns NAN when the chip reports an open-circuit / thermocouple fault.
 *
 * @param h  Valid handle.
 * @return Temperature in °C, or NAN on fault / invalid handle.
 */
float hal_thermocouple_read(hal_thermocouple_t h);
hal_status_t hal_thermocouple_read_ex(hal_thermocouple_t h, float *out_c);

#ifdef HAL_ENABLE_MCP9600
/**
 * @brief Read the cold-junction (on-chip ambient) temperature in °C.
 *
 * MCP9600 only - the chip contains an internal thermistor for cold-junction
 * compensation.  MAX6675 has no on-chip CJC sensor.
 *
 * @param h  Valid handle.
 * @return Temperature in °C, or NAN if unsupported / error.
 */
float hal_thermocouple_read_ambient(hal_thermocouple_t h);
hal_status_t hal_thermocouple_read_ambient_ex(hal_thermocouple_t h,
                                              float *out_c);

/**
 * @brief Read the raw EMF ADC value in µV (signed 24-bit, sign-extended).
 *
 * MCP9600 only.
 *
 * @param h  Valid handle.
 * @return Raw µV reading, or 0 if unsupported.
 */
int32_t hal_thermocouple_read_adc_raw(hal_thermocouple_t h);
hal_status_t hal_thermocouple_read_adc_raw_ex(hal_thermocouple_t h,
                                              int32_t *out_raw);
#endif /* HAL_ENABLE_MCP9600 */

#ifdef HAL_ENABLE_MCP9600
/**
 * @brief Configure the thermocouple wire type.
 *
 * MCP9600 only.  MAX6675 is internally fixed to K-type and prints an error.
 *
 * @param h     Valid handle.
 * @param type  Desired wire type.
 */
hal_status_t hal_thermocouple_set_type(hal_thermocouple_t h,
                                       hal_thermocouple_type_t type);
#endif /* HAL_ENABLE_MCP9600 */

/**
 * @brief Return the currently configured thermocouple wire type.
 *
 * MCP9600: reads from the sensor-configuration register.
 * MAX6675: always returns HAL_THERMOCOUPLE_TYPE_K (fixed silicon - no error).
 *
 * @param h  Valid handle.
 * @return Current wire type.
 */
hal_thermocouple_type_t hal_thermocouple_get_type(hal_thermocouple_t h);
hal_status_t hal_thermocouple_get_type_ex(hal_thermocouple_t h,
                                          hal_thermocouple_type_t *out_type);

#ifdef HAL_ENABLE_MCP9600
/**
 * @brief Set the hardware IIR filter coefficient.
 *
 * MCP9600 only.  Coefficient range: 0 (filter off) ... 7 (maximum smoothing).
 *
 * @param h      Valid handle.
 * @param coeff  Filter coefficient in [0, 7].
 */
hal_status_t hal_thermocouple_set_filter(hal_thermocouple_t h, uint8_t coeff);

/**
 * @brief Get the current hardware IIR filter coefficient.
 *
 * MCP9600 only.
 *
 * @param h  Valid handle.
 * @return Coefficient in [0, 7], or 0 if unsupported.
 */
uint8_t hal_thermocouple_get_filter(hal_thermocouple_t h);
hal_status_t hal_thermocouple_get_filter_ex(hal_thermocouple_t h,
                                            uint8_t *out_coeff);

/**
 * @brief Set the hot-junction ADC conversion resolution.
 *
 * MCP9600 only.  Higher resolution -> longer conversion time.
 *
 * @param h    Valid handle.
 * @param res  Desired ADC resolution.
 */
hal_status_t
hal_thermocouple_set_adc_resolution(hal_thermocouple_t h,
                                    hal_thermocouple_adc_res_t res);

/**
 * @brief Get the current hot-junction ADC conversion resolution.
 *
 * MCP9600 only.
 *
 * @param h  Valid handle.
 * @return Current ADC resolution, or HAL_THERMOCOUPLE_ADC_RES_12 if
 *         unsupported.
 */
hal_thermocouple_adc_res_t
hal_thermocouple_get_adc_resolution(hal_thermocouple_t h);
hal_status_t
hal_thermocouple_get_adc_resolution_ex(hal_thermocouple_t h,
                                       hal_thermocouple_adc_res_t *out_res);

/**
 * @brief Set the cold-junction (ambient) ADC resolution.
 *
 * MCP9600 only.
 *
 * @param h    Valid handle.
 * @param res  Desired ambient resolution.
 */
hal_status_t
hal_thermocouple_set_ambient_resolution(hal_thermocouple_t h,
                                        hal_thermocouple_ambient_res_t res);

/**
 * @brief Enable or put the sensor into sleep / low-power mode.
 *
 * MCP9600 only.  MAX6675 has no sleep mode; this call prints an error.
 *
 * @param h       Valid handle.
 * @param enable  true = normal operation; false = sleep.
 */
hal_status_t hal_thermocouple_enable(hal_thermocouple_t h, bool enable);
#endif /* HAL_ENABLE_MCP9600 */

/**
 * @brief Check whether the sensor is currently in normal operating mode.
 *
 * MCP9600: reads the device-configuration register.
 * MAX6675: always returns true (no sleep mode, no error printed).
 *
 * @param h  Valid handle.
 * @return true if awake and measuring.
 */
bool hal_thermocouple_is_enabled(hal_thermocouple_t h);
hal_status_t hal_thermocouple_is_enabled_ex(hal_thermocouple_t h,
                                            bool *out_enabled);

#ifdef HAL_ENABLE_MCP9600
/**
 * @brief Configure one of the four programmable alert outputs.
 *
 * MCP9600 only.  When @p enabled is false the alert is disabled and
 * @p cfg may be NULL.
 *
 * @param h          Valid handle.
 * @param alert_num  Alert channel number (1-4).
 * @param enabled    true to enable this alert channel.
 * @param cfg        Alert parameters (must be non-NULL when @p enabled).
 */
hal_status_t
hal_thermocouple_set_alert(hal_thermocouple_t h, uint8_t alert_num,
                           bool enabled,
                           const hal_thermocouple_alert_cfg_t *cfg);

/**
 * @brief Read back the configured trigger temperature of an alert channel.
 *
 * MCP9600 only.
 *
 * @param h          Valid handle.
 * @param alert_num  Alert channel number (1-4).
 * @return Trigger temperature in °C, or NAN if unsupported / invalid channel.
 */
float hal_thermocouple_get_alert_temp(hal_thermocouple_t h, uint8_t alert_num);
hal_status_t hal_thermocouple_get_alert_temp_ex(hal_thermocouple_t h,
                                                uint8_t alert_num,
                                                float *out_c);

/**
 * @brief Read the raw 8-bit status register.
 *
 * MCP9600 only. Bit flags are exposed by the shared MCP9600 driver as
 * HAL_MCP9600_STATUS_ALERT1..4, HAL_MCP9600_STATUS_THUPDATE,
 * HAL_MCP9600_STATUS_BURST and HAL_MCP9600_STATUS_INPUTRANGE.
 *
 * @param h  Valid handle.
 * @return Status byte, or 0 if unsupported.
 */
uint8_t hal_thermocouple_get_status(hal_thermocouple_t h);
hal_status_t hal_thermocouple_get_status_ex(hal_thermocouple_t h,
                                            uint8_t *out_status);
#endif /* HAL_ENABLE_MCP9600 */

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_THERMOCOUPLE */
