#include "hal/radio/hal_lora_radio.h"

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
  hal_lora_operation_status_t operation = {HAL_LORA_OPERATION_IDLE, HAL_NONE};
  hal_lora_radio_event_t event = {HAL_LORA_RADIO_EVENT_TX_COMPLETE,
                                  HAL_LORA_OPERATION_KIND_TRANSMIT, HAL_OK, 0u,
                                  false};
  config.model = HAL_LORA_RADIO_SX1262;
  config.hardware.sx126x.rf_switch_mode = HAL_LORA_RF_SWITCH_DIO2_SINGLE_GPIO;
  return config.model == HAL_LORA_RADIO_SX1262 &&
                 operation.state == HAL_LORA_OPERATION_IDLE &&
                 event.result == HAL_OK
             ? 0
             : 1;
}
