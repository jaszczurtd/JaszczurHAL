#pragma once

#include "hal_config.h"
#ifdef HAL_ENABLE_PGA2311

#include "hal_spi.h"
#include "hal_status.h"

/**
 * @file hal_pga2311.h
 * @brief Hardware abstraction for the PGA2311 stereo volume controller (SPI).
 *
 * This module exposes an Arduino-free facade over a shared SPI/GPIO transport
 * driver. It is target-agnostic and works on every backend that implements
 * hal_spi and hal_gpio (RP2040, STM32G474, host/mock).
 *
 * Compile-time enable:
 *   HAL_ENABLE_PGA2311 - enables this module and propagates HAL_ENABLE_SPI.
 *
 * The caller must bring the SPI controller up first with hal_spi_init().
 *
 * Thread-safety: each handle owns a mutex. Multi-step SPI transactions are
 * serialized by hal_spi_lock()/hal_spi_unlock() in the shared driver.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of simultaneous PGA2311 handles. */
#ifndef HAL_PGA2311_MAX_INSTANCES
#define HAL_PGA2311_MAX_INSTANCES 4
#endif

/** @brief Sentinel value for an unused pin in config fields. */
#define HAL_PGA2311_PIN_NONE 0xFFu
/** @brief Alias for @ref HAL_PGA2311_PIN_NONE used by mute_pin. */
#define HAL_PGA2311_MUTE_PIN_NONE HAL_PGA2311_PIN_NONE

/** @brief Recommended default SPI clock for PGA2311 control writes: 1 MHz. */
#define HAL_PGA2311_SPI_DEFAULT_HZ 1000000UL

/** @brief Raw gain-code constants (8-bit per channel). */
#define HAL_PGA2311_CODE_MUTE 0x00u
#define HAL_PGA2311_CODE_MIN 0x01u
#define HAL_PGA2311_CODE_0DB 0xC0u
#define HAL_PGA2311_CODE_MAX 0xFFu

/** @brief Gain range in half-dB units for code-based volume control. */
#define HAL_PGA2311_GAIN_HALF_DB_MIN (-191)
#define HAL_PGA2311_GAIN_HALF_DB_MAX (63)

/** @brief Gain range in dB for convenience float APIs. */
#define HAL_PGA2311_GAIN_DB_MIN (-95.5f)
#define HAL_PGA2311_GAIN_DB_MAX (31.5f)

/** @brief Hardware mute input polarity. */
typedef enum {
  HAL_PGA2311_MUTE_ACTIVE_LOW = 0,  /**< mute asserted at logic LOW.  */
  HAL_PGA2311_MUTE_ACTIVE_HIGH = 1, /**< mute asserted at logic HIGH. */
} hal_pga2311_mute_polarity_t;

/**
 * @brief Initialisation descriptor for hal_pga2311_init().
 *
 * Defaults can be obtained via hal_pga2311_default_config().
 */
typedef struct {
  uint8_t spi_bus;  /**< SPI controller index (0 = SPI, 1 = SPI1). */
  uint8_t cs_pin;   /**< Chip-select GPIO pin (required). */
  uint8_t mute_pin; /**< Optional hardware mute pin, or
                       HAL_PGA2311_MUTE_PIN_NONE. */

  hal_pga2311_mute_polarity_t mute_polarity; /**< Hardware mute polarity. */

  uint32_t
      spi_clock_hz; /**< SPI clock in Hz (0 = HAL_PGA2311_SPI_DEFAULT_HZ). */
  uint8_t spi_bit_order; /**< HAL_SPI_MSBFIRST or HAL_SPI_LSBFIRST. */
  uint8_t spi_mode;      /**< HAL_SPI_MODE0..HAL_SPI_MODE3. */

  bool start_muted; /**< Apply mute immediately after init. */
} hal_pga2311_config_t;

/** @brief Opaque handle type; NULL means invalid/uninitialised. */
typedef struct hal_pga2311_impl_s hal_pga2311_impl_t;
typedef hal_pga2311_impl_t *hal_pga2311_t;

/**
 * @brief Return a fully initialised config with safe defaults.
 *
 * The returned descriptor uses bus 0, SPI mode0, MSB-first, 1 MHz clock,
 * no mute pin, active-low mute polarity, and start_muted=false.
 */
hal_pga2311_config_t hal_pga2311_default_config(void);

/**
 * @brief Create and initialise a PGA2311 instance.
 *
 * The function configures CS and optional MUTE pins and reserves a static pool
 * slot. It does not call hal_spi_init(); bus pin mapping is owned by the
 * application/board setup.
 *
 * @param cfg Pointer to configuration descriptor.
 * @param out_handle Receives the created handle on success and NULL on error.
 * @return HAL_OK on success, HAL_EINVAL for invalid arguments/configuration,
 *         HAL_ENOMEM when the static pool or mutex allocation is exhausted,
 *         or a propagated SPI status when start-muted setup writes a frame.
 */
hal_status_t hal_pga2311_init_ex(const hal_pga2311_config_t *cfg,
                                 hal_pga2311_t *out_handle);
