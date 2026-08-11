#include "hal/hal.h"

#include <type_traits>

static_assert(std::is_pointer_v<hal_lora_radio_t>);
static_assert(std::is_same_v<decltype(&hal_lora_radio_create),
                             hal_status_t (*)(const hal_lora_radio_config_t *,
                                              hal_lora_radio_t *)>);
static_assert(
    std::is_same_v<decltype(&hal_lora_radio_receive),
                   hal_status_t (*)(hal_lora_radio_t, uint8_t *, size_t,
                                    size_t *, hal_lora_packet_info_t *)>);
static_assert(std::is_same_v<decltype(&hal_lora_radio_transmit_start),
                             hal_status_t (*)(hal_lora_radio_t, const uint8_t *,
                                              size_t)>);
static_assert(std::is_same_v<decltype(&hal_lora_radio_get_tx_status),
                             hal_status_t (*)(hal_lora_radio_t,
                                              hal_lora_operation_status_t *)>);
static_assert(std::is_same_v<decltype(&hal_lora_radio_process),
                             hal_status_t (*)(hal_lora_radio_t)>);
static_assert(std::is_same_v<decltype(&hal_lora_time_on_air),
                             hal_status_t (*)(const hal_lora_modem_config_t *,
                                              size_t, uint32_t *)>);

int main() {
  hal_lora_radio_config_t hardware{};
  hardware.model = HAL_LORA_RADIO_SX1276;
  hardware.hardware.sx127x.pa_output = HAL_LORA_SX127X_PA_BOOST;
  hal_lora_modem_config_t modem{};
  modem.frequency_hz = UINT32_C(868100000);
  modem.bandwidth_hz = UINT32_C(125000);
  modem.spreading_factor = 7u;
  return modem.frequency_hz == UINT32_C(868100000) &&
                 hardware.model == HAL_LORA_RADIO_SX1276
             ? 0
             : 1;
}
