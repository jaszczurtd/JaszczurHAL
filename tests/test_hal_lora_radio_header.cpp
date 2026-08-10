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
static_assert(std::is_same_v<decltype(&hal_lora_time_on_air),
                             hal_status_t (*)(const hal_lora_modem_config_t *,
                                              size_t, uint32_t *)>);

int main() {
  hal_lora_modem_config_t modem{};
  modem.frequency_hz = UINT32_C(868100000);
  modem.bandwidth_hz = UINT32_C(125000);
  modem.spreading_factor = 7u;
  return modem.frequency_hz == UINT32_C(868100000) ? 0 : 1;
}