/** @brief Compatibility wrapper returning NULL on any init error. */
hal_pga2311_t hal_pga2311_init(const hal_pga2311_config_t *cfg);

/**
 * @brief Release a PGA2311 handle back to the static pool.
 * @param h Handle returned by hal_pga2311_init() (NULL is a no-op).
 */
void hal_pga2311_deinit(hal_pga2311_t h);

/**
 * @brief Set both channel gain codes directly (raw 8-bit register values).
 * @param h Valid handle.
 * @param left_code Left channel raw code.
 * @param right_code Right channel raw code.
 * @return HAL_OK, HAL_EINVAL for an invalid handle, or a propagated SPI error.
 */
hal_status_t hal_pga2311_set_raw_ex(hal_pga2311_t h, uint8_t left_code,
                                    uint8_t right_code);
/** @brief Compatibility wrapper over hal_pga2311_set_raw_ex(). */
bool hal_pga2311_set_raw(hal_pga2311_t h, uint8_t left_code,
                         uint8_t right_code);

/**
 * @brief Set both channels to the same raw code.
 */
hal_status_t hal_pga2311_set_raw_both_ex(hal_pga2311_t h, uint8_t code);
/** @brief Compatibility wrapper over hal_pga2311_set_raw_both_ex(). */
bool hal_pga2311_set_raw_both(hal_pga2311_t h, uint8_t code);

/**
 * @brief Set gain in 0.5 dB units.
 *
 * Valid range: HAL_PGA2311_GAIN_HALF_DB_MIN..HAL_PGA2311_GAIN_HALF_DB_MAX.
 */
hal_status_t hal_pga2311_set_gain_half_db_ex(hal_pga2311_t h,
                                             int16_t left_half_db,
                                             int16_t right_half_db);
/** @brief Compatibility wrapper over hal_pga2311_set_gain_half_db_ex(). */
bool hal_pga2311_set_gain_half_db(hal_pga2311_t h, int16_t left_half_db,
                                  int16_t right_half_db);

/**
 * @brief Set gain in dB, rounded to the nearest 0.5 dB step.
 *
 * Valid range: HAL_PGA2311_GAIN_DB_MIN..HAL_PGA2311_GAIN_DB_MAX.
 */
hal_status_t hal_pga2311_set_gain_db_ex(hal_pga2311_t h, float left_db,
                                        float right_db);
/** @brief Compatibility wrapper over hal_pga2311_set_gain_db_ex(). */
bool hal_pga2311_set_gain_db(hal_pga2311_t h, float left_db, float right_db);

/**
 * @brief Set the same gain for both channels in dB.
 */
hal_status_t hal_pga2311_set_gain_db_both_ex(hal_pga2311_t h, float db);
/** @brief Compatibility wrapper over hal_pga2311_set_gain_db_both_ex(). */
bool hal_pga2311_set_gain_db_both(hal_pga2311_t h, float db);

/**
 * @brief Enable/disable mute.
 *
 * If mute_pin is provided, this toggles hardware mute. Otherwise software mute
 * is emulated by writing HAL_PGA2311_CODE_MUTE to both channels and restoring
 * the cached target codes when unmuted.
 */
hal_status_t hal_pga2311_set_mute_ex(hal_pga2311_t h, bool mute);
/** @brief Compatibility wrapper over hal_pga2311_set_mute_ex(). */
bool hal_pga2311_set_mute(hal_pga2311_t h, bool mute);

/**
 * @brief Query current mute state.
 * @return true if muted; false for unmuted or invalid handle.
 */
bool hal_pga2311_is_muted(hal_pga2311_t h);

/**
 * @brief Read the currently cached target raw codes.
 *
 * These are the target (requested) values, which may differ from hardware
 * output while software mute is active.
 */
bool hal_pga2311_get_target_raw(hal_pga2311_t h, uint8_t *left_code,
                                uint8_t *right_code);

/**
 * @brief Read cached target gain in 0.5 dB units.
 *
 * Returns false if either target code is HAL_PGA2311_CODE_MUTE.
 */
bool hal_pga2311_get_target_gain_half_db(hal_pga2311_t h, int16_t *left_half_db,
                                         int16_t *right_half_db);

/**
 * @brief Convert gain from 0.5 dB units to raw PGA2311 code.
 */
hal_status_t hal_pga2311_gain_half_db_to_raw_ex(int16_t half_db,
                                                uint8_t *out_code);
/** @brief Compatibility wrapper over hal_pga2311_gain_half_db_to_raw_ex(). */
bool hal_pga2311_gain_half_db_to_raw(int16_t half_db, uint8_t *out_code);

/**
 * @brief Convert raw PGA2311 code to gain in 0.5 dB units.
 *
 * HAL_PGA2311_CODE_MUTE is treated as non-convertible and returns false.
 */
hal_status_t hal_pga2311_raw_to_gain_half_db_ex(uint8_t code,
                                                int16_t *out_half_db);
/** @brief Compatibility wrapper over hal_pga2311_raw_to_gain_half_db_ex(). */
bool hal_pga2311_raw_to_gain_half_db(uint8_t code, int16_t *out_half_db);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_PGA2311 */
