/*
 * Blink the board LED, print system diagnostics, and run a simulated PID loop.
 * Software timers schedule the work; a separate timer counts 250 ms intervals.
 */

#include <hal/core/hal_config.h>

#if !defined(HAL_ENABLE_PULSE_CAPTURE)

#include <hal/control/hal_pid_controller.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_array.h>
#include <hal/core/hal_memory.h>
#include <hal/debug/hal_debug_events.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>
#include <hal/timers/hal_soft_timer.h>
#include <hal/timers/hal_timer.h>
#include <stdint.h>

static hal_soft_timer_t s_blink_timer = NULL;
static hal_soft_timer_t s_pid_timer = NULL;
static hal_timer_t s_periodic_timer = NULL;
static volatile uint32_t s_periodic_ticks = 0u;
static bool s_led_on = false;

static hal_pid_controller_t s_pid = NULL;
#define PID_SETPOINT 100.0f
static float s_process_value = 0.0f;
static float s_pid_error = PID_SETPOINT;
static float s_pid_output = 0.0f;
static bool s_pid_stable = false;
static bool s_pid_oscillating = false;
static uint32_t s_last_report_ms = 0u;
static uint8_t s_console_storage[1024];
typedef struct {
  uint32_t us;
  uint32_t ticks;
} runtime_event_t;
static runtime_event_t s_event_storage[8];
static hal_debug_events_t s_events;
static uint32_t s_reported_drops;

/* Boards without a plain GPIO status LED (for example an addressable WS2812
 * only) do not define HAL_LED_BUILTIN; the blink then only toggles state. */
static void HAL_RAM_FUNC(blink_tick)(void) {
  s_led_on = !s_led_on;
#if defined(HAL_LED_BUILTIN)
  hal_gpio_write(HAL_LED_BUILTIN, s_led_on);
#endif
}

static void pid_tick(void) {
  if (s_pid == NULL) {
    return;
  }
  /* The PID update takes seconds; hal_millis() measures milliseconds. */
  hal_pid_controller_update_time(s_pid, 1000.0f);
  s_pid_error = PID_SETPOINT - s_process_value;
  s_pid_output = hal_pid_controller_update(s_pid, s_pid_error);
  s_process_value += s_pid_output * 0.05f;
  s_pid_stable =
      hal_pid_controller_is_error_stable(s_pid, s_pid_error, 1.0f, 10);
  s_pid_oscillating = hal_pid_controller_is_oscillating(s_pid, s_pid_error, 10);
}

static const hal_soft_timer_table_entry_t s_soft_timers[] = {
    {&s_blink_timer, blink_tick, 200u},
    {&s_pid_timer, pid_tick, 200u},
};

static void periodic_tick(hal_timer_t timer, void *user_data) {
  (void)timer;
  (void)user_data;
  ++s_periodic_ticks;
  const runtime_event_t event = {hal_micros(), s_periodic_ticks};
  (void)hal_debug_events_publish(&s_events, &event);
}

static hal_status_t report_event(const void *record, void *context) {
  (void)context;
  const runtime_event_t *const event = record;
  deb("timer event us=%lu ticks=%lu", (unsigned long)event->us,
      (unsigned long)event->ticks);
  return HAL_OK;
}

static void report_architecture(void) {
  hal_system_architecture_t architecture = {0};
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
      (double)PID_SETPOINT, (double)s_process_value, (double)s_pid_error,
      (double)s_pid_output, s_pid_stable ? 1u : 0u,
      s_pid_oscillating ? 1u : 0u);
}

void app_start(void) {
  hal_debug_init_default();
  hal_debug_set_module_prefix("CORE");
  if (hal_debug_events_init(&s_events, s_event_storage,
                            sizeof(s_event_storage[0]),
                            COUNTOF(s_event_storage)) != HAL_OK ||
      hal_serial_buffered_init(s_console_storage, sizeof(s_console_storage)) !=
          HAL_OK) {
    derr("diagnostic initialization failed");
    return;
  }
#if defined(HAL_LED_BUILTIN)
  hal_gpio_set_mode(HAL_LED_BUILTIN, HAL_GPIO_OUTPUT);
  hal_gpio_write(HAL_LED_BUILTIN, false);
#endif

  s_pid = hal_pid_controller_create_with_gains(1.2f, 0.03f, 0.08f, 100.0f);
  if (s_pid == NULL) {
    derr("PID controller allocation failed");
  } else {
    hal_pid_controller_set_output_limits(s_pid, -25.0f, 25.0f);
    hal_pid_controller_set_tf(s_pid, 0.05f);
    hal_pid_controller_reset(s_pid);
  }

  if (!hal_soft_timer_setup_table(s_soft_timers, COUNTOF(s_soft_timers),
                                  hal_watchdog_feed, 2u)) {
    derr("soft-timer table setup failed");
  }

  start_periodic_timer();
  report_architecture();
  deb("core runtime started");
}

void app_task0(void) {
  (void)hal_soft_timer_tick_table(s_soft_timers, COUNTOF(s_soft_timers));
  (void)hal_debug_events_poll(&s_events, 2u, report_event, NULL);

  const uint32_t now = hal_millis();
  if ((uint32_t)(now - s_last_report_ms) >= 1000u) {
    s_last_report_ms = now;
    report_runtime();
    size_t pending = 0u;
    uint32_t tx_drops = 0u;
    if (hal_serial_buffered_status(&pending, &tx_drops) == HAL_OK) {
      const uint32_t drops = tx_drops + hal_debug_events_dropped(&s_events);
      if (drops != s_reported_drops) {
        s_reported_drops = drops;
        derr("diagnostic records dropped=%lu", (unsigned long)drops);
      }
    }
  }

  size_t written = 0u;
  (void)hal_serial_buffered_poll(128u, &written);
  hal_idle();
}

#endif /* !defined(HAL_ENABLE_PULSE_CAPTURE) */
