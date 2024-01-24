
#include "multicoreWatchdog.h"

static unsigned long watchdogCore0_a = 0, watchdogCore1_a = 0;
static unsigned long watchdogCore0_b = 0, watchdogCore1_b = 0;

static std::atomic<bool> core0(false);
static std::atomic<bool> started_a(false);
static std::atomic<bool> core1(false);
static std::atomic<bool> started_b(false);

NOINIT bool _core0;
NOINIT bool _started_a;
NOINIT bool _core1;
NOINIT bool _started_b;

static Timer watchdogTimer;

static unsigned int watchdogTime = 0;

static bool externalReset = false;
static bool watchdogStarted = false;

bool watchdogHandle(void *argument);

static int valuesToReturn[WATCHDOG_VALUES_AMOUNT];

bool setupWatchdog(void(*function)(int *values, int size), unsigned int time) {

  watchdogTime = time;

  externalReset = false;

  bool rebooted = watchdog_caused_reboot();
  if (rebooted) {
    deb("Rebooted by Watchdog!\n");

    valuesToReturn[0] = _started_a;
    valuesToReturn[1] = _core0;
    valuesToReturn[2] = _started_b;
    valuesToReturn[3] = _core1;

    if(function != NULL) {
      function(valuesToReturn, WATCHDOG_VALUES_AMOUNT);
    }

    saveLoggerAndClose();

  } else {
    deb("Clean boot\n");
  }

  _core0 = false;
  _started_a = false;
  _core1 = false;
  _started_b = false;

  watchdogTimer = timer_create_default();
  watchdogTimer.every(time / 10, watchdogHandle);

  deb("Start of Watchdog with time: %ds and refresh %ds", 
    time / SECOND, (time / 10) / SECOND);

  watchdog_enable(watchdogTime, false);
  watchdogStarted = true;
  watchdog_feed();

  return rebooted;
}

void triggerSystemReset(void) {
  externalReset = true;
}

bool watchdogHandle(void *argument) {

  if(externalReset) {
    deb("CAUTION: external reset has been scheduled!");
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

  _core0 = core0.load();
  _core1 = core1.load();

  if(core0.load() && core1.load()) {
    watchdog_feed();
  }

  return true;
}

void updateWatchdogCore0(void) {
  watchdogCore0_a++;  
  watchdogTimer.tick();
}

void updateWatchdogCore1(void) {
  watchdogCore1_a++;
  watchdogTimer.tick();
}

void setStartedCore0(void) {
  started_a.store(true);
  _started_a = true;
}

void setStartedCore1(void) {
  started_b.store(true);
  _started_b = true;
}

bool isEnvironmentStarted(void) {
  return started_a.load() && started_b.load();
}

void watchdog_feed(void) {
  if(watchdogStarted) {
    watchdog_update();
  }
}