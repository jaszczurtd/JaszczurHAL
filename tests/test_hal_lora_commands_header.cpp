#include "hal/radio/hal_lora_commands.h"

#include <type_traits>

static_assert(std::is_pointer_v<hal_lora_commands_t>);
static_assert(std::is_same_v<decltype(&hal_lora_commands_process),
                             hal_status_t (*)(hal_lora_commands_t)>);

int main() {
  hal_lora_commands_diagnostics_t diagnostics{};
  return diagnostics.process_calls == 0u ? 0 : 1;
}
