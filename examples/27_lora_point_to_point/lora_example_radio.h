/**
 * @file lora_example_radio.h
 * @brief SX1262 wiring and modem settings shared by the ping/pong and command
 * examples.
 *
 * The radio comes from device_tree.json: an external Waveshare Core1262-HF
 * node or the radio soldered on the board (`rp2040-lora-lf`). Change the
 * wiring there, not in the code.
 */

#pragma once

#include <hal/core/hal_status.h>
#include <hal/radio/hal_lora_radio.h>

#include <stdbool.h>
#include <stdint.h>

/** Fixed LF test frequency; not a region-specific regulatory setting. */
#define EXAMPLE_LORA_LF_TEST_FREQUENCY_HZ UINT32_C(434000000)

/**
 * @brief Radio configuration of the selected assembly.
 *
 * The external Core1262-HF takes its wiring and module facts from the
 * generated initializer. Waveshare names the RF switch lines after the path
 * they turn off: RXEN is high while transmitting and TXEN while receiving,
 * so RXEN drives switch pin A and TXEN pin B. A radio on the board comes from
 * its board description through hal_lora_radio_config_from_board().
 *
 * @param out_config Filled configuration.
 * @param out_from_board Set to true when the board's own radio is used.
 * @return HAL_OK, or the status of hal_lora_radio_config_from_board().
 */
static inline hal_status_t
example_lora_radio_config(hal_lora_radio_config_t *out_config,
                          bool *out_from_board) {
#if JH_HW_NODE_RADIO_MODULE_RADIO_CONFIG_AVAILABLE
  *out_config =
      (hal_lora_radio_config_t)JH_HW_NODE_RADIO_MODULE_RADIO_CONFIG_INIT;
  *out_from_board = false;
  return HAL_OK;
#else
  const hal_status_t status = hal_lora_radio_config_from_board(out_config);
  *out_from_board = status == HAL_OK;
  return status;
#endif
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
