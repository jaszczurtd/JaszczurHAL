#include "hal/hal_lora_radio.h"

_Static_assert(HAL_LORA_RADIO_MAX_INSTANCES >= 1u,
               "LoRa instance pool must not be empty");
_Static_assert(HAL_LORA_PIN_NONE == UINT8_MAX,
               "LoRa optional-pin sentinel drifted");
_Static_assert(HAL_LORA_SPI_CLOCK_DEFAULT_HZ == UINT32_C(8000000),
               "LoRa default SPI clock drifted");
_Static_assert(HAL_LORA_RF_SWITCH_DIO2_SINGLE_GPIO == 3,
               "combined RF switch mode drifted");

int main(void) {
  hal_lora_radio_config_t config = {0};
  config.model = HAL_LORA_RADIO_SX1262;
  config.hardware.sx126x.rf_switch_mode = HAL_LORA_RF_SWITCH_DIO2_SINGLE_GPIO;
  return config.model == HAL_LORA_RADIO_SX1262 ? 0 : 1;
}
