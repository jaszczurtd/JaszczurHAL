
#include "multicoreWatchdog.h"
#include "SmartTimers.h"
#include <hal/hal.h>

/*
 * Multicore safety analysis (RP2040 Cortex-M0+):
 *
 * The counters watchdogCore{0,1}_a are incremented by their respective core
 * and read by watchdogHandle() (which can run on either core via tick()).
 * On Cortex-M0+ aligned 32-bit reads/writes are atomic and there is no
 * cache or out-of-order execution, so 'volatile' alone is sufficient to
 * guarantee cross-core visibility.
 *
 * watchdogTimer.tick() is called from both cores.  SmartTimers
 * is NOT thread-safe — two concurrent tick() calls may both detect the same
 * timer as expired and invoke the callback twice.  We guard the tick with a
 * hal_mutex to serialise access and eliminate the double-fire race.
 */

static volatile unsigned long watchdogCore0_a = 0, watchdogCore1_a = 0;
static volatile unsigned long watchdogCore0_b = 0, watchdogCore1_b = 0;

static volatile bool core0 = false;
static volatile bool started_a = false;
static volatile bool core1 = false;
static volatile bool started_b = false;

NOINIT bool _core0;
NOINIT bool _started_a;
NOINIT bool _core1;
NOINIT bool _started_b;

static SmartTimers watchdogTimer;
static volatile hal_mutex_t watchdogTickMutex = NULL;
static volatile bool watchdogRuntimeReady = false;

static unsigned int watchdogTime = 0;

static volatile bool externalReset = false;
static bool watchdogStarted = false;

void watchdogHandle(void);

static int valuesToReturn[WATCHDOG_VALUES_AMOUNT];

bool setupWatchdog(void(*function)(int *values, int size), unsigned int time) {

  watchdogTime = time;
  watchdogRuntimeReady = false;

  externalReset = false;

  bool rebooted = hal_watchdog_caused_reboot();
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

  watchdogTimer.begin(watchdogHandle, time / 10);
  watchdogTickMutex = hal_mutex_create();

  deb("Start of Watchdog with time: %ds and refresh %ds", 
    time / SECOND, (time / 10) / SECOND);

  hal_watchdog_enable(watchdogTime, false);
  watchdogStarted = true;
  watchdogRuntimeReady = true;
  watchdog_feed();

  return rebooted;
}

void triggerSystemReset(void) {
  externalReset = true;
}

void watchdogHandle(void) {

  if(externalReset) {
    deb("CAUTION: external reset has been scheduled!");
    return;
  }

  core0 = false;
  core1 = false;
  
  if(watchdogCore0_a != watchdogCore0_b) {
    watchdogCore0_b = watchdogCore0_a;
    core0 = true;
  }

  if(watchdogCore1_a != watchdogCore1_b) {
    watchdogCore1_b = watchdogCore1_a;
    core1 = true;
  }

  _core0 = core0;
  _core1 = core1;

  if(core0 && core1) {
    watchdog_feed();
  }

  return;
}

void updateWatchdogCore0(void) {
  if(!watchdogRuntimeReady) {
    return;
  }

  watchdogCore0_a++;
  hal_mutex_lock(watchdogTickMutex);
  watchdogTimer.tick();
  hal_mutex_unlock(watchdogTickMutex);
}

void updateWatchdogCore1(void) {
  if(!watchdogRuntimeReady) {
    return;
  }

  watchdogCore1_a++;
  hal_mutex_lock(watchdogTickMutex);
  watchdogTimer.tick();
  hal_mutex_unlock(watchdogTickMutex);
}

void setStartedCore0(void) {
  started_a = true;
  _started_a = true;
}

void setStartedCore1(void) {
  started_b = true;
  _started_b = true;
}

bool isEnvironmentStarted(void) {
  return started_a && started_b;
}

void watchdog_feed(void) {
  if(watchdogStarted) {
    hal_watchdog_feed();
  }
}
