/* Initializers of the pico-lora example, filled into the HAL types. */
#include "jh_hardware.h"

#include "hal/can/hal_can.h"
#include "hal/radio/hal_lora_radio.h"
#include "hal/serial/hal_uart.h"
#include "hal/spi/hal_spi.h"

static const hal_lora_radio_config_t radio = JH_HW_NODE_LORA_RADIO_CONFIG_INIT;
static const hal_can_config_t can = JH_HW_NODE_CAN_CONFIG_INIT;
static const hal_spi_settings_t can_spi = JH_HW_NODE_CAN_SPI_SETTINGS_INIT;
static const uint16_t frame = JH_HW_BUS_CONSOLE_FRAME_CONFIG;

int main(void) {
  int failures = 0;
  const hal_lora_sx126x_hardware_config_t *hw = &radio.hardware.sx126x;
  failures += radio.model != HAL_LORA_RADIO_SX1262;
  failures += radio.spi_bus != JH_HW_NODE_LORA_RADIO_BUS_INDEX;
  failures += radio.spi_sck_pin != JH_HW_BUS_RADIO_PIN_SCK;
  failures += radio.cs_pin != JH_HW_NODE_LORA_RADIO_PIN_CS;
  failures += radio.spi_clock_hz != JH_HW_NODE_LORA_RADIO_FREQUENCY_HZ;
  failures += hw->rf_switch_mode != HAL_LORA_RF_SWITCH_DUAL_GPIO;
  failures += hw->rf_switch_pin_b != JH_HW_NODE_LORA_RADIO_PIN_RF_SWITCH_B;
  failures +=
      hw->max_spi_clock_hz != JH_HW_NODE_LORA_RADIO_PROP_MAX_SPI_CLOCK_HZ;
  failures +=
      hw->min_tx_power_dbm != JH_HW_NODE_LORA_RADIO_PROP_MIN_TX_POWER_DBM;
  failures += can.backend != HAL_CAN_BACKEND_MCP2515;
  failures += can.mcp2515.cs_pin != JH_HW_NODE_CAN_PIN_CS;
  failures += can.mcp2515.bitrate_hz != JH_HW_NODE_CAN_PROP_BITRATE_HZ;
  failures += can_spi.clock_hz != JH_HW_NODE_CAN_FREQUENCY_HZ;
  failures += can_spi.bit_order != HAL_SPI_MSBFIRST ||
              can_spi.data_mode != HAL_SPI_MODE0;
  failures += frame != HAL_UART_CFG_8E1;
  failures += JH_HW_BUS_CONSOLE_UART_PORT != (unsigned)HAL_UART_PORT_2;
  failures += JH_HW_NODE_LAMP_PIN_OUT_DOMAIN != 2 ||
              JH_HW_NODE_LAMP_PIN_OUT_INDEX != 3u;
  return failures;
}
