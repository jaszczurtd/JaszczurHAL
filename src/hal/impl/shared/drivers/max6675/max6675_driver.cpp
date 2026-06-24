/*
 * MAX6675 transaction logic is modeled after the Adafruit MAX6675 Arduino
 * library by Limor Fried for Adafruit Industries. This implementation was
 * rewritten as an Arduino-free JaszczurHAL shared driver: it keeps the proven
 * CS timing, two 8-bit MSB-first reads, fault-bit check and 0.25 C/LSB decode,
 * but uses only HAL GPIO, delay and synchronization primitives.
 *
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2019, Limor Fried for Adafruit Industries
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "hal/hal_target.h"
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "hal/hal_config.h"
#ifdef HAL_ENABLE_MAX6675

#include "max6675_driver.h"

#include "hal/hal_gpio.h"
#include "hal/hal_sync.h"
#include "hal/hal_system.h"

#include <math.h>
#include <stddef.h>

#define MAX6675_OPEN_CIRCUIT_BIT 0x0004u
#define MAX6675_BIT_DELAY_US 10u
#define MAX6675_CS_DELAY_US 10u

static uint8_t hal_max6675_spiread(hal_max6675_t *dev) {
  uint8_t d = 0u;

  for (int bit = 7; bit >= 0; --bit) {
    hal_gpio_write(dev->cfg.sclk_pin, false);
    hal_delay_us(MAX6675_BIT_DELAY_US);
    if (hal_gpio_read(dev->cfg.miso_pin)) {
      d |= (uint8_t)(1u << bit);
    }

    hal_gpio_write(dev->cfg.sclk_pin, true);
    hal_delay_us(MAX6675_BIT_DELAY_US);
  }

  return d;
}

bool hal_max6675_init(hal_max6675_t *dev, const hal_max6675_config_t *cfg) {
  if (dev == NULL || cfg == NULL) {
    return false;
  }

  dev->cfg = *cfg;
  dev->mutex = hal_mutex_create();
  if (dev->mutex == NULL) {
    return false;
  }

  hal_gpio_set_mode(dev->cfg.cs_pin, HAL_GPIO_OUTPUT);
  hal_gpio_set_mode(dev->cfg.sclk_pin, HAL_GPIO_OUTPUT);
  hal_gpio_set_mode(dev->cfg.miso_pin, HAL_GPIO_INPUT);

  hal_gpio_write(dev->cfg.cs_pin, true);
  return true;
}

void hal_max6675_deinit(hal_max6675_t *dev) {
  if (dev == NULL || dev->mutex == NULL) {
    return;
  }
  hal_mutex_destroy(dev->mutex);
  dev->mutex = NULL;
}

uint16_t hal_max6675_read_raw(hal_max6675_t *dev) {
  if (dev == NULL || dev->mutex == NULL) {
    return 0u;
  }

  hal_mutex_lock(dev->mutex);

  hal_gpio_write(dev->cfg.cs_pin, false);
  hal_delay_us(MAX6675_CS_DELAY_US);

  uint16_t raw = hal_max6675_spiread(dev);
  raw <<= 8;
  raw |= hal_max6675_spiread(dev);

  hal_gpio_write(dev->cfg.cs_pin, true);

  hal_mutex_unlock(dev->mutex);

  return raw;
}

bool hal_max6675_raw_has_fault(uint16_t raw) {
  return (raw & MAX6675_OPEN_CIRCUIT_BIT) != 0u;
}

float hal_max6675_raw_to_celsius(uint16_t raw) {
  if (hal_max6675_raw_has_fault(raw)) {
    return NAN;
  }
  return (float)(raw >> 3) * 0.25f;
}

float hal_max6675_read_celsius(hal_max6675_t *dev) {
  if (dev == NULL || dev->mutex == NULL) {
    return NAN;
  }
  return hal_max6675_raw_to_celsius(hal_max6675_read_raw(dev));
}

float hal_max6675_read_fahrenheit(hal_max6675_t *dev) {
  return hal_max6675_read_celsius(dev) * 9.0f / 5.0f + 32.0f;
}

float hal_max6675_read_farenheit(hal_max6675_t *dev) {
  return hal_max6675_read_fahrenheit(dev);
}

#endif /* HAL_ENABLE_MAX6675 */
#endif /* HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 ||                   \
          HAL_TARGET_IS_MOCK */
