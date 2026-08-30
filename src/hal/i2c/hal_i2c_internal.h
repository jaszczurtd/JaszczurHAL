#ifndef JH_HAL_I2C_INTERNAL_H
#define JH_HAL_I2C_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "hal/i2c/hal_i2c.h"

bool jh_hal_i2c_bus_is_initialized(uint8_t bus);

#ifdef HAL_ENABLE_I2C_10BIT
/** @brief True when the selected bus was initialised in 10-bit addressing
 *         mode (hal_i2c_init_10bit()/hal_i2c_init_bus_10bit()). */
bool jh_hal_i2c_bus_is_10bit(uint8_t bus);
#endif

/**
 * @brief Validate @p address against the addressing mode of @p bus.
 *
 * 7-bit-mode buses accept 0x000..0x07F; 10-bit-mode buses accept
 * 0x000..0x3FF. Backends call this once at the top of every address-taking
 * entry point instead of re-deriving the range check.
 */
static inline hal_status_t
jh_hal_i2c_validate_address(uint8_t bus, hal_i2c_address_t address) {
#ifdef HAL_ENABLE_I2C_10BIT
  if (jh_hal_i2c_bus_is_10bit(bus)) {
    return (address <= 0x3FFu) ? HAL_OK : HAL_EINVAL;
  }
#else
  (void)bus;
#endif
  return (address <= 0x7Fu) ? HAL_OK : HAL_EINVAL;
}

#endif
