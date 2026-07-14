#pragma once

/**
 * @file onewire_driver.h
 * @brief Arduino-free 1-Wire bus driver built on JaszczurHAL GPIO/time.
 *
 * This shared driver mirrors Paul Stoffregen's OneWire Arduino library API and
 * timing behaviour while replacing Arduino.h, direct GPIO macros and
 * delayMicroseconds() with JaszczurHAL primitives. It is an internal
 * implementation detail used by hal_onewire and hal_ds18b20 on RP2040 and
 * STM32G474.
 */

#include "hal/hal_target.h"
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "hal/hal_config.h"
#ifdef HAL_ENABLE_ONEWIRE

#include <stdbool.h>
#include <stdint.h>

class JHOneWire {
public:
  JHOneWire();
  explicit JHOneWire(uint8_t pin);

  void begin(uint8_t pin);

  uint8_t reset(void);
  void select(const uint8_t rom[8]);
  void skip(void);

  void write(uint8_t value, uint8_t power = 0u);
  void write_bytes(const uint8_t *buf, uint16_t count, bool power = false);
  uint8_t read(void);
  void read_bytes(uint8_t *buf, uint16_t count);

  void write_bit(uint8_t value);
  uint8_t read_bit(void);
  void depower(void);

  void reset_search(void);
  void target_search(uint8_t family_code);
  bool search(uint8_t *new_addr, bool search_mode = true);

  /* CRC-8/CRC-16 moved to hal_crc.h (hal_crc8_maxim / hal_crc16_maxim). */

private:
  uint8_t pin_;
  uint8_t rom_no_[8];
  uint8_t last_discrepancy_;
  uint8_t last_family_discrepancy_;
  bool last_device_flag_;
};

#endif /* HAL_ENABLE_ONEWIRE */
#endif /* supported target */
