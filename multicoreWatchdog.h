
#ifndef T_WATCHDOG
#define T_WATCHDOG

#include <arduino-timer.h>
#include <atomic> 
#include <stdint.h>
#include "tools.h"

bool setupWatchdog(Timer<> *timer, unsigned int time);
void updateWatchdogCore0(void);
void updateWatchdogCore1(void);

void setStartedCore0(void);
void setStartedCore1(void);
bool isEnvironmentStarted(void);

void triggerSystemReset(void);

#endif
