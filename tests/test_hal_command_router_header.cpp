#include "hal/commands/hal_command_router.h"
#include "hal/commands/hal_command_wire.h"

#include <type_traits>

static_assert(std::is_pointer_v<hal_command_router_t>);
static_assert(std::is_same_v<decltype(&hal_command_router_dispatch),
                             hal_status_t (*)(hal_command_router_t,
                                              const hal_command_request_t *,
                                              hal_command_response_t *)>);

int main() {
  hal_command_message_t message{};
  return message.payload_length == 0u ? 0 : 1;
}
