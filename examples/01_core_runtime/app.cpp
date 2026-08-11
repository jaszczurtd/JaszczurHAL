#include <hal/core/hal_app.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/system/hal_system.h>
#include <hal/timers/hal_soft_timer.h>
#include <hal/timers/hal_timer.h>
#include <tools.h>
#include <utils/pidController.h>

#include <stdint.h>

static hal_soft_timer_t s_blink_timer = NULL;
static hal_soft_timer_t s_pid_timer = NULL;
static hal_timer_t s_periodic_timer = NULL;
static volatile uint32_t s_periodic_ticks = 0u;
static bool s_led_on = false;

static PIDController s_pid(1.2f, 0.03f, 0.08f, 100.0f);
static const float kPidSetpoint = 100.0f;
static float s_process_value = 0.0f;
static float s_pid_error = kPidSetpoint;
static float s_pid_output = 0.0f;
static bool s_pid_stable = false;
static bool s_pid_oscillating = false;
static uint32_t s_last_report_ms = 0u;

static void blink_tick(void) {
  s_led_on = !s_led_on;
  hal_gpio_write(HAL_LED_BUILTIN, s_led_on);
}

static void pid_tick(void) {
  /* Convert hal_millis() deltas to seconds before every controller update. */
  s_pid.updatePIDtime(1000.0f);
  s_pid_error = kPidSetpoint - s_process_value;
  s_pid_output = s_pid.updatePIDcontroller(s_pid_error);
  s_process_value += s_pid_output * 0.05f;
  s_pid_stable = s_pid.isErrorStable(s_pid_error, 1.0f, 10);
  s_pid_oscillating = s_pid.isOscillating(s_pid_error, 10);
}

static const hal_soft_timer_table_entry_t s_soft_timers[] = {
    {&s_blink_timer, blink_tick, 200u},
    {&s_pid_timer, pid_tick, 200u},
};

static void periodic_tick(hal_timer_t timer, void *user_data) {
  (void)timer;
  (void)user_data;
  ++s_periodic_ticks;
}

static void report_architecture(void) {
  hal_system_architecture_t architecture = {};
  const hal_status_t status =
      hal_system_get_current_architecture(&architecture);
  if (status != HAL_OK) {
    derr("architecture snapshot failed: %s", hal_status_to_string(status));
    return;
  }

  deb("target=%s backend=%s mcu=%s cpu=%s rtos=%s", architecture.target_name,
      architecture.backend_name, architecture.mcu, architecture.cpu_arch,
      architecture.rtos_name);
  deb("cores=%u hw=%u fpu=%u cpu_hz=%lu ram=%lu flash=%lu",
      (unsigned)architecture.cpu_cores, (unsigned)architecture.is_hardware,
      (unsigned)architecture.has_fpu, (unsigned long)architecture.cpu_clock_hz,
      (unsigned long)architecture.ram_usable_bytes,
      (unsigned long)architecture.flash_usable_bytes);
}

static void start_periodic_timer(void) {
  const hal_timer_result_t create_result =
      hal_timer_create(HAL_TIMER_POOL_DEFAULT, 250000u, true, periodic_tick,
                       NULL, &s_periodic_timer);
  if (create_result != HAL_TIMER_OK) {
    derr("timer create failed: %d", (int)create_result);
    return;
  }

  const hal_timer_result_t start_result = hal_timer_start(s_periodic_timer);
  if (start_result != HAL_TIMER_OK) {
    derr("timer start failed: %d", (int)start_result);
    const hal_timer_result_t destroy_result =
        hal_timer_destroy(s_periodic_timer);
    if (destroy_result != HAL_TIMER_OK) {
      derr("timer cleanup failed: %d", (int)destroy_result);
    }
    s_periodic_timer = NULL;
  }
}

static void report_runtime(void) {
  int64_t remaining_us = 0;
  hal_timer_result_t remaining_result = HAL_TIMER_ERR_INVALID_ARG;
  hal_timer_state_t timer_state = HAL_TIMER_STATE_STOPPED;

  if (s_periodic_timer != NULL) {
    remaining_result =
        hal_timer_get_remaining_us(s_periodic_timer, &remaining_us);
    timer_state = hal_timer_get_state(s_periodic_timer);
  }

  deb("timer ticks=%lu state=%d remaining_us=%ld result=%d",
      (unsigned long)s_periodic_ticks, (int)timer_state, (long)remaining_us,
      (int)remaining_result);
  deb("pid set=%.1f pv=%.2f err=%.2f out=%.2f stable=%u oscillating=%u",
      (double)kPidSetpoint, (double)s_process_value, (double)s_pid_error,
      (double)s_pid_output, s_pid_stable ? 1u : 0u,
      s_pid_oscillating ? 1u : 0u);
}

void app_start(void) {
  debugInit();
  hal_deb_set_prefix("CORE");
  hal_gpio_set_mode(HAL_LED_BUILTIN, HAL_GPIO_OUTPUT);
  hal_gpio_write(HAL_LED_BUILTIN, false);

  s_pid.setOutputLimits(-25.0f, 25.0f);
  s_pid.setTf(0.05f);
  s_pid.reset();

  if (!hal_soft_timer_setup_table(
          s_soft_timers, sizeof(s_soft_timers) / sizeof(s_soft_timers[0]),
          hal_watchdog_feed, 2u)) {
    derr("soft-timer table setup failed");
  }

  start_periodic_timer();
  report_architecture();
  deb("core runtime started");
}

void app_task0(void) {
  (void)hal_soft_timer_tick_table(s_soft_timers, sizeof(s_soft_timers) /
                                                     sizeof(s_soft_timers[0]));

  const uint32_t now = hal_millis();
  if ((uint32_t)(now - s_last_report_ms) >= 1000u) {
    s_last_report_ms = now;
    report_runtime();
  }

  hal_idle();
}
