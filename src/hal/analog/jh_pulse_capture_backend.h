#pragma once
#include "hal/analog/hal_pulse_capture.h"

/* Internal backend interface. Calls are serialized by the public facade.
 * next returns a hardware timestamp and its conservative wall-clock mapping.
 * stride is the number of input periods between consecutive timestamps.
 */
typedef struct {
  uint32_t ticks;
  uint32_t measured_us;
} jh_pulse_capture_edge_t;

hal_status_t jh_pulse_capture_start(const hal_pulse_capture_config_t *config,
                                    uint32_t *clock_hz, uint16_t *stride);
/* Idempotent, including before start and after a failed/partial start. */
hal_status_t jh_pulse_capture_stop(void);
hal_status_t jh_pulse_capture_next(jh_pulse_capture_edge_t *edge);
