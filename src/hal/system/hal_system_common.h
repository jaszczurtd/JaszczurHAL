#ifndef JH_HAL_SYSTEM_COMMON_H
#define JH_HAL_SYSTEM_COMMON_H

#include "hal/system/hal_system.h"

static inline const char *jh_hal_reset_reason_str(hal_reset_reason_t reason) {
  switch (reason) {
  case HAL_RESET_REASON_POWER_ON:
    return "POWER_ON";
  case HAL_RESET_REASON_RUN_PIN:
    return "RUN_PIN";
  case HAL_RESET_REASON_SOFT:
    return "SOFT";
  case HAL_RESET_REASON_WATCHDOG:
    return "WATCHDOG";
  case HAL_RESET_REASON_DEBUG:
    return "DEBUG";
  case HAL_RESET_REASON_GLITCH:
    return "GLITCH";
  case HAL_RESET_REASON_BROWNOUT:
    return "BROWNOUT";
  case HAL_RESET_REASON_HARDFAULT:
    return "HARDFAULT";
  case HAL_RESET_REASON_STACK_OVERFLOW:
    return "STACK_OVERFLOW";
  case HAL_RESET_REASON_UNKNOWN:
  default:
    return "UNKNOWN";
  }
}

#endif
