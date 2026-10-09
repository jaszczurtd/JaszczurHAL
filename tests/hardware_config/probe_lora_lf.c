/* The board-owned radio of the RP2040-LoRa-LF as a HAL configuration. */
#include "jh_hardware.h"

#include "hal/radio/hal_lora_radio.h"

static const hal_lora_radio_config_t radio =
    JH_HW_NODE_BOARD_LORA_RADIO_CONFIG_INIT;

int main(void) {
  int failures = 0;
  failures +=
      JH_HW_NODE_LORA_PRESENT != 0 || JH_HW_NODE_LORA_CONFIG_AVAILABLE != 0;
  failures += radio.spi_bus != 1u || radio.spi_miso_pin != 24u;
  failures += radio.hardware.sx126x.rf_switch_pin_b != HAL_LORA_PIN_NONE;
  failures += radio.hardware.sx126x.max_spi_clock_hz != 17999999u;
  failures += JH_HW_HAS_SX1262_RADIO != 1;
  return failures;
}
