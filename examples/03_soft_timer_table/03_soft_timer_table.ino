#include <JaszczurHAL.h>
#include <hal/hal_soft_timer.h>
#include <hal/hal_system.h>

static hal_soft_timer_t fast_timer = NULL;
static hal_soft_timer_t slow_timer = NULL;

static void onFast(void) {
  hal_deb("fast tick");
}

static void onSlow(void) {
  hal_deb("slow tick");
}

static const hal_soft_timer_table_entry_t timers[] = {
  { &fast_timer, onFast, 50 },
  { &slow_timer, onSlow, 1000 }
};

void setup() {
  hal_debug_init(115200);
  if (!hal_soft_timer_setup_table(timers,
                                  sizeof(timers) / sizeof(timers[0]),
                                  hal_watchdog_feed,
                                  2)) {
    hal_derr("timer table setup failed");
  }
}

void loop() {
  (void)hal_soft_timer_tick_table(timers, sizeof(timers) / sizeof(timers[0]));
}