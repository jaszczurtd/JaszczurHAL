
#include "multicoreWatchdog.h"

static unsigned long watchdogCore0_a = 0, watchdogCore1_a = 0;
static unsigned long watchdogCore0_b = 0, watchdogCore1_b = 0;

static std::atomic<bool> core0(false), core1(false);
static std::atomic<bool> started_a(false), started_b(false);

static unsigned int watchdogTime = 0;

static bool externalReset = false;

bool watchdogHandle(void *argument);

bool setupWatchdog(Timer<> *timer, unsigned int time) {

  watchdogTime = time;

  externalReset = false;

  timer->every(time / 10, watchdogHandle);

  deb("Start of Watchdog with time: %ds and refresh %ds", 
    time / 1000, (time / 10) / 1000);

  bool rebooted = watchdog_caused_reboot();
  if (rebooted) {
    deb("Rebooted by Watchdog!\n");
    deb("core 0 started:%d updated:%d \n", started_a.load(), core0.load());
    deb("core 1 started:%d updated:%d \n", started_b.load(), core1.load());

  } else {
    deb("Clean boot\n");
  }

  watchdog_enable(watchdogTime, false);

  return rebooted;
}

void triggerSystemReset(void) {
  externalReset = true;
}

bool watchdogHandle(void *argument) {

  if(externalReset) {
    return true;
  }

  core0.store(false);
  core1.store(false);
  
  if(watchdogCore0_a != watchdogCore0_b) {
    watchdogCore0_b = watchdogCore0_a;
    core0.store(true);
  }

  if(watchdogCore1_a != watchdogCore1_b) {
    watchdogCore1_b = watchdogCore1_a;
    core1.store(true);
  }

  if(core0.load() && core1.load()) {
    watchdog_update();
  }

  return true;
}

void updateWatchdogCore0(void) {
  watchdogCore0_a++;  
}

void updateWatchdogCore1(void) {
  watchdogCore1_a++;
}

void setStartedCore0(void) {
  started_a.store(true);
}

void setStartedCore1(void) {
  started_b.store(true);
}

bool isEnvironmentStarted(void) {
  return started_a.load() && started_b.load();
}
