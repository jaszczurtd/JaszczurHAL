/* Measure a periodic GPIO input using hardware timestamps. */
#include <JaszczurHAL.h>

/* The standalone native build also discovers this optional source. */
#ifdef HAL_ENABLE_PULSE_CAPTURE

static hal_pulse_capture_sample_t latest;
static hal_status_t capture_status = HAL_EUNINIT;
static uint32_t reported_ms;

void app_start(void) {
  hal_debug_init_default();
  hal_debug_set_module_prefix("capture");
  const hal_pulse_capture_config_t config = {0U, true, 10000U};
  hal_gpio_set_mode(config.pin, HAL_GPIO_INPUT_PULLUP);
  capture_status = hal_pulse_capture_init(&config);
  if (capture_status != HAL_OK)
    derr("capture init: %s", hal_status_to_string(capture_status));
}

void app_task0(void) {
  hal_pulse_capture_sample_t sample;
  hal_status_t status;
  while ((status = hal_pulse_capture_read(&sample)) == HAL_OK) {
    latest = sample;
    capture_status = HAL_OK;
  }
  if (status != HAL_EAGAIN)
    capture_status = status;
  if (hal_millis_interval_elapsed_now(&reported_ms, 250U)) {
    if (capture_status == HAL_OK &&
        !hal_elapsed_u32(hal_micros(), latest.measured_us, 10000U)) {
      const uint32_t hz =
          (uint32_t)(((uint64_t)latest.periods * latest.clock_hz +
                      latest.ticks / 2U) /
                     latest.ticks);
      deb("frequency=%lu Hz age=%lu us", (unsigned long)hz,
          (unsigned long)(hal_micros() - latest.measured_us));
    } else {
      deb("capture: %s", hal_status_to_string(capture_status));
    }
  }
  hal_delay_ms(1U);
}

#endif
