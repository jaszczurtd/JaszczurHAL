#pragma once

/**
 * @file hal_dac.h
 * @brief Hardware abstraction for digital-to-analog conversion (true DAC).
 *
 * Opt-in module: enable with `HAL_ENABLE_DAC`.
 *
 * Channel numbering is uniform across backends:
 *   channel 0 -> first DAC output, channel 1 -> second, ...
 * On STM32G474 these map to DAC1_OUT1 (PA4) and DAC1_OUT2 (PA5); 12-bit.
 *
 * Capability differs by target. The RP2040 has NO true DAC peripheral, so on
 * that backend hal_dac_is_supported() returns false, hal_dac_init() returns
 * false, and writes are no-ops (use hal_pwm + an RC filter instead). Always
 * check hal_dac_is_supported() in portable code.
 *
 * Values are right-aligned in the range [0, hal_dac_max_value()].
 */

#include <stdbool.h>
#include <stdint.h>

#include "hal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Whether this target has a real DAC peripheral.
 * @return true if DAC channels can be used; false (e.g. on RP2040) otherwise.
 */
bool hal_dac_is_supported(void);

/**
 * @brief DAC resolution in bits (e.g. 12 on STM32G474).
 * @return Resolution in bits, or 0 if the target has no DAC.
 */
uint8_t hal_dac_resolution_bits(void);

/**
 * @brief Maximum raw code, i.e. (1 << resolution) - 1.
 * @return Max value, or 0 if the target has no DAC.
 */
uint16_t hal_dac_max_value(void);

/**
 * @brief Initialise a DAC channel (clock, pin to analog, enable output).
 * @param channel Channel index (0-based).
 * @return HAL_OK on success, HAL_EUNSUPPORTED when the target has no DAC, or
 *         HAL_EINVAL when the channel is invalid.
 */
hal_status_t hal_dac_init_ex(uint8_t channel);

/**
 * @brief Initialise a DAC channel (clock, pin to analog, enable output).
 *
 * Compatibility wrapper over hal_dac_init_ex().
 *
 * @param channel Channel index (0-based).
 * @return true on success; false if unsupported or the channel is invalid.
 */
bool hal_dac_init(uint8_t channel);

/**
 * @brief Write a raw code to a DAC channel.
 * @param channel Channel index.
 * @param value   Code in [0, hal_dac_max_value()]; values above are clamped.
 * @return HAL_OK on success, HAL_EUNSUPPORTED when the target has no DAC,
 *         HAL_EINVAL when the channel is invalid, or HAL_EUNINIT when the
 *         channel has not been initialized.
 */
hal_status_t hal_dac_write_ex(uint8_t channel, uint16_t value);

/**
 * @brief Write a raw code to a DAC channel.
 *
 * Compatibility wrapper over hal_dac_write_ex().
 *
 * @param channel Channel index.
 * @param value   Code in [0, hal_dac_max_value()]; values above are clamped.
 */
void hal_dac_write(uint8_t channel, uint16_t value);

/**
 * @brief Write an output voltage expressed in millivolts.
 *
 * Converts using the reference voltage @ref HAL_DAC_VREF_MV (default 3300 mV;
 * override with -DHAL_DAC_VREF_MV=...). The result is clamped to full scale.
 *
 * @param channel      Channel index.
 * @param millivolts   Desired output in mV (clamped to [0, VREF]).
 * @return HAL_OK on success, HAL_EUNSUPPORTED when the target has no DAC,
 *         HAL_EINVAL when the channel is invalid, or HAL_EUNINIT when the
 *         channel has not been initialized.
 */
hal_status_t hal_dac_write_millivolts_ex(uint8_t channel, uint16_t millivolts);

/**
 * @brief Write an output voltage expressed in millivolts.
 *
 * Compatibility wrapper over hal_dac_write_millivolts_ex().
 *
 * @param channel      Channel index.
 * @param millivolts   Desired output in mV (clamped to [0, VREF]).
 */
void hal_dac_write_millivolts(uint8_t channel, uint16_t millivolts);

#ifdef __cplusplus
}
#endif
