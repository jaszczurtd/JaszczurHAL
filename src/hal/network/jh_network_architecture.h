#pragma once

#include "hal/core/hal_config.h"
#include "hal/system/hal_system.h"

/** Fill compile-time-selected network identity in an architecture snapshot. */
static inline void
jh_network_architecture_fill(hal_system_architecture_t *architecture) {
#if defined(HAL_ENABLE_NETWORK_CORE) && defined(HAL_NETWORK_BACKEND_CYW43)
  architecture->network_backend_name = "cyw43-host-lwip";
  architecture->network_stack_name = "lwIP";
  architecture->network_stack_type = HAL_SYSTEM_NETWORK_STACK_TYPE_HOST;
#elif defined(HAL_ENABLE_NETWORK_CORE) && defined(HAL_NETWORK_BACKEND_ESP_IDF)
  architecture->network_backend_name = "esp-idf-native";
  architecture->network_stack_name = "lwIP";
  architecture->network_stack_type = HAL_SYSTEM_NETWORK_STACK_TYPE_HOST;
#elif defined(HAL_ENABLE_NETWORK_CORE) && defined(HAL_NETWORK_BACKEND_MOCK)
  architecture->network_backend_name = "mock-host-stack";
  architecture->network_stack_name = "mock";
  architecture->network_stack_type = HAL_SYSTEM_NETWORK_STACK_TYPE_HOST;
#elif defined(HAL_ENABLE_NETWORK_CORE) && defined(HAL_NETWORK_BACKEND_ESP_AT)
  architecture->network_backend_name = "esp-at";
  architecture->network_stack_name = "ESP-AT";
  architecture->network_stack_type =
      HAL_SYSTEM_NETWORK_STACK_TYPE_SOCKET_OFFLOAD;
#else
  architecture->network_backend_name = "none";
  architecture->network_stack_name = "none";
  architecture->network_stack_type = HAL_SYSTEM_NETWORK_STACK_TYPE_NONE;
#endif
}
