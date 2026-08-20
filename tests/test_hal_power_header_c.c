#include "hal/power/hal_power.h"

int main(void) {
  const hal_power_request_t request = {
      HAL_POWER_STATE_SLEEP,
      HAL_POWER_POLICY_FAST_WAKE,
      HAL_POWER_WAKE_SOURCE_INTERRUPT,
      0,
      0u,
      0,
      0,
      0,
  };
  return request.state == HAL_POWER_STATE_SLEEP ? 0 : 1;
}
