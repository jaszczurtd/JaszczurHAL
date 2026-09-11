#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_PULSE_CAPTURE
#include "hal/analog/jh_pulse_capture_backend.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/system/hal_system.h"

namespace {
hal_mutex_t mutex;
bool active, primed;
hal_status_t fault = HAL_OK;
uint32_t clock_hz, timeout_us, first_ticks, last_us, sequence;
uint16_t stride, periods;

hal_status_t read_locked(hal_pulse_capture_sample_t *out) {
  if (!active)
    return HAL_EUNINIT;
  if (fault != HAL_OK)
    return fault;
  for (;;) {
    jh_pulse_capture_edge_t edge;
    hal_status_t status = jh_pulse_capture_next(&edge);
    if (status != HAL_OK) {
      if (status == HAL_EAGAIN) {
        if (hal_elapsed_u32(hal_micros(), last_us, timeout_us)) {
          primed = false;
          periods = 0;
          return HAL_ETIMEOUT;
        }
      } else {
        fault = status;
      }
      return status;
    }
    if (hal_elapsed_u32(hal_micros(), edge.measured_us, timeout_us) ||
        (primed && hal_elapsed_u32(edge.measured_us, last_us, timeout_us))) {
      primed = false;
      periods = 0;
      last_us = edge.measured_us;
      return HAL_ETIMEOUT;
    }
    last_us = edge.measured_us;
    if (!primed) {
      first_ticks = edge.ticks;
      periods = 0;
      primed = true;
      continue;
    }
    periods = (uint16_t)(periods + stride);
    if (periods == 32U) {
      const uint32_t ticks = edge.ticks - first_ticks;
      first_ticks = edge.ticks;
      periods = 0;
      if (ticks == 0U) {
        fault = HAL_EHW;
        return fault;
      }
      *out = {ticks, clock_hz, edge.measured_us, ++sequence, 32U};
      return HAL_OK;
    }
  }
}
} // namespace

hal_status_t hal_pulse_capture_init(const hal_pulse_capture_config_t *config) {
  if (config == nullptr || config->timeout_us < 1000U ||
      config->timeout_us > 100000U)
    return HAL_EINVAL;
  if (active)
    return HAL_EBUSY;
  if (jh_hal_mutex_try_create_once(&mutex) == nullptr)
    return HAL_ENOMEM;
  hal_status_t status = jh_pulse_capture_start(config, &clock_hz, &stride);
  if (status != HAL_OK)
    return status;
  timeout_us = config->timeout_us;
  last_us = hal_micros();
  primed = false;
  periods = 0;
  sequence = 0;
  fault = HAL_OK;
  active = true;
  return HAL_OK;
}

hal_status_t hal_pulse_capture_deinit(void) {
  /* Also release resources retained by a failed backend initialization. */
  hal_status_t status = jh_pulse_capture_stop();
  if (status == HAL_OK)
    active = false;
  return status;
}

hal_status_t hal_pulse_capture_read(hal_pulse_capture_sample_t *out) {
  if (out == nullptr)
    return HAL_EINVAL;
  if (!active)
    return HAL_EUNINIT;
  hal_mutex_lock(mutex);
  hal_status_t status = read_locked(out);
  hal_mutex_unlock(mutex);
  return status;
}
#endif
