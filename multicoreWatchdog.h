
#ifndef T_WATCHDOG
#define T_WATCHDOG

#include "libConfig.h"
#include <arduino-timer.h>
#include <atomic> 
#include <stdint.h>
#include "tools.h"

#define WATCHDOG_VALUES_AMOUNT 4

bool setupWatchdog(void(*function)(int *values, int size), unsigned int time);
void updateWatchdogCore0(void);
void updateWatchdogCore1(void);

void setStartedCore0(void);
void setStartedCore1(void);
bool isEnvironmentStarted(void);

void triggerSystemReset(void);
void watchdog_feed(void);

#endif
