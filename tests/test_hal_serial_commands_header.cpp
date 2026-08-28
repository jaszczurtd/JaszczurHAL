#include "hal/serial/hal_serial_commands.h"

#include <type_traits>

static_assert(std::is_same_v<decltype(&hal_serial_commands_deinit),
                             hal_status_t (*)(hal_serial_commands_t *)>);

int main() {
  hal_serial_commands_t commands{};
  return commands.initialized ? 1 : 0;
}
