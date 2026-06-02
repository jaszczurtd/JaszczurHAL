#include <JaszczurHAL.h>

static volatile uint32_t timer_ticks = 0;
static hal_timer_t periodic_timer = NULL;
static uint32_t last_report_ms = 0;

static void onPeriodicTimer(hal_timer_t timer, void *user_data) {
  (void)timer;
  (void)user_data;
  timer_ticks++;
}

void setup() {
  hal_debug_init(115200);

  const hal_timer_result_t create_result =
      hal_timer_create(HAL_TIMER_POOL_DEFAULT,
                       250000u,
                       true,
                       onPeriodicTimer,
                       NULL,
                       &periodic_timer);
  if (create_result != HAL_TIMER_OK) {
    hal_derr("timer create failed: %d", (int)create_result);
    return;
  }

  const hal_timer_result_t start_result = hal_timer_start(periodic_timer);
  if (start_result != HAL_TIMER_OK) {
    hal_derr("timer start failed: %d", (int)start_result);
  }
}

void loop() {
  const uint32_t now = hal_millis();
  if (now - last_report_ms < 1000u) {
    return;
  }
  last_report_ms = now;

  int64_t remaining_us = 0;
  const hal_timer_result_t remaining_result =
      hal_timer_get_remaining_us(periodic_timer, &remaining_us);

  hal_deb("timer ticks=%lu state=%d remaining=%ld result=%d",
          (unsigned long)timer_ticks,
          (int)hal_timer_get_state(periodic_timer),
          (long)remaining_us,
          (int)remaining_result);
}
