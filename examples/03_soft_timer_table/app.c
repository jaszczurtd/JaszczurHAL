#include <hal/hal_app.h>
#include <hal/hal_soft_timer.h>
#include <hal/hal_system.h>
#include <tools_c.h>

static hal_soft_timer_t fast_timer = NULL;
static hal_soft_timer_t slow_timer = NULL;

static void onFast(void) { deb("fast tick"); }

static void onSlow(void) { deb("slow tick"); }

static const hal_soft_timer_table_entry_t timers[] = {
    {&fast_timer, onFast, 50}, {&slow_timer, onSlow, 1000}};

void app_start(void) {
  debugInit();
  if (!hal_soft_timer_setup_table(timers, sizeof(timers) / sizeof(timers[0]),
                                  hal_watchdog_feed, 2)) {
    derr("timer table setup failed");
  }
}

void app_task0(void) {
  (void)hal_soft_timer_tick_table(timers, sizeof(timers) / sizeof(timers[0]));
}
