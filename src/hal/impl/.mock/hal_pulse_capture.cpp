#include "hal/core/hal_config.h"
#if HAL_TARGET_IS_MOCK && defined(HAL_ENABLE_PULSE_CAPTURE)
#include "hal/analog/jh_pulse_capture_backend.h"
#include "hal/core/hal_array.h"
#include "hal/impl/.mock/hal_mock.h"

namespace {
jh_pulse_capture_edge_t queue[1024];
uint32_t written, consumed;
bool running;
hal_status_t injected_fault;
} // namespace

hal_status_t jh_pulse_capture_start(const hal_pulse_capture_config_t *config,
                                    uint32_t *clock_hz, uint16_t *stride) {
  if (config->pin >= 64U)
    return HAL_EINVAL;
  written = consumed = 0;
  injected_fault = HAL_OK;
  running = true;
  *clock_hz = 16000000U;
  *stride = 1;
  return HAL_OK;
}

hal_status_t jh_pulse_capture_stop(void) {
  running = false;
  return HAL_OK;
}

hal_status_t jh_pulse_capture_next(jh_pulse_capture_edge_t *edge) {
  if (injected_fault != HAL_OK)
    return injected_fault;
  if (written - consumed > COUNTOF(queue))
    return HAL_EOVERFLOW;
  if (written == consumed)
    return HAL_EAGAIN;
  *edge = queue[consumed++ % COUNTOF(queue)];
  return HAL_OK;
}

hal_status_t hal_mock_pulse_capture_edge(uint32_t ticks, uint32_t measured_us) {
  if (!running)
    return HAL_EUNINIT;
  queue[written++ % COUNTOF(queue)] = {ticks, measured_us};
  return HAL_OK;
}

void hal_mock_pulse_capture_fault(hal_status_t status) {
  injected_fault = status;
}
#endif
