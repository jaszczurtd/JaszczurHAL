/**
 * @file lora_example_radio.h
 * @brief SX1262 wiring and modem settings shared by the ping/pong and command
 * examples.
 *
 * A board with a soldered radio, such as `rp2040-lora-lf`, describes it in its
 * board profile. On any other board the example assumes an external Waveshare
 * Core1262-HF wired as below; change the pins to match your own wiring.
 */

#pragma once

#include <hal/core/hal_status.h>
#include <hal/core/hal_target.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/radio/hal_lora_radio.h>

#include <stdbool.h>
#include <stdint.h>

#if HAL_TARGET_IS_STM32G474
/* NUCLEO-G474RE: SPI2 on PB13/PB14/PB15, control lines on PB0..PB5. */
#define EXAMPLE_LORA_SPI_BUS 1u
#define EXAMPLE_LORA_SCK_PIN HAL_GPIO_STM32_PIN('B', 13u)
#define EXAMPLE_LORA_MISO_PIN HAL_GPIO_STM32_PIN('B', 14u)
#define EXAMPLE_LORA_MOSI_PIN HAL_GPIO_STM32_PIN('B', 15u)
#define EXAMPLE_LORA_CS_PIN HAL_GPIO_STM32_PIN('B', 0u)
#define EXAMPLE_LORA_RESET_PIN HAL_GPIO_STM32_PIN('B', 1u)
#define EXAMPLE_LORA_BUSY_PIN HAL_GPIO_STM32_PIN('B', 2u)
#define EXAMPLE_LORA_DIO1_PIN HAL_GPIO_STM32_PIN('B', 3u)
#define EXAMPLE_LORA_RXEN_PIN HAL_GPIO_STM32_PIN('B', 4u)
#define EXAMPLE_LORA_TXEN_PIN HAL_GPIO_STM32_PIN('B', 5u)
#else
/* Raspberry Pi Pico: SPI0 on GP16/GP18/GP19, control lines on GP10/GP11 and
 * GP17/GP20..GP22. */
#define EXAMPLE_LORA_SPI_BUS 0u
#define EXAMPLE_LORA_SCK_PIN 18u
#define EXAMPLE_LORA_MISO_PIN 16u
#define EXAMPLE_LORA_MOSI_PIN 19u
#define EXAMPLE_LORA_CS_PIN 17u
#define EXAMPLE_LORA_RESET_PIN 20u
#define EXAMPLE_LORA_BUSY_PIN 21u
#define EXAMPLE_LORA_DIO1_PIN 22u
#define EXAMPLE_LORA_RXEN_PIN 10u
#define EXAMPLE_LORA_TXEN_PIN 11u
#endif

/** Fixed LF test frequency; not a region-specific regulatory setting. */
#define EXAMPLE_LORA_LF_TEST_FREQUENCY_HZ UINT32_C(434000000)

/**
 * @brief Radio configuration from the board profile, or the Core1262-HF wiring
 *        above when the board declares no radio.
 *
 * Waveshare names the RF switch lines after the path they turn off: RXEN is
 * high while transmitting and TXEN while receiving. With RXEN on switch pin A
 * and TXEN on pin B, hal_lora_sx126x_core1262_hf_defaults() sets these levels.
 *
 * @param out_config Filled configuration.
 * @param out_from_board Set to true when the board profile supplied it.
 * @return HAL_OK or the status of the failing helper.
 */
static inline hal_status_t
example_lora_radio_config(hal_lora_radio_config_t *out_config,
                          bool *out_from_board) {
  hal_status_t status = hal_lora_radio_config_from_board(out_config);
  *out_from_board = status == HAL_OK;
  if (status != HAL_EUNSUPPORTED) {
    return status;
  }
  *out_config = (hal_lora_radio_config_t){0};
  out_config->model = HAL_LORA_RADIO_SX1262;
  out_config->spi_bus = EXAMPLE_LORA_SPI_BUS;
  out_config->spi_miso_pin = EXAMPLE_LORA_MISO_PIN;
  out_config->spi_mosi_pin = EXAMPLE_LORA_MOSI_PIN;
  out_config->spi_sck_pin = EXAMPLE_LORA_SCK_PIN;
  out_config->cs_pin = EXAMPLE_LORA_CS_PIN;
  out_config->spi_clock_hz = HAL_LORA_SPI_CLOCK_DEFAULT_HZ;
  status = hal_lora_sx126x_core1262_hf_defaults(&out_config->hardware.sx126x);
  out_config->hardware.sx126x.reset_pin = EXAMPLE_LORA_RESET_PIN;
  out_config->hardware.sx126x.busy_pin = EXAMPLE_LORA_BUSY_PIN;
  out_config->hardware.sx126x.dio1_pin = EXAMPLE_LORA_DIO1_PIN;
  out_config->hardware.sx126x.rf_switch_pin_a = EXAMPLE_LORA_RXEN_PIN;
  out_config->hardware.sx126x.rf_switch_pin_b = EXAMPLE_LORA_TXEN_PIN;
  return status;
}

/**
 * @brief EU868 modem settings at 10 dBm, or the LF test frequency for an LF
 *        radio. `HAL_LORA_EXAMPLE_SF` and `HAL_LORA_EXAMPLE_TX_POWER_DBM`
 *        override the spreading factor and TX power.
 */
static inline hal_lora_modem_config_t
example_lora_modem_config(const hal_lora_radio_config_t *hardware) {
  hal_lora_modem_config_t modem = hal_lora_default_eu868();
  modem.tx_power_dbm = 10;
  if (hardware->hardware.sx126x.max_frequency_hz < UINT32_C(800000000)) {
    modem.frequency_hz = EXAMPLE_LORA_LF_TEST_FREQUENCY_HZ;
  }
#ifdef HAL_LORA_EXAMPLE_SF
  modem.spreading_factor = HAL_LORA_EXAMPLE_SF;
#endif
#ifdef HAL_LORA_EXAMPLE_TX_POWER_DBM
  modem.tx_power_dbm = HAL_LORA_EXAMPLE_TX_POWER_DBM;
#endif
  return modem;
}
