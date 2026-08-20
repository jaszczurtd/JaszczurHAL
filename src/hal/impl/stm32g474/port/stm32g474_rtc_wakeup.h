#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JH_G474_RTC_WAKEUP_RESOLUTION_US UINT64_C(1000000)
#define JH_G474_RTC_WAKEUP_MAX_SECONDS UINT64_C(65536)
#define JH_G474_RTC_WAKEUP_MAX_TIMEOUT_US                                      \
  (JH_G474_RTC_WAKEUP_MAX_SECONDS * JH_G474_RTC_WAKEUP_RESOLUTION_US)

static inline bool
jh_stm32g474_rtc_wakeup_compute(uint64_t timeout_us, uint16_t *out_counter,
                                uint64_t *out_programmed_timeout_us) {
  if (timeout_us == 0u || out_counter == NULL ||
      out_programmed_timeout_us == NULL ||
      timeout_us > JH_G474_RTC_WAKEUP_MAX_TIMEOUT_US) {
    return false;
  }

  const uint64_t seconds =
      (timeout_us + JH_G474_RTC_WAKEUP_RESOLUTION_US - 1u) /
      JH_G474_RTC_WAKEUP_RESOLUTION_US;
  *out_counter = (uint16_t)(seconds - 1u);
  *out_programmed_timeout_us = seconds * JH_G474_RTC_WAKEUP_RESOLUTION_US;
  return true;
}
