#pragma once

#include "hal/radio/hal_lora_radio.h"
#include "utils/unity.h"

static inline hal_lora_radio_config_t jh_test_lora_radio_config(void) {
  hal_lora_radio_config_t config = {};
  config.model = HAL_LORA_RADIO_SX1262;
  config.spi_bus = 0u;
  config.spi_miso_pin = 16u;
  config.spi_mosi_pin = 19u;
  config.spi_sck_pin = 18u;
  config.cs_pin = 17u;
  config.spi_clock_hz = HAL_LORA_SPI_CLOCK_DEFAULT_HZ;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_sx126x_core1262_hf_defaults(&config.hardware.sx126x));
  config.hardware.sx126x.reset_pin = 20u;
  config.hardware.sx126x.busy_pin = 21u;
  config.hardware.sx126x.dio1_pin = 22u;
  config.hardware.sx126x.rf_switch_pin_a = 10u;
  config.hardware.sx126x.rf_switch_pin_b = 11u;
  return config;
}
