#include "hal/radio/hal_lora_link.h"

#include <type_traits>

static_assert(std::is_pointer_v<hal_lora_link_t>);
static_assert(
    std::is_same_v<decltype(&hal_lora_link_send_start),
                   hal_status_t (*)(hal_lora_link_t, uint16_t, uint8_t,
                                    const uint8_t *, size_t, bool)>);
static_assert(std::is_same_v<decltype(&hal_lora_link_process),
                             hal_status_t (*)(hal_lora_link_t)>);

int main() {
  hal_lora_link_message_info_t info{};
  return info.encrypted ? 1 : 0;
}
