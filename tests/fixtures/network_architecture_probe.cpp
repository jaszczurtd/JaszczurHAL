#include "hal/network/jh_network_architecture.h"

#include <cstring>

int main(void) {
  hal_system_architecture_t architecture = {};
  jh_network_architecture_fill(&architecture);

#if defined(JH_EXPECT_NETWORK_NONE)
  const char *expected_backend = "none";
  const char *expected_stack = "none";
  const hal_system_network_stack_type_t expected_type =
      HAL_SYSTEM_NETWORK_STACK_TYPE_NONE;
#elif defined(JH_EXPECT_NETWORK_MOCK)
  const char *expected_backend = "mock-host-stack";
  const char *expected_stack = "mock";
  const hal_system_network_stack_type_t expected_type =
      HAL_SYSTEM_NETWORK_STACK_TYPE_HOST;
#elif defined(JH_EXPECT_NETWORK_CYW43)
  const char *expected_backend = "cyw43-host-lwip";
  const char *expected_stack = "lwIP";
  const hal_system_network_stack_type_t expected_type =
      HAL_SYSTEM_NETWORK_STACK_TYPE_HOST;
#elif defined(JH_EXPECT_NETWORK_ESP_AT)
  const char *expected_backend = "esp-at";
  const char *expected_stack = "ESP-AT";
  const hal_system_network_stack_type_t expected_type =
      HAL_SYSTEM_NETWORK_STACK_TYPE_SOCKET_OFFLOAD;
#else
#error "Select a JH_EXPECT_NETWORK_* identity"
#endif

  return std::strcmp(architecture.network_backend_name, expected_backend) !=
                     0 ||
                 std::strcmp(architecture.network_stack_name, expected_stack) !=
                     0 ||
                 architecture.network_stack_type != expected_type
             ? 1
             : 0;
}
