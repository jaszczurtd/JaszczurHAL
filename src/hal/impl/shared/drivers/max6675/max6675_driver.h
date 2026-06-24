#pragma once

/**
 * @file max6675_driver.h
 * @brief Arduino-free MAX6675 thermocouple reader built on JaszczurHAL GPIO.
 *
 * The MAX6675 is a read-only, SPI-like device. This shared driver deliberately
 * bit-bangs the tiny 16-bit transaction through hal_gpio/hal_system so it can
 * run unchanged on RP2040, STM32G474, and the host mock backend without
 * depending on Arduino.h, SPI.h, or a target-specific SPI object.
 */

#include "hal/hal_config.h"

#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK) && \
    defined(HAL_ENABLE_MAX6675)

#include "hal/hal_sync.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t sclk_pin;
  uint8_t cs_pin;
  uint8_t miso_pin;
} hal_max6675_config_t;

typedef struct {
  hal_max6675_config_t cfg;
  hal_mutex_t mutex;
} hal_max6675_t;

bool hal_max6675_init(hal_max6675_t *dev, const hal_max6675_config_t *cfg);
void hal_max6675_deinit(hal_max6675_t *dev);
uint16_t hal_max6675_read_raw(hal_max6675_t *dev);
bool hal_max6675_raw_has_fault(uint16_t raw);
float hal_max6675_raw_to_celsius(uint16_t raw);
float hal_max6675_read_celsius(hal_max6675_t *dev);
float hal_max6675_read_fahrenheit(hal_max6675_t *dev);
float hal_max6675_read_farenheit(hal_max6675_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* supported target && HAL_ENABLE_MAX6675 */
